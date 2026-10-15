#include "memory_model.h"

#include <string.h>

static uint8_t width_bit(int len) {
  switch (len) {
    case 1: return BM_WIDTH_1;
    case 2: return BM_WIDTH_2;
    case 4: return BM_WIDTH_4;
    case 8: return BM_WIDTH_8;
    default: return 0;
  }
}

static bool contains(const MemRegion *region, paddr_t addr, int len) {
  if (len <= 0 || addr < region->base) return false;
  const paddr_t offset = addr - region->base;
  return offset < region->size && (paddr_t)len <= region->size - offset;
}

static bool overlaps(const MemRegion *a, const MemRegion *b) {
  if (a->base > UINT64_MAX - a->size ||
      b->base > UINT64_MAX - b->size)
    return true;
  return a->base < b->base + b->size && b->base < a->base + a->size;
}

static MemResult pma_check(const MemRegion *region, paddr_t addr, int len,
                           MemAccessType type) {
  const uint8_t width = width_bit(len);
  if (width == 0 || !(region->pma.supported_widths & width))
    return MEM_RESULT_UNSUPPORTED;
  if ((addr % (paddr_t)len) != 0 &&
      region->pma.misaligned == PMA_MISALIGNED_TRAP)
    return MEM_RESULT_UNSUPPORTED;

  if (type == MEM_ACCESS_IFETCH && !region->pma.executable)
    return MEM_RESULT_UNSUPPORTED;
  if (type == MEM_ACCESS_READ && !region->pma.readable)
    return MEM_RESULT_UNSUPPORTED;
  if (type == MEM_ACCESS_WRITE && !region->pma.writable)
    return MEM_RESULT_UNSUPPORTED;
  return MEM_RESULT_OK;
}

void address_space_init(AddressSpace *space) {
  memset(space, 0, sizeof(*space));
}

bool address_space_add_region(AddressSpace *space, const MemRegion *region) {
  if (space == NULL || region == NULL || region->name == NULL ||
      region->size == 0 || region->object == NULL || region->object->ops == NULL)
    return false;
  if (region->base > UINT64_MAX - region->size ||
      space->region_count >= BM_MAX_REGIONS)
    return false;
  for (size_t i = 0; i < space->region_count; i++) {
    if (overlaps(region, &space->regions[i])) return false;
  }
  space->regions[space->region_count++] = *region;
  return true;
}

MemRegion *address_space_find(AddressSpace *space, paddr_t addr, int len) {
  if (space == NULL) return NULL;
  for (size_t i = 0; i < space->region_count; i++) {
    if (contains(&space->regions[i], addr, len)) return &space->regions[i];
  }
  return NULL;
}

MemResult address_space_read(AddressSpace *space, paddr_t addr, int len,
                             MemAccessType type, word_t *data) {
  if (data == NULL) return MEM_RESULT_INVALID;
  MemRegion *region = address_space_find(space, addr, len);
  if (region == NULL) return MEM_RESULT_UNMAPPED;
  MemResult result = pma_check(region, addr, len, type);
  if (result != MEM_RESULT_OK) return result;
  if (region->object->ops->read == NULL) return MEM_RESULT_UNSUPPORTED;
  return region->object->ops->read(region->object->opaque,
                                   addr - region->base, len, data);
}

MemResult address_space_write(AddressSpace *space, paddr_t addr, int len,
                              word_t data) {
  MemRegion *region = address_space_find(space, addr, len);
  if (region == NULL) return MEM_RESULT_UNMAPPED;
  MemResult result = pma_check(region, addr, len, MEM_ACCESS_WRITE);
  if (result != MEM_RESULT_OK) return result;
  if (region->object->ops->write == NULL) return MEM_RESULT_UNSUPPORTED;
  return region->object->ops->write(region->object->opaque,
                                    addr - region->base, len, data);
}

uint8_t *address_space_host_ptr(AddressSpace *space, paddr_t addr, int len) {
  MemRegion *region = address_space_find(space, addr, len);
  if (region == NULL || region->object->ops->host_ptr == NULL ||
      pma_check(region, addr, len, MEM_ACCESS_READ) != MEM_RESULT_OK)
    return NULL;
  return region->object->ops->host_ptr(region->object->opaque,
                                       addr - region->base, len);
}

const char *mem_result_name(MemResult result) {
  switch (result) {
    case MEM_RESULT_OK: return "ok";
    case MEM_RESULT_UNMAPPED: return "unmapped";
    case MEM_RESULT_UNSUPPORTED: return "unsupported";
    case MEM_RESULT_INVALID: return "invalid";
    case MEM_RESULT_HOST_ERROR: return "host-error";
  }
  return "unknown";
}

const char *mem_kind_name(MemKind kind) {
  switch (kind) {
    case MEM_KIND_RAM: return "RAM";
    case MEM_KIND_ROM: return "ROM";
    case MEM_KIND_UART: return "UART";
  }
  return "UNKNOWN";
}

void address_space_dump(const AddressSpace *space, FILE *out) {
  if (space == NULL || out == NULL) return;
  for (size_t i = 0; i < space->region_count; i++) {
    const MemRegion *r = &space->regions[i];
    fprintf(out, "  %-12s [%#018llx, %#018llx) -> %s (%s)\n", r->name,
            (unsigned long long)r->base,
            (unsigned long long)(r->base + r->size), r->object->name,
            mem_kind_name(r->object->kind));
  }
}
