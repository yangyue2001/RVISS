#include "mmu.h"

#include <cpu/cpu.h>
#include <memory/mmu.h>
#include <memory/paddr.h>

/*
 * 运行时按 satp.MODE 选择 Sv39、Sv48 或 Sv57。三级都是 9 位索引。
 * 页表项是 S 模式的物理访问：检查 PMP，并且必须落在 RAM。失败按原访问
 * 报告 access fault，而不是页表自己的 load/store fault。
 */

_Static_assert(RISCV_SV39_LEVELS* RISCV_VPN_BITS + RISCV_PAGE_SHIFT ==
                   RISCV_SV39_VA_BITS,
               "Sv39 width");
_Static_assert(RISCV_SV48_LEVELS* RISCV_VPN_BITS + RISCV_PAGE_SHIFT ==
                   RISCV_SV48_VA_BITS,
               "Sv48 width");
_Static_assert(RISCV_SV57_LEVELS* RISCV_VPN_BITS + RISCV_PAGE_SHIFT ==
                   RISCV_SV57_VA_BITS,
               "Sv57 width");
_Static_assert(RISCV_PPN_BITS + RISCV_PAGE_SHIFT == RISCV_PA_BITS, "PA width");

bool isa_pa_format_ok(paddr_t addr, word_t len) {
  Assert(len != 0, "非法长度");

  const paddr_t limit = UINT64_C(1) << RISCV_PA_BITS;
  return addr < limit && len <= limit - addr;
}

static inline int paging_levels(reg_t satp) {
  reg_t mode = satp & SATP64_MODE;
  int level = 0;
  if (satp_mode_is_sv39(mode)) {
    level = RISCV_SV39_LEVELS;
  }
  if (satp_mode_is_sv48(mode)) {
    level = RISCV_SV48_LEVELS;
  }
  if (satp_mode_is_sv57(mode)) {
    level = RISCV_SV57_LEVELS;
  }
  return level;
}

static bool read_pte(paddr_t pte_addr, word_t* value) {
  MemRequest request = {.addr = pte_addr,
                        .len = RISCV_PTE_BYTES,
                        .type = kMemPteRead,
                        .data = 0,
                        .privilege = PRV_S};
  MemResult result = paddr_access(&request);
  if (result.exception) return false;
  *value = result.data;
  return true;
}

static word_t pte_ppn(word_t pte) {
  return (pte & PTE_PPN) >> RISCV_PTE_PPN_SHIFT;
}

MemResult riscv_mmu_translate(MmuRequest* req) {
  Assert(req != NULL, "riscv_mmu_translate 缺少请求");
  int levels = paging_levels(cpu.satp);

  const word_t ppn_mask = (UINT64_C(1) << RISCV_PPN_BITS) - 1;
  const word_t vpn_mask = (UINT64_C(1) << RISCV_VPN_BITS) - 1;
  paddr_t table = (cpu.satp & ppn_mask) << RISCV_PAGE_SHIFT;

  for (int level = levels - 1; level >= 0; level--) {
    unsigned shift = RISCV_PAGE_SHIFT + (unsigned)level * RISCV_VPN_BITS;
    word_t vpn = (req->vaddr >> shift) & vpn_mask;
    paddr_t pte_addr = table + vpn * RISCV_PTE_BYTES;
    word_t pte = 0;
    if (!read_pte(pte_addr, &pte)) {
      return mem_result_fault(mem_access_fault(req->type), req->vaddr);
    }

    bool reserved = (pte & PTE_V) == 0 ||
                    ((pte & PTE_R) == 0 && (pte & PTE_W) != 0) ||
                    (pte & (PTE_RESERVED | PTE_PBMT | PTE_N)) != 0;
    if (reserved)
      return mem_result_fault(mem_page_fault(req->type), req->vaddr);

    word_t ppn = pte_ppn(pte);
    // 是否是leaf pte 如果是non leaf 检查是否是合格的non leaf pte
    if ((pte & (PTE_R | PTE_X)) == 0) {
      if (level == 0 || (pte & (PTE_U | PTE_A | PTE_D)) != 0) {
        return mem_result_fault(mem_page_fault(req->type), req->vaddr);
      }
      table = ppn << RISCV_PAGE_SHIFT;
    } else {
      // 防止用户态访问非用户page，  地址翻译， 不是s态就是u态，
      // 防止s态在u取指令 sum 控制 s态load/store 用户page
      bool user_page = (pte & PTE_U) != 0;
      if (req->privilege == PRV_U) {
        if (!user_page)
          return mem_result_fault(mem_page_fault(req->type), req->vaddr);
      } else if (user_page && (req->type == kMemIFetch || !req->sum)) {
        return mem_result_fault(mem_page_fault(req->type), req->vaddr);
      }

      // 权限检查
      bool permitted = false;
      if (req->type == kMemIFetch)
        permitted = (pte & PTE_X) != 0;
      else if (req->type == kMemRead)
        permitted = (pte & PTE_R) != 0 || (req->mxr && (pte & PTE_X) != 0);
      else
        permitted = (pte & PTE_W) != 0;

      if (!permitted)
        return mem_result_fault(mem_page_fault(req->type), req->vaddr);

      word_t dropped = (word_t)level * RISCV_VPN_BITS;
      word_t low_mask = dropped == 0 ? 0 : (UINT64_C(1) << dropped) - 1;
      if ((ppn & low_mask) != 0) {
        return mem_result_fault(mem_page_fault(req->type), req->vaddr);
      }

      /* Svadu 才硬件置 A/D。未实现时，缺 A 或 store 缺 D 报 page fault，不改
       * PTE。 */
      word_t needed = PTE_A | (req->type == kMemWrite ? PTE_D : 0);
      if ((pte & needed) != needed) {
        return mem_result_fault(mem_page_fault(req->type), req->vaddr);
      }

      word_t page_off = req->vaddr & (RISCV_PAGE_SIZE - 1);
      paddr_t translated =
          ((ppn | ((req->vaddr >> RISCV_PAGE_SHIFT) & low_mask))
           << RISCV_PAGE_SHIFT) |
          page_off;
      return mem_result_data(translated);
    }
  }
  return mem_result_fault(mem_page_fault(req->type), req->vaddr);
}
