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

static void trim(char *text) {
  char *start = text;
  while (isspace((unsigned char)*start)) start++;
  if (start != text) memmove(text, start, strlen(start) + 1);
  size_t length = strlen(text);
  while (length != 0 && isspace((unsigned char)text[length - 1]))
    text[--length] = '\0';
}

static bool parse_number(const char *text, paddr_t *value) {
  char *end = NULL;
  unsigned long long parsed = strtoull(text, &end, 0);
  if (end == text || *end != '\0') return false;
  *value = (paddr_t)parsed;
  return true;
}

static bool parse_quoted(const char *text, char *value, size_t value_size) {
  const char *first = strchr(text, '"');
  if (first == NULL) return false;
  const char *last = strchr(first + 1, '"');
  if (last == NULL || (size_t)(last - first - 1) >= value_size) return false;
  memcpy(value, first + 1, (size_t)(last - first - 1));
  value[last - first - 1] = '\0';
  return true;
}

static bool parse_angle_values(const char *text, paddr_t *first,
                               paddr_t *second, bool two_values) {
  const char *left = strchr(text, '<');
  const char *right = left == NULL ? NULL : strchr(left + 1, '>');
  if (left == NULL || right == NULL) return false;

  char values[128];
  size_t length = (size_t)(right - left - 1);
  if (length == 0 || length >= sizeof(values)) return false;
  memcpy(values, left + 1, length);
  values[length] = '\0';

  char first_text[64], second_text[64];
  int count = sscanf(values, "%63s %63s", first_text, second_text);
  if ((two_values && count != 2) || (!two_values && count != 1) ||
      !parse_number(first_text, first))
    return false;
  if (two_values && !parse_number(second_text, second)) return false;
  return true;
}

static bool parse_node_header(const char *text, char *object_name,
                              size_t object_name_size) {
  const char *left = strchr(text, '{');
  if (left == NULL) return false;
  char header[128];
  size_t length = (size_t)(left - text);
  if (length == 0 || length >= sizeof(header)) return false;
  memcpy(header, text, length);
  header[length] = '\0';
  trim(header);
  if (header[0] == '/' || header[0] == '\0') return false;

  char *colon = strchr(header, ':');
  char *name = colon == NULL ? header : colon + 1;
  trim(name);
  if (colon != NULL) {
    *colon = '\0';
    trim(header);
    name = header;
  }
  char *at = strchr(name, '@');
  if (at != NULL) *at = '\0';
  trim(name);
  if (*name == '\0' || strlen(name) >= object_name_size) return false;
  strcpy(object_name, name);
  return true;
}

static PmaAttributes default_pma(MemKind kind) {
  if (kind == MEM_KIND_RAM) {
    return (PmaAttributes){
        .type = PMA_MAIN_MEMORY,
        .readable = true,
        .writable = true,
        .executable = true,
        .supported_widths = BM_WIDTH_1 | BM_WIDTH_2 | BM_WIDTH_4 | BM_WIDTH_8,
        .atomic_widths = BM_WIDTH_4 | BM_WIDTH_8,
        .misaligned = PMA_MISALIGNED_SPLIT,
        .idempotent = true,
        .cacheable = true,
    };
  }
  return (PmaAttributes){
      .type = PMA_IO,
      .readable = true,
      .writable = true,
      .executable = false,
      .supported_widths = BM_WIDTH_1,
      .atomic_widths = 0,
      .misaligned = PMA_MISALIGNED_TRAP,
      .idempotent = false,
      .cacheable = false,
  };
}

static bool resolve_kind(BoardObjectConfig *object) {
  if (strcmp(object->device_type, "memory") == 0) {
    object->kind = MEM_KIND_RAM;
    return true;
  }
  if (strcmp(object->compatible, "ns16550a") == 0) {
    object->kind = MEM_KIND_UART;
    return true;
  }
  return false;
}

static bool duplicate_object(const BoardConfig *config, const char *name) {
  for (size_t i = 0; i < config->object_count; i++) {
    if (strcmp(config->objects[i].name, name) == 0) return true;
  }
  return false;
}

static bool finish_node(BoardConfig *config, int object_index,
                        paddr_t base, paddr_t size, bool have_reg,
                        char *error, size_t error_size, size_t line) {
  BoardObjectConfig *object = &config->objects[object_index];
  if (!resolve_kind(object)) {
    set_error(error, error_size, line, "unsupported compatible/device-type");
    return false;
  }
  if (!object->enabled || !have_reg) return true;
  if (config->region_count >= BM_MAX_REGIONS || size == 0 ||
      base > UINT64_MAX - size) {
    set_error(error, error_size, line, "invalid node reg");
    return false;
  }
  BoardRegionConfig *region = &config->regions[config->region_count++];
  (void)snprintf(region->name, sizeof(region->name), "%s_map", object->name);
  strcpy(region->object, object->name);
  region->base = base;
  region->size = size;
  region->pma = default_pma(object->kind);
  return true;
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
  bool root_seen = false;
  bool root_closed = false;
  int current_object = -1;
  paddr_t node_base = 0, node_size = 0;
  bool node_have_reg = false;
  bool ok = true;

  while (fgets(line, sizeof(line), file) != NULL) {
    line_number++;
    char *comment = strstr(line, "//");
    if (comment != NULL) *comment = '\0';
    trim(line);
    if (*line == '\0' || strcmp(line, "/dts-v1/;") == 0) continue;

    if (!root_seen) {
      if (strcmp(line, "/ {") == 0) root_seen = true;
      else {
        set_error(error, error_size, line_number, "root node is missing");
        ok = false;
        break;
      }
      continue;
    }

    if (current_object < 0 && line[0] == '}') {
      root_closed = true;
      continue;
    }
    if (root_closed) {
      if (line[0] != '\0' && strcmp(line, ";") != 0) {
        set_error(error, error_size, line_number, "data after root node");
        ok = false;
        break;
      }
      continue;
    }

    if (current_object < 0 && strchr(line, '{') != NULL) {
      if (config->object_count >= BM_MAX_OBJECTS) {
        set_error(error, error_size, line_number, "too many nodes");
        ok = false;
        break;
      }
      BoardObjectConfig *object = &config->objects[config->object_count];
      memset(object, 0, sizeof(*object));
      object->enabled = true;
      if (!parse_node_header(line, object->name, sizeof(object->name)) ||
          duplicate_object(config, object->name)) {
        set_error(error, error_size, line_number, "invalid or duplicate node");
        ok = false;
        break;
      }
      current_object = (int)config->object_count++;
      node_base = node_size = 0;
      node_have_reg = false;
      continue;
    }

    if (current_object >= 0 && line[0] == '}') {
      if (!finish_node(config, current_object, node_base, node_size,
                       node_have_reg, error, error_size, line_number)) {
        ok = false;
        break;
      }
      current_object = -1;
      continue;
    }

    if (current_object >= 0) {
      BoardObjectConfig *object = &config->objects[current_object];
      if (strncmp(line, "compatible", 10) == 0) {
        if (!parse_quoted(line, object->compatible,
                          sizeof(object->compatible))) {
          set_error(error, error_size, line_number, "invalid compatible");
          ok = false;
          break;
        }
      } else if (strncmp(line, "device-type", 11) == 0) {
        if (!parse_quoted(line, object->device_type,
                          sizeof(object->device_type))) {
          set_error(error, error_size, line_number, "invalid device-type");
          ok = false;
          break;
        }
      } else if (strncmp(line, "reg", 3) == 0) {
        if (!parse_angle_values(line, &node_base, &node_size, true)) {
          set_error(error, error_size, line_number, "invalid reg");
          ok = false;
          break;
        }
        node_have_reg = true;
      } else if (strncmp(line, "interrupts", 10) == 0) {
        paddr_t irq;
        if (!parse_angle_values(line, &irq, NULL, false) || irq > INT32_MAX) {
          set_error(error, error_size, line_number, "invalid interrupts");
          ok = false;
          break;
        }
        object->irq = (int)irq;
        object->has_irq = true;
      } else if (strncmp(line, "status", 6) == 0) {
        char status[16];
        if (!parse_quoted(line, status, sizeof(status)) ||
            (strcmp(status, "okay") != 0 && strcmp(status, "disabled") != 0)) {
          set_error(error, error_size, line_number, "invalid status");
          ok = false;
          break;
        }
        object->enabled = strcmp(status, "okay") == 0;
      }
      continue;
    }

    if (strncmp(line, "model", 5) == 0) {
      if (!parse_quoted(line, config->name, sizeof(config->name))) {
        set_error(error, error_size, line_number, "invalid model");
        ok = false;
        break;
      }
    } else if (strncmp(line, "reset-pc", 8) == 0) {
      if (!parse_angle_values(line, &config->reset_pc, NULL, false)) {
        set_error(error, error_size, line_number, "invalid reset-pc");
        ok = false;
        break;
      }
    }
  }
  fclose(file);

  if (!ok) return false;
  if (!root_seen || !root_closed || config->name[0] == '\0' ||
      config->region_count == 0) {
    if (error != NULL && error_size != 0)
      (void)snprintf(error, error_size, "incomplete board description");
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
  fprintf(out, "nodes:\n");
  for (size_t i = 0; i < config->object_count; i++) {
    const BoardObjectConfig *object = &config->objects[i];
    fprintf(out, "  %-12s %-12s compatible=%s irq=", object->name,
            mem_kind_name(object->kind),
            object->compatible[0] != '\0' ? object->compatible : "memory");
    if (object->has_irq) fprintf(out, "%d", object->irq);
    else fprintf(out, "-");
    fprintf(out, " status=%s\n", object->enabled ? "okay" : "disabled");
  }
  fprintf(out, "regions:\n");
  for (size_t i = 0; i < config->region_count; i++) {
    const BoardRegionConfig *region = &config->regions[i];
    fprintf(out, "  %-12s object=%-8s base=%#018llx size=%#llx\n",
            region->name, region->object,
            (unsigned long long)region->base,
            (unsigned long long)region->size);
  }
}
