#include <common.h>
#include <memory/vaddr.h>

/*
 * 地址翻译缓存（TLB）。
 * TODO(M3)：页表遍历很贵，实现 Sv39 的同时加一个直接映射 TLB：
 *   - 表项：vpn, asid, pte 权限快照
 *   - 失效：sfence.vma / 写 satp / 写页表后
 * 骨架阶段 vaddr.c 每次都调 isa_mmu_translate，先不加缓存。
 */

void isa_mmu_flush_tlb(void) {
  /* 当前无缓存，空实现 */
}
