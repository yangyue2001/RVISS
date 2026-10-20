#ifndef RVISS_MEMORY_PADDR_H
#define RVISS_MEMORY_PADDR_H

#include <common.h>
#include <memory/vaddr.h>

/*
 * 物理地址空间：RAM + MMIO。
 * paddr_access 是客户机物理访问：req->addr 就是物理地址。
 * 先查找覆盖这次访问的 MemRegion，不存在就是 access fault。
 * 页表访问还要求 object.name 是 RAM。然后做 PMP，再调用区域的 read/write。
 * 宿主查看内存走 host_mem_*，不在这里。
 */

extern paddr_t mem_base;
extern size_t mem_size;

/* 分配 RAM（host_mmap_anon），失败返回 false */
bool mem_init(paddr_t base, size_t size);
void mem_shutdown(void);

/* Image/ABI ranges may exceed INT_MAX; instruction accesses still use 1..8. */
static inline bool mem_in_ram(paddr_t addr, size_t len) {
  return len > 0 && addr >= mem_base && addr - mem_base < mem_size &&
         (paddr_t)len <= mem_size - (addr - mem_base);
}

/* addr 必须已确认在 RAM 内 */
uint8_t* mem_host_ptr(paddr_t addr);

/* bus_init 用它把 mem_init 的 RAM 登记成普通区域。范围不一致则不登记。 */
bool mem_install_ram(paddr_t base, size_t size);

/* 客户机物理访问。addr 是物理地址。成功的读把数据放进返回值。 */
MemResult paddr_access(MemRequest* req);
bool isa_pa_format_ok(paddr_t addr, word_t len);
bool isa_pmp_permit(paddr_t addr, word_t len, MemAccessType type, Priv privilege);

#endif /* RVISS_MEMORY_PADDR_H */
