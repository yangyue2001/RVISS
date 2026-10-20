#include <common.h>
#include <device/device.h>
#include <device/ram.h>
#include <host/host.h>
#include <memory/paddr.h>
#include <string.h>
#include <util/log.h>

/*
 * 宿主查看或改写客户机物理内存。
 * 不做地址翻译，不做 PMP，也不把失败变成客户机异常。
 * 客户机取指、load/store 和页表遍历必须走 paddr_access。
 */

static bool region_is_ram(const MemRegion* region) {
  const char* name = region->object.name;
  return name != NULL && strcmp(name, RAM_OBJECT_NAME) == 0;
}

static bool try_len_ok(int len) {
  return len == 1 || len == 2 || len == 4 || len == 8;
}

bool host_mem_try_read(paddr_t addr, int len, word_t* data, bool* mmio) {
  if (mmio != NULL) *mmio = false;
  if (!try_len_ok(len) || data == NULL) return false;
  const MemRegion* region = mem_region_find(addr, (word_t)len);
  if (region == NULL || region->object.read == NULL) return false;
  if (!region->object.read(region->object.ctx, addr - region->base, len,
                           data)) {
    return false;
  }
  if (mmio != NULL) *mmio = !region_is_ram(region);
  return true;
}

word_t host_mem_read(paddr_t addr, int len) {
  word_t data = 0;
  if (!host_mem_try_read(addr, len, &data, NULL)) {
    LogMem("host read unmapped " FMT_PADDR " (len=%d)", addr, len);
  }
  return data;
}

bool host_mem_try_write(paddr_t addr, int len, word_t data, bool* mmio) {
  if (mmio != NULL) *mmio = false;
  if (!try_len_ok(len)) return false;
  const MemRegion* region = mem_region_find(addr, (word_t)len);
  if (region == NULL || region->object.write == NULL) return false;
  if (!region->object.write(region->object.ctx, addr - region->base, len,
                            data)) {
    return false;
  }
  if (mmio != NULL) *mmio = !region_is_ram(region);
  return true;
}

bool host_mem_write(paddr_t addr, int len, word_t data) {
  return host_mem_try_write(addr, len, data, NULL);
}
