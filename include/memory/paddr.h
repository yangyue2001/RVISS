#ifndef RVISS_MEMORY_PADDR_H
#define RVISS_MEMORY_PADDR_H

#include <common.h>
#include <memory/vaddr.h>

/*
 * 物理地址空间：RAM + MMIO。
 * paddr_try_* 给 loader 和调试口用，不做翻译、PMP 或物理地址宽度检查。
 * paddr_access 是客户机物理访问：req->addr 就是物理地址。先检查地址宽度和 PMP，
 * 再经同一次传输进入 RAM 或 MMIO。二者在这里没有区别。
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

/* 客户机物理访问。addr 是物理地址。成功的读把数据放进返回值。 */
MemResult paddr_access(MemRequest* req);
bool isa_pa_format_ok(paddr_t addr, word_t len);
bool isa_pmp_permit(paddr_t addr, word_t len, MemAccessType type, Priv privilege);

/* checked 接口不注入 CPU 异常。失败不写 data；成功可标明 RAM/MMIO。 */
bool paddr_try_read(paddr_t addr, int len, word_t* data, bool* mmio);
bool paddr_try_write(paddr_t addr, int len, word_t data, bool* mmio);
/* legacy 调试读取：无设备应答时按 0 处理；客户机必须走 checked 接口。 */
word_t paddr_read(paddr_t addr, int len);
/* 写：返回 true 表示有设备/内存应答 */
bool paddr_write(paddr_t addr, int len, word_t data);

#endif /* RVISS_MEMORY_PADDR_H */
