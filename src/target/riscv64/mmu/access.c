#include <cpu/cpu.h>
#include <cpu/interrupt.h>
#include <memory/paddr.h>
#include <memory/vaddr.h>

/* RV64 访问规则集中于此。当前只有 Bare；PMP/页表规则后续接在此路径。
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

  /* profile 在翻译前拒绝非自然对齐的数据访问。fetch 使用 halfword，
   * IALIGN 在 decoder/控制流检查，第二 halfword 不按 4-byte 对齐拒绝。
   */
  if (type != MEM_TYPE_IFETCH && (addr & (word_t)(len - 1))) {
    a.status = MEM_FAULT;
    a.cause = aligned_cause;
    a.tval = addr;
    return a;
  }

  if (effective_privilege(type) != PRV_M && isa_mmu_enabled()) {
    a.status = MEM_HOST_ERROR; /* Sv39/PMP 尚未实现：不能声称 page fault */
    return a;
  }

  a.paddr = addr; /* Bare */
  a.paddr_valid = true;
  if (addr >= (UINT64_C(1) << 56) ||
      (paddr_t)len > (UINT64_C(1) << 56) - addr ||
      (ram_only && !mem_in_ram(a.paddr, len))) {
    a.status = MEM_FAULT;
    a.cause = access_cause;
    a.tval = addr;
  }
  return a;
}

MemAccess isa_mem_access(vaddr_t addr, int len, int type, word_t data) {
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
