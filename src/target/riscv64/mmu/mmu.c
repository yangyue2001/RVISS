#include "mmu.h"

#include <cpu/cpu.h>
#include <cpu/interrupt.h>
#include <memory/paddr.h>

#include "pmp.h"

#define PPN_MASK ((UINT64_C(1) << 44) - 1)
enum {
  PTE_V = 1,
  PTE_R = 2,
  PTE_W = 4,
  PTE_X = 8,
  PTE_U = 16,
  PTE_A = 64,
  PTE_D = 128
};

void riscv_mmu_set_sv39(bool supported) {
#ifndef CONFIG_RV_SV39
  Assert(!supported, "Sv39 unavailable in this build");
#endif
  cpu.sv39_supported = supported;
}

void riscv_mmu_reset(void) {
#ifdef CONFIG_RV_SV39
  riscv_mmu_set_sv39(true);
#else
  riscv_mmu_set_sv39(false);
#endif
}

word_t riscv_satp_value(word_t value) {
  unsigned mode = value >> 60;
  if (mode != 0 && (mode != 8 || !cpu.sv39_supported)) return cpu.satp;
  return cpu.sv39_supported ? value & ((UINT64_C(0xf) << 60) | PPN_MASK) : 0;
}

static bool fault(MemAccess* a, bool page) {
  a->status = MEM_FAULT;
  a->cause = a->type == MEM_TYPE_IFETCH ? (page ? EX_IPF : EX_IAF)
             : a->type == MEM_TYPE_READ ? (page ? EX_LPF : EX_LAF)
                                        : (page ? EX_SPF : EX_SAF);
  a->tval = a->vaddr;
  return false;
}

/* PTE PMA is RAM-only, with S-mode PMP permissions regardless of origin/MPRV.
 * 8-byte read/check/update is indivisible in this single-hart execution model:
 * no host thread or DMA writes guest RAM while the event executes. */
static bool pte_access(paddr_t addr, int type, word_t* value) {
  if (!mem_in_ram(addr, 8) || !riscv_pmp_check(addr, 8, type, PRV_S))
    return false;
  return type == MEM_TYPE_READ ? paddr_try_read(addr, 8, value, NULL)
                               : paddr_try_write(addr, 8, *value, NULL);
}

bool riscv_mmu_translate(MemAccess* a, uint8_t privilege) {
  unsigned mode = cpu.satp >> 60;
  if (privilege == PRV_M || mode == 0) {
    a->paddr = a->vaddr;
    a->paddr_valid = true;
    return true;
  }
  if (!cpu.sv39_supported || mode != 8) {
    a->status = MEM_HOST_ERROR;
    return false; /* corrupt/unsupported host state */
  }
  word_t top = a->vaddr >> 38;
  if (top != 0 && top != ((UINT64_C(1) << 26) - 1)) return fault(a, true);
  paddr_t table = (cpu.satp & PPN_MASK) << 12;
  for (int level = 2; level >= 0; level--) {
    paddr_t pte_addr = table + ((a->vaddr >> (12 + 9 * level)) & 511) * 8;
    word_t pte;
    if (!pte_access(pte_addr, MEM_TYPE_READ, &pte)) return fault(a, false);
    if (!(pte & PTE_V) || (!(pte & PTE_R) && (pte & PTE_W)) || (pte >> 54))
      return fault(a, true);
    word_t ppn = (pte >> 10) & PPN_MASK;
    if (!(pte & (PTE_R | PTE_X))) {
      if (level == 0 || (pte & (PTE_U | PTE_A | PTE_D))) return fault(a, true);
      table = ppn << 12;
      continue;
    }
    if (privilege == PRV_U ? !(pte & PTE_U)
                           : ((pte & PTE_U) && (a->type == MEM_TYPE_IFETCH ||
                                                !(cpu.mstatus & MSTATUS_SUM))))
      return fault(a, true);
    bool permission =
        a->type == MEM_TYPE_IFETCH ? (pte & PTE_X)
        : a->type == MEM_TYPE_READ
            ? ((pte & PTE_R) || ((cpu.mstatus & MSTATUS_MXR) && (pte & PTE_X)))
            : (pte & PTE_W);
    if (!permission) return fault(a, true);
    word_t low_mask = (UINT64_C(1) << (9 * level)) - 1;
    if (ppn & low_mask) return fault(a, true);
    word_t ad = PTE_A | (a->type == MEM_TYPE_WRITE ? PTE_D : 0);
    if ((pte & ad) != ad) {
      pte |= ad;
      if (!pte_access(pte_addr, MEM_TYPE_WRITE, &pte)) return fault(a, false);
    }
    a->paddr =
        ((ppn | ((a->vaddr >> 12) & low_mask)) << 12) | (a->vaddr & 4095);
    a->paddr_valid = true;
    return true;
  }
  return fault(a, true);
}
