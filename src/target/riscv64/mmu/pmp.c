#include <common.h>
#include <memory/vaddr.h>

/*
 * PMP（物理内存保护）。
 * TODO(M4)：16 个 PMP entry（OFF/TOR/NA4/NAPOT），pmpcfg0-15 + pmpaddr0-15，
 * 在物理访问路径（paddr.c 之前）检查。README 里承诺过，属于 M4 范围。
 */

bool isa_pmp_check_permission(paddr_t addr, int len, int type);
bool isa_pmp_check_permission(paddr_t addr, int len, int type) {
  (void)addr;
  (void)len;
  (void)type;
  return true; /* 骨架：不设防 */
}
