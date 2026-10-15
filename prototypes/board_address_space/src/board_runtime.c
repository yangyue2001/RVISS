#include "board_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint8_t *bytes;
  size_t size;
} RamState;

typedef struct {
  uint8_t *bytes;
  size_t size;
} RomState;

typedef struct {
  uint8_t regs[256];
} UartState;

static word_t load_le(const uint8_t *bytes, int len) {
  word_t value = 0;
  for (int i = 0; i < len; i++) value |= (word_t)bytes[i] << (8 * i);
  return value;
}

static void store_le(uint8_t *bytes, int len, word_t value) {
  for (int i = 0; i < len; i++) bytes[i] = (uint8_t)(value >> (8 * i));
}

static MemResult ram_read(void *opaque, paddr_t offset, int len, word_t *data) {
  RamState *state = opaque;
  if (offset > state->size || (paddr_t)len > state->size - offset)
    return MEM_RESULT_HOST_ERROR;
  *data = load_le(state->bytes + offset, len);
  return MEM_RESULT_OK;
}

static MemResult ram_write(void *opaque, paddr_t offset, int len, word_t data) {
  RamState *state = opaque;
  if (offset > state->size || (paddr_t)len > state->size - offset)
    return MEM_RESULT_HOST_ERROR;
  store_le(state->bytes + offset, len, data);
  return MEM_RESULT_OK;
}

static uint8_t *ram_host_ptr(void *opaque, paddr_t offset, int len) {
  RamState *state = opaque;
  if (offset > state->size || (paddr_t)len > state->size - offset) return NULL;
  return state->bytes + offset;
}

static MemResult rom_read(void *opaque, paddr_t offset, int len, word_t *data) {
  RomState *state = opaque;
  if (offset > state->size || (paddr_t)len > state->size - offset)
    return MEM_RESULT_HOST_ERROR;
  *data = load_le(state->bytes + offset, len);
  return MEM_RESULT_OK;
}

static uint8_t *rom_host_ptr(void *opaque, paddr_t offset, int len) {
  RomState *state = opaque;
  if (offset > state->size || (paddr_t)len > state->size - offset) return NULL;
  return state->bytes + offset;
}

static MemResult uart_read(void *opaque, paddr_t offset, int len,
                           word_t *data) {
  UartState *state = opaque;
  if (offset >= sizeof(state->regs) || len != 1) return MEM_RESULT_UNSUPPORTED;
  *data = state->regs[offset];
  return MEM_RESULT_OK;
}

static MemResult uart_write(void *opaque, paddr_t offset, int len, word_t data) {
  UartState *state = opaque;
  if (offset >= sizeof(state->regs) || len != 1) return MEM_RESULT_UNSUPPORTED;
  state->regs[offset] = (uint8_t)data;
  return MEM_RESULT_OK;
}

static const MemOps ram_ops = {
    .read = ram_read,
    .write = ram_write,
    .host_ptr = ram_host_ptr,
};

static const MemOps rom_ops = {
    .read = rom_read,
    .write = NULL,
    .host_ptr = rom_host_ptr,
};

static const MemOps uart_ops = {
    .read = uart_read,
    .write = uart_write,
    .host_ptr = NULL,
};

static void destroy_ram(void *opaque) {
  RamState *state = opaque;
  if (state != NULL) {
    free(state->bytes);
    free(state);
  }
}

static void destroy_rom(void *opaque) {
  RomState *state = opaque;
  if (state != NULL) {
    free(state->bytes);
    free(state);
  }
}

static void destroy_uart(void *opaque) { free(opaque); }

static int find_object(const BoardConfig *config, const char *name) {
  for (size_t i = 0; i < config->object_count; i++) {
    if (strcmp(config->objects[i].name, name) == 0) return (int)i;
  }
  return -1;
}

static int find_runtime_object(const BoardRuntime *runtime, const char *name) {
  for (size_t i = 0; i < runtime->object_count; i++) {
    if (strcmp(runtime->objects[i].name, name) == 0) return (int)i;
  }
  return -1;
}

static paddr_t object_capacity(const BoardConfig *config, const char *name) {
  paddr_t capacity = 0;
  for (size_t i = 0; i < config->region_count; i++) {
    if (strcmp(config->regions[i].object, name) == 0 &&
        config->regions[i].size > capacity)
      capacity = config->regions[i].size;
  }
  return capacity;
}

static bool create_object(const BoardObjectConfig *config, paddr_t capacity,
                          MemObject *object, void **state,
                          MemObjectDestroy *destroy, char *error,
                          size_t error_size) {
  if (capacity == 0 || capacity > SIZE_MAX) {
    (void)snprintf(error, error_size, "object %s has invalid size",
                   config->name);
    return false;
  }
  memset(object, 0, sizeof(*object));
  object->name = config->name;
  object->kind = config->kind;
  switch (config->kind) {
    case MEM_KIND_RAM: {
      RamState *ram = calloc(1, sizeof(*ram));
      if (ram == NULL || (ram->bytes = calloc(1, (size_t)capacity)) == NULL) {
        free(ram);
        (void)snprintf(error, error_size, "cannot allocate RAM %s",
                       config->name);
        return false;
      }
      ram->size = (size_t)capacity;
      object->ops = &ram_ops;
      object->opaque = ram;
      *state = ram;
      *destroy = destroy_ram;
      return true;
    }
    case MEM_KIND_ROM: {
      RomState *rom = calloc(1, sizeof(*rom));
      if (rom == NULL || (rom->bytes = calloc(1, (size_t)capacity)) == NULL) {
        free(rom);
        (void)snprintf(error, error_size, "cannot allocate ROM %s",
                       config->name);
        return false;
      }
      rom->size = (size_t)capacity;
      object->ops = &rom_ops;
      object->opaque = rom;
      *state = rom;
      *destroy = destroy_rom;
      return true;
    }
    case MEM_KIND_UART: {
      UartState *uart = calloc(1, sizeof(*uart));
      if (uart == NULL) {
        (void)snprintf(error, error_size, "cannot allocate MMIO %s",
                       config->name);
        return false;
      }
      object->ops = &uart_ops;
      object->opaque = uart;
      *state = uart;
      *destroy = destroy_uart;
      return true;
    }
  }
  return false;
}

void board_runtime_destroy(BoardRuntime *runtime) {
  if (runtime == NULL) return;
  for (size_t i = 0; i < runtime->object_count; i++) {
    if (runtime->destroy[i] != NULL) runtime->destroy[i](runtime->states[i]);
  }
  memset(runtime, 0, sizeof(*runtime));
}

bool board_runtime_build(const BoardConfig *config, BoardRuntime *runtime,
                         char *error, size_t error_size) {
  if (config == NULL || runtime == NULL) return false;
  memset(runtime, 0, sizeof(*runtime));
  address_space_init(&runtime->space);

  for (size_t i = 0; i < config->object_count; i++) {
    const BoardObjectConfig *object = &config->objects[i];
    if (!object->enabled) continue;
    if (find_object(config, object->name) != (int)i) {
      (void)snprintf(error, error_size, "duplicate object %s", object->name);
      board_runtime_destroy(runtime);
      return false;
    }
    if (!create_object(object, object_capacity(config, object->name),
                       &runtime->objects[runtime->object_count],
                       &runtime->states[runtime->object_count],
                       &runtime->destroy[runtime->object_count], error,
                       error_size)) {
      board_runtime_destroy(runtime);
      return false;
    }
    runtime->object_count++;
  }

  for (size_t i = 0; i < config->region_count; i++) {
    const BoardRegionConfig *source = &config->regions[i];
    int object_index = find_runtime_object(runtime, source->object);
    if (object_index < 0) {
      (void)snprintf(error, error_size, "region %s references unknown object %s",
                     source->name, source->object);
      board_runtime_destroy(runtime);
      return false;
    }
    MemRegion region = {
        .name = source->name,
        .base = source->base,
        .size = source->size,
        .pma = source->pma,
        .object = &runtime->objects[object_index],
    };
    if (!address_space_add_region(&runtime->space, &region)) {
      (void)snprintf(error, error_size, "cannot map region %s",
                     source->name);
      board_runtime_destroy(runtime);
      return false;
    }
  }
  return true;
}
