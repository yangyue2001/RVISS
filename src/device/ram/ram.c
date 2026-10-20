#include <common.h>
#include <device/device.h>
#include <device/ram.h>
#include <host/host.h>
#include <memory/paddr.h>
#include <util/log.h>

/*
 * RAM 是一块普通 MemObject。基址和长度由板卡在登记时给出，不放进对象里。
 */

enum { BITS_PER_BYTE = 8 };

paddr_t mem_base;
size_t mem_size;

static uint8_t* ram;

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

uint8_t* mem_host_ptr(paddr_t addr) {
  Assert(mem_in_ram(addr, 1), "地址 " FMT_PADDR " 不在 RAM 内", addr);
  return ram + (addr - mem_base);
}

static bool ram_read(void* ctx, paddr_t offset, int len, word_t* data) {
  (void)ctx;
  if (ram == NULL || data == NULL || len <= 0 ||
      (size_t)offset + (size_t)len > mem_size) {
    return false;
  }
  word_t value = 0;
  const uint8_t* bytes = ram + offset;
  for (int i = 0; i < len; i++) {
    value |= (word_t)bytes[i] << (BITS_PER_BYTE * i);
  }
  *data = value;
  return true;
}

static bool ram_write(void* ctx, paddr_t offset, int len, word_t data) {
  (void)ctx;
  if (ram == NULL || len <= 0 || (size_t)offset + (size_t)len > mem_size) {
    return false;
  }
  uint8_t* bytes = ram + offset;
  for (int i = 0; i < len; i++) {
    bytes[i] = (uint8_t)(data >> (BITS_PER_BYTE * i));
  }
  return true;
}

bool mem_install_ram(paddr_t base, size_t size) {
  if (ram == NULL || size == 0 || base != mem_base || size != mem_size)
    return false;
  MemObject object = {
      .name = RAM_OBJECT_NAME, .read = ram_read, .write = ram_write};
  return mem_region_add(base, (paddr_t)size, &object);
}
