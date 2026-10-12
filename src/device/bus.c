#include <common.h>
#include <device/device.h>
#include <util/log.h>

/*
 * 设备框架的核心（不含任何具体设备）：
 *   ① MMIO 映射表 + 分发
 *   ② 设备节拍注册表（引擎只调 devices_tick）
 *
 * 映射时的三类错误全部当场拒绝（对应 QEMU 在地址空间树上插入时的检查）：
 *   - 区域大小非法 / 地址溢出
 *   - 压在 RAM 上（否则 RAM 会挡住设备，静默失效）
 *   - 与已映射设备重叠（否则后注册者永远收不到访问）
 */

#define MAX_MMIO_REGIONS 16
#define MAX_TICK_HANDLERS 8

typedef struct {
  paddr_t base;
  paddr_t size;
  MmioRegion *region;
} MapEntry;

static MapEntry maps[MAX_MMIO_REGIONS];
static int nr_maps;

static paddr_t bus_ram_base;
static size_t bus_ram_size;

static struct {
  DeviceTickFn fn;
  void *opaque;
} tick_handlers[MAX_TICK_HANDLERS];
static int nr_tick_handlers;
static struct { DeviceDeadlineFn fn; void *opaque; } deadline_handlers[MAX_TICK_HANDLERS];
static int nr_deadline_handlers;
static word_t (*time_reader)(void *);
static void *time_opaque;

void device_set_time_reader(word_t (*reader)(void *), void *opaque) {
  time_reader=reader; time_opaque=opaque;
}

word_t device_time(void) {
  return time_reader ? time_reader(time_opaque) : 0;
}

void bus_init(paddr_t ram_base, size_t ram_size) {
  nr_maps = 0;
  device_set_time_reader(NULL,NULL);
  nr_tick_handlers = 0;
  nr_deadline_handlers = 0;
  memset(maps, 0, sizeof(maps));
  memset(tick_handlers, 0, sizeof(tick_handlers));
  bus_ram_base = ram_base;
  bus_ram_size = ram_size;
}

static bool ranges_overlap(paddr_t a, paddr_t a_size, paddr_t b, paddr_t b_size) {
  return a < b + b_size && b < a + a_size;
}

bool mmio_map(MmioRegion *region, paddr_t base) {
  if (region == NULL || region->name == NULL) {
    Loge("mmio_map: 设备/名字为空");
    return false;
  }
  if (region->size == 0) {
    Loge("mmio_map: 设备 %s 的 MMIO 大小非法（0）", region->name);
    return false;
  }
  if (base + region->size < base) { /* 无符号溢出 */
    Loge("mmio_map: 设备 %s 的映射区间溢出（base=" FMT_PADDR " size=" FMT_PADDR "）",
         region->name, base, region->size);
    return false;
  }
  if (ranges_overlap(base, region->size, bus_ram_base, (paddr_t)bus_ram_size)) {
    Loge("mmio_map: 设备 %s 映射到 [" FMT_PADDR ", " FMT_PADDR "]，与 RAM ["
         FMT_PADDR ", " FMT_PADDR "] 重叠（RAM 会挡住该设备）",
         region->name, base, base + region->size - 1, bus_ram_base,
         bus_ram_base + bus_ram_size - 1);
    return false;
  }
  for (int i = 0; i < nr_maps; i++) {
    if (ranges_overlap(base, region->size, maps[i].base, maps[i].size)) {
      Loge("mmio_map: 设备 %s [" FMT_PADDR ", " FMT_PADDR "] 与设备 %s ["
           FMT_PADDR ", " FMT_PADDR "] 区间重叠",
           region->name, base, base + region->size - 1, maps[i].region->name,
           maps[i].base, maps[i].base + maps[i].size - 1);
      return false;
    }
  }
  if (nr_maps >= MAX_MMIO_REGIONS) {
    Loge("mmio_map: 设备数量超过上限 %d", MAX_MMIO_REGIONS);
    return false;
  }

  maps[nr_maps++] = (MapEntry){base, region->size, region};
  LogDevice("mmio map: %-9s [" FMT_PADDR ", " FMT_PADDR "]", region->name, base,
            base + region->size - 1);
  return true;
}

static const MapEntry *map_find(paddr_t addr, int len) {
  if (len <= 0) return NULL;
  for (int i = 0; i < nr_maps; i++) {
    const MapEntry *m = &maps[i];
    if (addr >= m->base && addr - m->base < m->size &&
        (paddr_t)len <= m->size - (addr - m->base)) {
      return m;
    }
  }
  return NULL;
}

bool mmio_dispatch_read(paddr_t addr, int len, word_t *data) {
  const MapEntry *m = map_find(addr, len);
  if (m == NULL || m->region->read == NULL) return false;
  if (m->region->accepts && !m->region->accepts(m->region->opaque, addr - m->base, len, false)) return false;
  *data = m->region->read(m->region->opaque, addr - m->base, len);
  return true;
}

bool mmio_dispatch_write(paddr_t addr, int len, word_t data) {
  const MapEntry *m = map_find(addr, len);
  if (m == NULL || m->region->write == NULL) return false;
  if (m->region->accepts && !m->region->accepts(m->region->opaque, addr - m->base, len, true)) return false;
  m->region->write(m->region->opaque, addr - m->base, len, data);
  return true;
}

/* ---------------- 设备节拍 ---------------- */

bool device_register_tick(DeviceTickFn fn, void *opaque) {
  if (fn == NULL) return false;
  if (nr_tick_handlers >= MAX_TICK_HANDLERS) {
    Loge("设备节拍回调超过上限 %d", MAX_TICK_HANDLERS);
    return false;
  }
  tick_handlers[nr_tick_handlers].fn = fn;
  tick_handlers[nr_tick_handlers].opaque = opaque;
  nr_tick_handlers++;
  return true;
}

bool device_register_deadline(DeviceDeadlineFn fn, void *opaque) {
  if (fn == NULL || nr_deadline_handlers >= MAX_TICK_HANDLERS) return false;
  deadline_handlers[nr_deadline_handlers++] = (typeof(deadline_handlers[0])){fn, opaque};
  return true;
}

uint64_t devices_next_deadline(void) {
  uint64_t next = UINT64_MAX;
  for (int i = 0; i < nr_deadline_handlers; i++) {
    uint64_t candidate = deadline_handlers[i].fn(deadline_handlers[i].opaque);
    if (candidate < next) next = candidate;
  }
  return next;
}

void devices_tick(uint64_t logical_ticks) {
  for (int i = 0; i < nr_tick_handlers; i++) {
    tick_handlers[i].fn(tick_handlers[i].opaque, logical_ticks);
  }
}
