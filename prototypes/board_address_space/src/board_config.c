#include "board_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t error_size, size_t line,
                      const char *message) {
  if (error != NULL && error_size != 0)
    (void)snprintf(error, error_size, "line %zu: %s", line, message);
}

static bool parse_kind(const char *text, MemKind *kind) {
  if (strcmp(text, "RAM") == 0) *kind = MEM_KIND_RAM;
  else if (strcmp(text, "ROM") == 0) *kind = MEM_KIND_ROM;
  else if (strcmp(text, "UART") == 0 || strcmp(text, "MMIO") == 0)
    *kind = MEM_KIND_UART;
  else return false;
  return true;
}

static bool parse_pma_type(const char *text, PmaType *type) {
  if (strcmp(text, "main") == 0) *type = PMA_MAIN_MEMORY;
  else if (strcmp(text, "io") == 0) *type = PMA_IO;
  else return false;
  return true;
}

static bool parse_permissions(const char *text, PmaAttributes *pma) {
  pma->readable = strchr(text, 'r') != NULL;
  pma->writable = strchr(text, 'w') != NULL;
  pma->executable = strchr(text, 'x') != NULL;
  for (const char *p = text; *p != '\0'; p++) {
    if (*p != 'r' && *p != 'w' && *p != 'x' && *p != '-') return false;
  }
  return true;
}

static bool parse_widths(const char *text, uint8_t *mask) {
  char copy[64];
  if (strlen(text) >= sizeof(copy)) return false;
  strcpy(copy, text);
  *mask = 0;
  if (strcmp(copy, "-") == 0) return true;
  for (char *part = strtok(copy, ","); part != NULL;
       part = strtok(NULL, ",")) {
    char *end = NULL;
    unsigned long width = strtoul(part, &end, 0);
    if (end == part || *end != '\0') return false;
    switch (width) {
      case 1: *mask |= BM_WIDTH_1; break;
      case 2: *mask |= BM_WIDTH_2; break;
      case 4: *mask |= BM_WIDTH_4; break;
      case 8: *mask |= BM_WIDTH_8; break;
      default: return false;
    }
  }
  return *mask != 0;
}

static bool parse_misaligned(const char *text, PmaMisalignedPolicy *policy) {
  if (strcmp(text, "trap") == 0) *policy = PMA_MISALIGNED_TRAP;
  else if (strcmp(text, "split") == 0) *policy = PMA_MISALIGNED_SPLIT;
  else return false;
  return true;
}

static bool parse_number(const char *text, paddr_t *value) {
  char *end = NULL;
  unsigned long long parsed = strtoull(text, &end, 0);
  if (end == text || *end != '\0') return false;
  *value = (paddr_t)parsed;
  return true;
}

static bool parse_bool(int value) { return value == 0 || value == 1; }

static char *skip_space(char *line) {
  while (isspace((unsigned char)*line)) line++;
  return line;
}

bool board_config_load(const char *path, BoardConfig *config,
                       char *error, size_t error_size) {
  if (path == NULL || config == NULL) return false;
  memset(config, 0, sizeof(*config));
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    if (error != NULL && error_size != 0)
      (void)snprintf(error, error_size, "cannot open %s", path);
    return false;
  }

  char line[512];
  size_t line_number = 0;
  bool ok = true;
  while (fgets(line, sizeof(line), file) != NULL) {
    line_number++;
    char *comment = strchr(line, '#');
    if (comment != NULL) *comment = '\0';
    char *text = skip_space(line);
    if (*text == '\0') continue;

    char command[16];
    if (sscanf(text, "%15s", command) != 1) continue;
    if (strcmp(command, "board") == 0) {
      if (sscanf(text, "board %31s", config->name) != 1) {
        set_error(error, error_size, line_number, "invalid board line");
        ok = false;
        break;
      }
    } else if (strcmp(command, "reset_pc") == 0) {
      char value[64];
      if (sscanf(text, "reset_pc %63s", value) != 1 ||
          !parse_number(value, &config->reset_pc)) {
        set_error(error, error_size, line_number, "invalid reset_pc");
        ok = false;
        break;
      }
    } else if (strcmp(command, "object") == 0) {
      char name[BM_NAME_SIZE], kind_text[16];
      if (config->object_count >= BM_MAX_OBJECTS ||
          sscanf(text, "object %31s %15s", name, kind_text) != 2 ||
          !parse_kind(kind_text, &config->objects[config->object_count].kind)) {
        set_error(error, error_size, line_number, "invalid object line");
        ok = false;
        break;
      }
      strcpy(config->objects[config->object_count].name, name);
      config->object_count++;
    } else if (strcmp(command, "region") == 0) {
      BoardRegionConfig *region;
      char base_text[64], size_text[64], pma_text[16], permissions[16];
      char widths[64], atomics[64], misaligned[16];
      int idempotent, cacheable;
      if (config->region_count >= BM_MAX_REGIONS) {
        set_error(error, error_size, line_number, "too many regions");
        ok = false;
        break;
      }
      region = &config->regions[config->region_count];
      int count = sscanf(text,
          "region %31s %31s %63s %63s %15s %15s %63s %63s %15s %d %d",
          region->name, region->object, base_text, size_text, pma_text,
          permissions, widths, atomics, misaligned, &idempotent, &cacheable);
      if (count != 11 || !parse_number(base_text, &region->base) ||
          !parse_number(size_text, &region->size) ||
          !parse_pma_type(pma_text, &region->pma.type) ||
          !parse_permissions(permissions, &region->pma) ||
          !parse_widths(widths, &region->pma.supported_widths) ||
          !parse_widths(atomics, &region->pma.atomic_widths) ||
          !parse_misaligned(misaligned, &region->pma.misaligned) ||
          !parse_bool(idempotent) || !parse_bool(cacheable)) {
        set_error(error, error_size, line_number, "invalid region line");
        ok = false;
        break;
      }
      region->pma.idempotent = idempotent != 0;
      region->pma.cacheable = cacheable != 0;
      config->region_count++;
    } else {
      set_error(error, error_size, line_number, "unknown directive");
      ok = false;
      break;
    }
  }
  fclose(file);
  if (!ok) return false;
  if (config->name[0] == '\0' || config->region_count == 0) {
    if (error != NULL && error_size != 0)
      (void)snprintf(error, error_size, "board or region is missing");
    return false;
  }
  return true;
}

const BoardRegionConfig *board_config_find_region(const BoardConfig *config,
                                                   const char *name) {
  if (config == NULL || name == NULL) return NULL;
  for (size_t i = 0; i < config->region_count; i++) {
    if (strcmp(config->regions[i].name, name) == 0) return &config->regions[i];
  }
  return NULL;
}

void board_config_dump(const BoardConfig *config, FILE *out) {
  fprintf(out, "board %s reset_pc=%#018llx\n", config->name,
          (unsigned long long)config->reset_pc);
  fprintf(out, "objects:\n");
  for (size_t i = 0; i < config->object_count; i++) {
    fprintf(out, "  %-12s %s\n", config->objects[i].name,
            mem_kind_name(config->objects[i].kind));
  }
  fprintf(out, "regions:\n");
  for (size_t i = 0; i < config->region_count; i++) {
    const BoardRegionConfig *r = &config->regions[i];
    fprintf(out, "  %-12s object=%-8s base=%#018llx size=%#llx\n",
            r->name, r->object, (unsigned long long)r->base,
            (unsigned long long)r->size);
  }
}
