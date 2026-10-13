#include <cpu/cpu.h>
#include <cpu/interrupt.h>
#include <memory/paddr.h>
#include <memory/vaddr.h>
#include "pmp.h"
#include "mmu.h"

/* RV64 有效特权、对齐、翻译、PMP 访问规则集中于此。
 * 失败返回结构化结果，由指令边界统一写 trap CSR，不在内存层改 CPU。
 */
static uint8_t effective_privilege(int type) {
  if (type != MEM_TYPE_IFETCH && cpu.priv == PRV_M &&
      (cpu.mstatus & MSTATUS_MPRV)) {
    return (uint8_t)BITS(cpu.mstatus, 12, 11);
  }
  return cpu.priv;
}

MemAccess isa_mem_prepare(vaddr_t addr, int len, int type, bool ram_only) {
  MemAccess a = {.valid = true, .type = type, .length = len,
                 .vaddr = addr};
  if ((len != 1 && len != 2 && len != 4 && len != 8) ||
      type < MEM_TYPE_IFETCH || type > MEM_TYPE_WRITE) {
    a.status = MEM_HOST_ERROR; /* 非法宿主调用，不能制造客户机异常 */
    return a;
  }
  const word_t access_cause = type == MEM_TYPE_IFETCH ? EX_IAF :
                             type == MEM_TYPE_READ ? EX_LAF : EX_SAF;
  const word_t aligned_cause = type == MEM_TYPE_READ ? EX_LAM : EX_SAM;

  /* prepare 始终检查自然对齐，尤其不能让 LR/SC/AMO 获得拆分语义。
   * 可选普通标量拆分在 access 中进入，逐字节走同一权限/翻译路径。
   * fetch 使用 halfword；IALIGN 由 decoder/控制流检查。 */
  if (type != MEM_TYPE_IFETCH && (addr & (word_t)(len - 1))) {
    a.status = MEM_FAULT;
    a.cause = aligned_cause;
    a.tval = addr;
    return a;
  }

  uint8_t privilege=effective_privilege(type);
  if (!riscv_mmu_translate(&a,privilege)) return a;
  if (a.paddr >= (UINT64_C(1) << 56) ||
      (paddr_t)len > (UINT64_C(1) << 56) - a.paddr ||
      (ram_only && !mem_in_ram(a.paddr, len)) ||
      !riscv_pmp_check(a.paddr,len,type,privilege)) {
    a.status = MEM_FAULT;
    a.cause = access_cause;
    a.tval = addr;
  }
  return a;
}

MemAccess isa_mem_prepare_atomic(vaddr_t addr, int len, bool ram_only) {
  MemAccess read = isa_mem_prepare(addr, len, MEM_TYPE_READ, ram_only);
  if (read.status != MEM_OK) {
    /* AMO faults are reported in the store/AMO class even when the required
     * read half fails first. */
    if (read.status == MEM_FAULT) {
      read.type = MEM_TYPE_WRITE;
      read.cause = read.cause == EX_LAM ? EX_SAM :
                   read.cause == EX_LPF ? EX_SPF : EX_SAF;
    }
    return read;
  }
  /* Translate again through the write path. This preserves its distinct
   * permission/fault cause and performs the required PTE D-bit update. */
  return isa_mem_prepare(addr, len, MEM_TYPE_WRITE, ram_only);
}

#ifdef CONFIG_RV_MISALIGNED_NATIVE
static MemAccess isa_mem_access_native_unaligned(vaddr_t addr, int len, int type,
                                                 word_t data) {
  MemAccess result = {.valid = true, .type = type, .length = len,
                      .vaddr = addr, .status = MEM_OK};
  if (addr > UINT64_MAX - (word_t)(len - 1)) {
    result.status = MEM_FAULT;
    result.cause = type == MEM_TYPE_READ ? EX_LAF : EX_SAF;
    result.tval = addr;
    return result;
  }

  MemAccess bytes[8];
  for (int i = 0; i < len; i++) {
    bytes[i] = isa_mem_prepare(addr + (word_t)i, 1, type, false);
    if (bytes[i].status != MEM_OK) {
      result.status = bytes[i].status;
      result.cause = bytes[i].cause;
      result.tval = bytes[i].tval;
      return result;
    }
    if (!bytes[i].paddr_valid || bytes[i].mmio ||
        !mem_in_ram(bytes[i].paddr, 1)) {
      result.status = MEM_FAULT;
      result.cause = type == MEM_TYPE_READ ? EX_LAF : EX_SAF;
      result.tval = addr;
      return result;
    }
    if (i == 0) result.paddr = bytes[i].paddr;
  }
  result.paddr_valid = true;

  if (type == MEM_TYPE_READ) {
    result.data = 0;
    for (int i = 0; i < len; i++) {
      word_t byte;
      if (!paddr_try_read(bytes[i].paddr, 1, &byte, NULL)) {
        result.status = MEM_HOST_ERROR;
        return result;
      }
      result.data |= (byte & 0xffu) << (8 * i);
    }
  } else {
    for (int i = 0; i < len; i++) {
      if (!paddr_try_write(bytes[i].paddr, 1, (data >> (8 * i)) & 0xffu,
                           NULL)) {
        result.status = MEM_HOST_ERROR;
        return result;
      }
    }
    result.data = data & (UINT64_MAX >> (64 - len * 8));
    result.write_performed = true;
  }
  return result;
}
#endif

MemAccess isa_mem_access(vaddr_t addr, int len, int type, word_t data) {
#ifdef CONFIG_RV_MISALIGNED_NATIVE
  if ((len == 2 || len == 4 || len == 8) &&
      (type == MEM_TYPE_READ || type == MEM_TYPE_WRITE) &&
      (addr & (word_t)(len - 1)))
    return isa_mem_access_native_unaligned(addr, len, type, data);
#endif
  MemAccess a = isa_mem_prepare(addr, len, type, false);
  if (a.status != MEM_OK) return a;
  a.data = data;
  bool ok = false;
  if (type == MEM_TYPE_WRITE) {
    a.data &= UINT64_MAX >> (64 - len * 8);
    ok = paddr_try_write(a.paddr, len, a.data, &a.mmio);
    a.write_performed = ok;
  } else {
    ok = paddr_try_read(a.paddr, len, &a.data, &a.mmio);
  }
  if (!ok) {
    a.status = MEM_FAULT;
    a.cause = type == MEM_TYPE_IFETCH ? EX_IAF :
              type == MEM_TYPE_READ ? EX_LAF : EX_SAF;
    a.tval = addr;
  }
  return a;
}
