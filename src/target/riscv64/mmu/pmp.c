#include "pmp.h"

#include <cpu/cpu.h>
#include <cpu/csr_list.h>

/*
 * 16 entry，粒度 4 字节。OFF/TOR/NA4/NAPOT。
 * 最低编号且碰到任一字节的 entry 决定结果；部分重叠直接拒绝，
 * 未锁定的 M 模式也不能跨过这个 entry。没有任何 entry 命中时，
 * 实现了 PMP 的 S/U 访问失败，M 访问成功。
 */

enum {
  BITS_PER_BYTE = 8,
  PMP_ADDR_BITS = 54,
  PMP_NA4_BYTES = 4,
  PMP_CFG_ENTRIES = 8,
  PMP_CFG_CSR_COUNT = 16,
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

bool riscv_pmp_csr(word_t addr) {
  bool cfg = addr >= CSR_PMPCFG0 && addr < CSR_PMPCFG0 + PMP_CFG_CSR_COUNT &&
             (addr % 2) == 0;
  bool pmpaddr = addr >= CSR_PMPADDR0 && addr < CSR_PMPADDR0 + PMP_ADDR_CSR_COUNT;
  return cfg || pmpaddr;
}

static unsigned cfg_first(word_t addr) {
  /* RV64 的 pmpcfg 地址间隔是 2，每个寄存器装 8 个 entry。 */
  return (unsigned)(addr - CSR_PMPCFG0) * (PMP_CFG_ENTRIES / 2);
}

word_t riscv_pmp_read(word_t addr) {
  if (addr >= CSR_PMPADDR0) {
    unsigned index = (unsigned)(addr - CSR_PMPADDR0);
    return index < cpu.pmp_count ? cpu.pmpaddr[index] : 0;
  }
  if ((addr & 1) != 0 || addr < CSR_PMPCFG0) return 0;
  unsigned first = cfg_first(addr);
  word_t value = 0;
  for (unsigned i = 0; i < PMP_CFG_ENTRIES && first + i < cpu.pmp_count; i++) {
    value |= (word_t)cpu.pmpcfg[first + i] << (BITS_PER_BYTE * i);
  }
  return value;
}

void riscv_pmp_write(word_t addr, word_t value) {
  if (addr >= CSR_PMPADDR0) {
    unsigned index = (unsigned)(addr - CSR_PMPADDR0);
    if (index >= cpu.pmp_count || (cpu.pmpcfg[index] & PMP_LOCK) != 0) return;
    unsigned tor = (unsigned)PMP_AMATCH_TOR << PMP_AMATCH_SHIFT;
    if (index + 1 < cpu.pmp_count &&
        (cpu.pmpcfg[index + 1] & (PMP_LOCK | PMP_AMATCH)) == (PMP_LOCK | tor)) {
      return;
    }
    cpu.pmpaddr[index] = value & PMPADDR_ADDR_RV64;
    return;
  }
  if ((addr & 1) != 0 || addr < CSR_PMPCFG0) return;
  unsigned first = cfg_first(addr);
  word_t keep = PMP_READ | PMP_WRITE | PMP_EXEC | PMP_AMATCH | PMP_LOCK;
  for (unsigned i = 0; i < PMP_CFG_ENTRIES && first + i < cpu.pmp_count; i++) {
    if ((cpu.pmpcfg[first + i] & PMP_LOCK) != 0) continue;
    uint8_t cfg = (uint8_t)((value >> (BITS_PER_BYTE * i)) & keep);
    if ((cfg & PMP_READ) == 0) cfg = (uint8_t)(cfg & ~PMP_WRITE);
    cpu.pmpcfg[first + i] = cfg;
  }
}

static unsigned pmp_permission(MemAccessType type) {
  if (type == kMemIFetch) return PMP_EXEC;
  if (type == kMemRead || type == kMemPteRead) return PMP_READ;
  return PMP_WRITE;
}

bool riscv_pmp_check(paddr_t addr, word_t len, MemAccessType type, Priv privilege) {
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
      unsigned ones =
          (unsigned)__builtin_ctzll(~cpu.pmpaddr[i] | (UINT64_C(1) << PMP_ADDR_BITS));
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

bool isa_pmp_permit(paddr_t addr, word_t len, MemAccessType type, Priv privilege) {
  return riscv_pmp_check(addr, len, type, privilege);
}
