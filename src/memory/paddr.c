#include <common.h>
#include <memory/paddr.h>
#include <device/device.h>
#include <host/host.h>
#include <util/log.h>

/*
 * 物理地址空间：RAM + MMIO。
 * 顺序：先查 RAM（快路径），未命中再问设备总线。
 */

paddr_t mem_base;
size_t  mem_size;

static uint8_t *ram;

bool mem_init(paddr_t base, size_t size) {
  if (size == 0 || size > UINT64_MAX - base) return false;
  ram = host_mmap_anon(size);
  if (ram == NULL) {
    Loge("内存分配失败：%zu MiB", size >> 20);
    return false;
  }
  mem_base = base;
  mem_size = size;
  memset(ram, 0, size);
  Log("memory: %zu MiB @ " FMT_PADDR, size >> 20, base);
  return true;
}

void mem_shutdown(void) {
  if (ram != NULL) {
    host_munmap(ram, mem_size);
    ram = NULL;
  }
}

uint8_t *mem_host_ptr(paddr_t addr) {
  Assert(mem_in_ram(addr, 1), "地址 " FMT_PADDR " 不在 RAM 内", addr);
  return ram + (addr - mem_base);
}

static word_t read_le(const uint8_t *p, int len) {
  word_t v = 0;
  for (int i = 0; i < len; i++) {
    v |= (word_t)p[i] << (8 * i);
  }
  return v;
}

static void write_le(uint8_t *p, int len, word_t v) {
  for (int i = 0; i < len; i++) {
    p[i] = (uint8_t)(v >> (8 * i));
  }
}

static bool valid_access(paddr_t addr, int len) {
  return (len == 1 || len == 2 || len == 4 || len == 8) &&
         addr <= UINT64_MAX - (paddr_t)(len - 1);
}

bool paddr_try_read(paddr_t addr, int len, word_t *data, bool *mmio) {
  if (mmio != NULL) *mmio = false;
  if (!valid_access(addr, len)) return false;
  if (likely(mem_in_ram(addr, len))) {
    *data = read_le(ram + (addr - mem_base), len);
    return true;
  }

  word_t value = 0;
  if (mmio_dispatch_read(addr, len, &value)) {
    *data = value;
    if (mmio != NULL) *mmio = true;
    LogMem("mmio read  " FMT_PADDR " len=%d -> 0x%lx", addr, len, value);
    return true;
  }
  return false;
}

word_t paddr_read(paddr_t addr, int len) {
  word_t data = 0;
  if (!paddr_try_read(addr, len, &data, NULL)) {
    LogMem("debug read unmapped " FMT_PADDR " (len=%d)", addr, len);
  }
  return data;
}

bool paddr_try_write(paddr_t addr, int len, word_t data, bool *mmio) {
  if (mmio != NULL) *mmio = false;
  if (!valid_access(addr, len)) return false;
  if (likely(mem_in_ram(addr, len))) {
    write_le(ram + (addr - mem_base), len, data);
    return true;
  }

  if (mmio_dispatch_write(addr, len, data)) {
    if (mmio != NULL) *mmio = true;
    LogMem("mmio write " FMT_PADDR " len=%d <- 0x%lx", addr, len, data);
    return true;
  }

  return false;
}

bool paddr_write(paddr_t addr, int len, word_t data) {
  return paddr_try_write(addr, len, data, NULL);
}
