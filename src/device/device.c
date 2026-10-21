#include <common.h>
#include <device/device.h>
#include <memory/paddr.h>
#include <util/log.h>

/*
 * 物理区域表。RAM 和外设都登记在这里。
 * 设备节拍不在这里。板卡用同一个逻辑时间调用 machine_advance。
 */

#define MAX_MEM_REGIONS 16
#define MAX_BUS_RESETS 8

static MemRegion regions[MAX_MEM_REGIONS];
static int nr_regions;

static bool ranges_overlap(paddr_t a, paddr_t a_size, paddr_t b,
                           paddr_t b_size) {
  return a_size > 0 && b_size > 0 && a < b + b_size && b < a + a_size;
}

bool mem_region_add(paddr_t base, paddr_t size, const MemObject* object) {
  Assert(object != NULL && object->name != NULL,
         "mem_region_add: 设备或名字为空");
  Assert(size != 0 && base + size >= base,
         "mem_region_add: %s 的区间非法（base=" FMT_PADDR " size=" FMT_PADDR
         "）",
         object->name, base, size);
  for (int i = 0; i < nr_regions; i++) {
    Assert(!ranges_overlap(base, size, regions[i].base, regions[i].size),
           "mem_region_add: %s [" FMT_PADDR ", " FMT_PADDR "] 与 %s [" FMT_PADDR
           ", " FMT_PADDR "] 重叠",
           object->name, base, base + size - 1, regions[i].object.name,
           regions[i].base, regions[i].base + regions[i].size - 1);
  }
  Assert(nr_regions < MAX_MEM_REGIONS, "mem_region_add: 区域数量超过上限 %d",
         MAX_MEM_REGIONS);
  regions[nr_regions++] = (MemRegion){
      .base = base,
      .size = size,
      .object = *object,
  };

  LogDevice("region %-9s [" FMT_PADDR ", " FMT_PADDR "]", object->name, base,
            base + size - 1);
  return true;
}

const MemRegion* mem_region_find(paddr_t addr, word_t len) {
  if (len == 0 || addr > UINT64_MAX - (len - 1)) return NULL;
  for (int i = 0; i < nr_regions; i++) {
    const MemRegion* region = &regions[i];
    if (addr >= region->base && addr - region->base < region->size &&
        len <= region->size - (addr - region->base)) {
      return region;
    }
  }
  return NULL;
}
