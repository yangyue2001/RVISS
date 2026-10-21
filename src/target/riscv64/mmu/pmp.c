#include "pmp.h"

#include <cpu/cpu.h>

/*
 * 16 entry，粒度 4 字节。OFF/TOR/NA4/NAPOT。
 * 最低编号且碰到任一字节的 entry 决定结果；部分重叠直接拒绝，
 * 未锁定的 M 模式也不能跨过这个 entry。没有任何 entry 命中时，
 * 实现了 PMP 的 S/U 访问失败，M 访问成功。
 */

enum {
  PMP_ADDR_BITS = 54,
  PMP_NA4_BYTES = 4,
  PMP_ADDR_CSR_COUNT = 64
};

_Static_assert(((UINT64_C(1) << PMP_ADDR_BITS) - 1) == PMPADDR_ADDR_RV64,
               "pmpaddr width");
_Static_assert((unsigned)CPU_PMP_ENTRIES <= (unsigned)PMP_ADDR_CSR_COUNT,
               "PMP entry count");

void riscv_pmp_reset(void) {
  memset(cpu.pmpcfg, 0, sizeof(cpu.pmpcfg));
  memset(cpu.pmpaddr, 0, sizeof(cpu.pmpaddr));
#ifdef CONFIG_RV_PMP
  riscv_pmp_set_regions(CPU_PMP_ENTRIES);
#else
  riscv_pmp_set_regions(0);
#endif
}

void riscv_pmp_set_regions(unsigned count) {
#ifdef CONFIG_RV_PMP
  Assert(count == 0 || count == CPU_PMP_ENTRIES,
         "PMP profile must have 0 or %d entries", CPU_PMP_ENTRIES);
#else
  Assert(count == 0, "PMP unavailable in this build");
#endif
  cpu.pmp_count = (uint8_t)count;
}

static unsigned pmp_permission(MemAccessType type) {
  if (type == kMemIFetch) return PMP_EXEC;
  if (type == kMemRead || type == kMemPteRead) return PMP_READ;
  /* store 与 AMO 都检查写权限。 */
  return PMP_WRITE;
}

bool riscv_pmp_check(paddr_t addr, word_t len, MemAccessType type,
                     Priv privilege) {
  if (len == 0 || addr > UINT64_MAX - (len - 1)) return false;
  if (cpu.pmp_count == 0) return true;
  paddr_t end = addr + len;
  for (unsigned i = 0; i < cpu.pmp_count; i++) {
    uint8_t cfg = cpu.pmpcfg[i];
    unsigned mode = (cfg & PMP_AMATCH) >> PMP_AMATCH_SHIFT;
    if (mode == PMP_AMATCH_OFF) continue;

    paddr_t low = 0, high = 0;
    if (mode == PMP_AMATCH_TOR) {
      low = i == 0 ? 0 : (cpu.pmpaddr[i - 1] << 2);
      high = cpu.pmpaddr[i] << 2;
      if (low >= high) continue;
    } else if (mode == PMP_AMATCH_NA4) {
      low = cpu.pmpaddr[i] << 2;
      high = low + PMP_NA4_BYTES;
    } else {
      unsigned ones = (unsigned)__builtin_ctzll(~cpu.pmpaddr[i] |
                                                (UINT64_C(1) << PMP_ADDR_BITS));
      low = (cpu.pmpaddr[i] & ~((UINT64_C(1) << ones) - 1)) << 2;
      high = low + (UINT64_C(1) << (ones + 3));
    }
    if (addr >= high || end <= low) continue;
    if (addr < low || end > high) return false;
    if (privilege == PRV_M && (cfg & PMP_LOCK) == 0) return true;
    return (cfg & pmp_permission(type)) != 0;
  }
  return privilege == PRV_M;
}

bool isa_pmp_permit(paddr_t addr, word_t len, MemAccessType type,
                    Priv privilege) {
  return riscv_pmp_check(addr, len, type, privilege);
}
