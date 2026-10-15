#ifndef RVISS_PROTOTYPE_MEMORY_MODEL_H
#define RVISS_PROTOTYPE_MEMORY_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef uint64_t word_t;
typedef uint64_t paddr_t;

#define BM_MAX_REGIONS 32

#define BM_WIDTH_1 (1u << 0)
#define BM_WIDTH_2 (1u << 1)
#define BM_WIDTH_4 (1u << 2)
#define BM_WIDTH_8 (1u << 3)

typedef enum {
  MEM_KIND_RAM,
  MEM_KIND_ROM,
  MEM_KIND_UART,
} MemKind;

typedef enum {
  PMA_MAIN_MEMORY,
  PMA_IO,
} PmaType;

typedef enum {
  PMA_MISALIGNED_TRAP,
  PMA_MISALIGNED_SPLIT,
} PmaMisalignedPolicy;

typedef enum {
  MEM_ACCESS_IFETCH,
  MEM_ACCESS_READ,
  MEM_ACCESS_WRITE,
} MemAccessType;

typedef enum {
  MEM_RESULT_OK,
  MEM_RESULT_UNMAPPED,
  MEM_RESULT_UNSUPPORTED,
  MEM_RESULT_INVALID,
  MEM_RESULT_HOST_ERROR,
} MemResult;

typedef struct {
  PmaType type;
  bool readable;
  bool writable;
  bool executable;
  uint8_t supported_widths;
  uint8_t atomic_widths;
  PmaMisalignedPolicy misaligned;
  bool idempotent;
  bool cacheable;
} PmaAttributes;

typedef struct MemOps {
  MemResult (*read)(void *opaque, paddr_t offset, int len, word_t *data);
  MemResult (*write)(void *opaque, paddr_t offset, int len, word_t data);
  uint8_t *(*host_ptr)(void *opaque, paddr_t offset, int len);
} MemOps;

typedef struct MemObject {
  const char *name;
  MemKind kind;
  const MemOps *ops;
  void *opaque;
} MemObject;

typedef struct {
  const char *name;
  paddr_t base;
  paddr_t size;
  PmaAttributes pma;
  MemObject *object;
} MemRegion;

typedef struct {
  MemRegion regions[BM_MAX_REGIONS];
  size_t region_count;
} AddressSpace;

void address_space_init(AddressSpace *space);
bool address_space_add_region(AddressSpace *space, const MemRegion *region);
MemRegion *address_space_find(AddressSpace *space, paddr_t addr, int len);

MemResult address_space_read(AddressSpace *space, paddr_t addr, int len,
                             MemAccessType type, word_t *data);
MemResult address_space_write(AddressSpace *space, paddr_t addr, int len,
                              word_t data);
uint8_t *address_space_host_ptr(AddressSpace *space, paddr_t addr, int len);

const char *mem_result_name(MemResult result);
const char *mem_kind_name(MemKind kind);
void address_space_dump(const AddressSpace *space, FILE *out);

#endif /* RVISS_PROTOTYPE_MEMORY_MODEL_H */
