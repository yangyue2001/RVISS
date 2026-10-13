#ifndef RVISS_MEMORY_PADDR_H
#define RVISS_MEMORY_PADDR_H

#include <common.h>

/*
 * 物理地址空间：RAM + MMIO 分发。
 * 这一层不认识 RISC-V，只知道"有一段连续内存 + 一段段 MMIO 区域"。
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

/* checked 接口不注入 CPU 异常。失败不写 data；成功可标明 RAM/MMIO。 */
bool paddr_try_read(paddr_t addr, int len, word_t* data, bool* mmio);
bool paddr_try_write(paddr_t addr, int len, word_t data, bool* mmio);
/* legacy 调试读取：无设备应答时按 0 处理；客户机必须走 checked 接口。 */
word_t paddr_read(paddr_t addr, int len);
/* 写：返回 true 表示有设备/内存应答 */
bool paddr_write(paddr_t addr, int len, word_t data);

#endif /* RVISS_MEMORY_PADDR_H */
