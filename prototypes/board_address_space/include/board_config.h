#ifndef RVISS_PROTOTYPE_BOARD_CONFIG_H
#define RVISS_PROTOTYPE_BOARD_CONFIG_H

#include "memory_model.h"

#define BM_MAX_OBJECTS 16
#define BM_NAME_SIZE 32

typedef struct {
  char name[BM_NAME_SIZE];
  char compatible[BM_NAME_SIZE];
  char device_type[BM_NAME_SIZE];
  MemKind kind;
  int irq;
  bool has_irq;
  bool enabled;
} BoardObjectConfig;

typedef struct {
  char name[BM_NAME_SIZE];
  char object[BM_NAME_SIZE];
  paddr_t base;
  paddr_t size;
  PmaAttributes pma; /* 由对象类型推导，不写入板卡配置文件。 */
} BoardRegionConfig;

typedef struct {
  char name[BM_NAME_SIZE];
  paddr_t reset_pc;
  BoardObjectConfig objects[BM_MAX_OBJECTS];
  size_t object_count;
  BoardRegionConfig regions[BM_MAX_REGIONS];
  size_t region_count;
} BoardConfig;

bool board_config_load(const char *path, BoardConfig *config,
                       char *error, size_t error_size);
const BoardRegionConfig *board_config_find_region(const BoardConfig *config,
                                                   const char *name);
void board_config_dump(const BoardConfig *config, FILE *out);

#endif /* RVISS_PROTOTYPE_BOARD_CONFIG_H */
