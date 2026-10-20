#include <common.h>
#include <device/device.h>
#include <util/log.h>

/*
 * MMIO 映射。设备节拍不在这里登记：板卡保存已注册实例，
 * machine_advance / machine_update 只刷新这些实例。
 * bus_init 清映射，并调用板卡登记的复位回调。
 */

#define MAX_MMIO_REGIONS 16
#define MAX_BUS_RESETS 8

typedef struct {
  paddr_t base;
  MemObject object;
} MapEntry;

static MapEntry maps[MAX_MMIO_REGIONS];
static int nr_maps;

static paddr_t bus_ram_base;
static size_t bus_ram_size;

static void (*bus_resets[MAX_BUS_RESETS])(void);
static int nr_bus_resets;

static word_t (*time_reader)(void*);
static void* time_opaque;

void device_set_time_reader(word_t (*reader)(void*), void* opaque) {
  time_reader = reader;
  time_opaque = opaque;
}

word_t device_time(void) { return time_reader ? time_reader(time_opaque) : 0; }

bool device_add_bus_reset(void (*fn)(void)) {
  if (fn == NULL) return false;
  for (int i = 0; i < nr_bus_resets; i++) {
    if (bus_resets[i] == fn) return true;
  }
  if (nr_bus_resets >= MAX_BUS_RESETS) {
    Loge("总线复位回调超过上限 %d", MAX_BUS_RESETS);
    return false;
  }
  bus_resets[nr_bus_resets++] = fn;
  return true;
}

void bus_init(paddr_t ram_base, size_t ram_size) {
  for (int i = 0; i < nr_bus_resets; i++) bus_resets[i]();
  nr_bus_resets = 0;
  nr_maps = 0;
  device_set_time_reader(NULL, NULL);
  memset(maps, 0, sizeof(maps));
  bus_ram_base = ram_base;
  bus_ram_size = ram_size;
}

static bool ranges_overlap(paddr_t a, paddr_t a_size, paddr_t b,
                           paddr_t b_size) {
  return a < b + b_size && b < a + a_size;
}

bool mmio_map(const MemObject* object, paddr_t base) {
  if (object == NULL || object->name == NULL) {
    Loge("mmio_map: 设备/名字为空");
    return false;
  }
  if (object->size == 0) {
    Loge("mmio_map: 设备 %s 的 MMIO 大小非法（0）", object->name);
    return false;
  }
  if (base + object->size < base) {
    Loge("mmio_map: 设备 %s 的映射区间溢出（base=" FMT_PADDR " size=" FMT_PADDR
         "）",
         object->name, base, object->size);
    return false;
  }
  if (ranges_overlap(base, object->size, bus_ram_base, (paddr_t)bus_ram_size)) {
    Loge("mmio_map: 设备 %s 映射到 [" FMT_PADDR ", " FMT_PADDR
         "]，与 RAM [" FMT_PADDR ", " FMT_PADDR "] 重叠（RAM 会挡住该设备）",
         object->name, base, base + object->size - 1, bus_ram_base,
         bus_ram_base + bus_ram_size - 1);
    return false;
  }
  for (int i = 0; i < nr_maps; i++) {
    if (ranges_overlap(base, object->size, maps[i].base, maps[i].object.size)) {
      Loge("mmio_map: 设备 %s [" FMT_PADDR ", " FMT_PADDR "] 与设备 %s [" FMT_PADDR
           ", " FMT_PADDR "] 区间重叠",
           object->name, base, base + object->size - 1, maps[i].object.name,
           maps[i].base, maps[i].base + maps[i].object.size - 1);
      return false;
    }
  }
  if (nr_maps >= MAX_MMIO_REGIONS) {
    Loge("mmio_map: 设备数量超过上限 %d", MAX_MMIO_REGIONS);
    return false;
  }

  maps[nr_maps++] = (MapEntry){.base = base, .object = *object};
  LogDevice("mmio map: %-9s [" FMT_PADDR ", " FMT_PADDR "]", object->name, base,
            base + object->size - 1);
  return true;
}

static const MapEntry* map_find(paddr_t addr, int len) {
  if (len <= 0) return NULL;
  for (int i = 0; i < nr_maps; i++) {
    const MapEntry* mapped = &maps[i];
    if (addr >= mapped->base && addr - mapped->base < mapped->object.size &&
        (paddr_t)len <= mapped->object.size - (addr - mapped->base)) {
      return mapped;
    }
  }
  return NULL;
}

bool mmio_dispatch_read(paddr_t addr, int len, word_t* data) {
  const MapEntry* mapped = map_find(addr, len);
  if (mapped == NULL || mapped->object.read == NULL) return false;
  return mapped->object.read(mapped->object.ctx, addr - mapped->base, len, data);
}

bool mmio_dispatch_write(paddr_t addr, int len, word_t data) {
  const MapEntry* mapped = map_find(addr, len);
  if (mapped == NULL || mapped->object.write == NULL) return false;
  return mapped->object.write(mapped->object.ctx, addr - mapped->base, len, data);
}
