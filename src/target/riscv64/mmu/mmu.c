#include <common.h>
#include <memory/vaddr.h>
#include <util/log.h>

#include "csr.h"
#include "cpu_state.h"

/*
 * 地址翻译。
 *
 * 骨架阶段：透明映射（等价 satp.MODE = Bare），先把整条访存链路跑通。
 * TODO(M3)：实现 Sv39/Sv48
 *   - 页表遍历（3/4 级），PTE 的 V/R/W/X/U/G/A/D/PPN 检查
 *   - 权限：SUM / MXR / U 位 / 页级权限
 *   - 缺页/访问异常（EX_IPF/EX_LPF/EX_SPF）与 A/D 位更新
 *   - TLB（mmu/tlb.c）与 sfence.vma 失效
 */

bool isa_mmu_enabled(void) {
  /* satp.MODE：0 = Bare（不翻译），8 = Sv39，9 = Sv48 */
  return BITS(cpu.satp, 63, 60) != 0;
}

paddr_t isa_mmu_translate(vaddr_t addr, int len, int type) {
  (void)len;
  (void)type;

  if (!isa_mmu_enabled()) {
    return (paddr_t)addr;
  }

  /* 未实现：直接 panic，避免"看起来能跑其实语义错" */
  panic("Sv39/Sv48 翻译尚未实现（见 src/target/riscv64/mmu/mmu.c 的 TODO）");
}
