#ifndef RVISS_PROTOTYPE_BOARD_RUNTIME_H
#define RVISS_PROTOTYPE_BOARD_RUNTIME_H

#include "board_config.h"

typedef void (*MemObjectDestroy)(void *opaque);

typedef struct {
  AddressSpace space;
  MemObject objects[BM_MAX_OBJECTS];
  void *states[BM_MAX_OBJECTS];
  MemObjectDestroy destroy[BM_MAX_OBJECTS];
  size_t object_count;
} BoardRuntime;

bool board_runtime_build(const BoardConfig *config, BoardRuntime *runtime,
                         char *error, size_t error_size);
void board_runtime_destroy(BoardRuntime *runtime);

#endif /* RVISS_PROTOTYPE_BOARD_RUNTIME_H */
