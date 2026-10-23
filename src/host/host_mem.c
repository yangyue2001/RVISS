#include <common.h>
#include <device/device.h>
#include <host/host.h>
#include <memory/paddr.h>
#include <util/log.h>

/*
 * 宿主查看客户机物理内存。
 * 不做地址翻译，不做 PMP，也不把失败变成客户机异常。
 * 客户机取指、load/store 和页表遍历必须走 paddr_access。
 */

static bool len_ok(int len) {
  return len == 1 || len == 2 || len == 4 || len == 8;
}

word_t host_mem_read(paddr_t addr, int len) {
  word_t data = 0;
  if (!len_ok(len)) {
    LogMem("host read unmapped " FMT_PADDR " (len=%d)", addr, len);
    return 0;
  }
  const MemRegion* region = mem_region_find(addr, (word_t)len);
  if (region == NULL || region->object.read == NULL ||
      !region->object.read(region->object.ctx, addr - region->base, len,
                           &data)) {
    LogMem("host read unmapped " FMT_PADDR " (len=%d)", addr, len);
    return 0;
  }
  return data;
}
