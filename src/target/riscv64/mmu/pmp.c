#include "pmp.h"

#include <cpu/cpu.h>
#include <memory/vaddr.h>

#define PMP_ADDR_MASK ((UINT64_C(1) << 54) - 1)

void riscv_pmp_reset(void) {
  memset(cpu.pmpcfg, 0, sizeof(cpu.pmpcfg));
  memset(cpu.pmpaddr, 0, sizeof(cpu.pmpaddr));
#ifdef CONFIG_RV_PMP
  riscv_pmp_set_regions(16);
#else
  riscv_pmp_set_regions(0);
#endif
}

void riscv_pmp_set_regions(unsigned count) {
#ifdef CONFIG_RV_PMP
  Assert(count == 0 || count == 16, "PMP profile must have 0 or 16 regions");
#else
  Assert(count == 0, "PMP unavailable in this build");
#endif
  cpu.pmp_count = count;
}

bool riscv_pmp_csr(word_t addr) {
  return (addr >= CSR_PMPADDR0 && addr < CSR_PMPADDR0 + 64) ||
         (addr >= CSR_PMPCFG0 && addr < CSR_PMPCFG0 + 16 && !(addr & 1));
}

word_t riscv_pmp_read(word_t addr) {
  if (addr >= CSR_PMPADDR0) {
    unsigned index = addr - CSR_PMPADDR0;
    return index < cpu.pmp_count ? cpu.pmpaddr[index] : 0;
  }
  unsigned first = (addr - CSR_PMPCFG0) * 4;
  word_t value = 0;
  for (unsigned i = 0; i < 8 && first + i < cpu.pmp_count; i++)
    value |= (word_t)cpu.pmpcfg[first + i] << (8 * i);
  return value;
}

void riscv_pmp_write(word_t addr, word_t value) {
  if (addr >= CSR_PMPADDR0) {
    unsigned i = addr - CSR_PMPADDR0;
    if (i >= cpu.pmp_count || (cpu.pmpcfg[i] & PMP_L)) return;
    if (i + 1 < cpu.pmp_count &&
        (cpu.pmpcfg[i + 1] & (PMP_L | PMP_A)) == (PMP_L | PMP_TOR))
      return;
    cpu.pmpaddr[i] = value & PMP_ADDR_MASK;
    return;
  }
  unsigned first = (addr - CSR_PMPCFG0) * 4;
  for (unsigned i = 0; i < 8 && first + i < cpu.pmp_count; i++) {
    if (cpu.pmpcfg[first + i] & PMP_L) continue;
    uint8_t cfg = (value >> (8 * i)) & (PMP_R | PMP_W | PMP_X | PMP_A | PMP_L);
    if (!(cfg & PMP_R)) cfg &= ~PMP_W; /* R=0/W=1 保留组合的 WARL 选择 */
    cpu.pmpcfg[first + i] = cfg;
  }
}

bool riscv_pmp_check(paddr_t addr, int len, int type, uint8_t privilege) {
  if (len <= 0 || addr > UINT64_MAX - (unsigned)len) return false;
  if (!cpu.pmp_count) return true;
  paddr_t end = addr + (unsigned)len;
  for (unsigned i = 0; i < cpu.pmp_count; i++) {
    uint8_t cfg = cpu.pmpcfg[i], mode = cfg & PMP_A;
    if (mode == PMP_OFF) continue;
    paddr_t low, high;
    if (mode == PMP_TOR) {
      low = i ? cpu.pmpaddr[i - 1] << 2 : 0;
      high = cpu.pmpaddr[i] << 2;
      if (low >= high) continue;
    } else if (mode == PMP_NA4) {
      low = cpu.pmpaddr[i] << 2;
      high = low + 4;
    } else {
      /* Bit54 sentinel bounds the trailing-one count and excludes ctz(0). */
      unsigned ones = __builtin_ctzll(~cpu.pmpaddr[i] | (UINT64_C(1) << 54));
      low = (cpu.pmpaddr[i] & ~((UINT64_C(1) << ones) - 1)) << 2;
      high = low + (UINT64_C(1) << (ones + 3));
    }
    if (addr >= high || end <= low) continue;
    /* 最先匹配任何字节的 entry 决定结果；未锁 M 也不能越过部分匹配。 */
    if (addr < low || end > high) return false;
    if (privilege == PRV_M && !(cfg & PMP_L)) return true;
    unsigned permission = type == MEM_TYPE_IFETCH ? PMP_X
                          : type == MEM_TYPE_READ ? PMP_R
                                                  : PMP_W;
    return (cfg & permission) != 0;
  }
  return privilege == PRV_M;
}
