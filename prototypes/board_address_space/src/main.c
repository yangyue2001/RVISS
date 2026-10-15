#include "board_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_result(const char *operation, MemResult result) {
  printf("%-24s -> %s\n", operation, mem_result_name(result));
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "configs/virt.board";
  BoardConfig config;
  char error[256];
  if (!board_config_load(path, &config, error, sizeof(error))) {
    fprintf(stderr, "config error: %s\n", error);
    return EXIT_FAILURE;
  }

  board_config_dump(&config, stdout);
  BoardRuntime runtime;
  if (!board_runtime_build(&config, &runtime, error, sizeof(error))) {
    fprintf(stderr, "board build error: %s\n", error);
    return EXIT_FAILURE;
  }
  printf("address space:\n");
  address_space_dump(&runtime.space, stdout);

  const BoardRegionConfig *ram_config =
      board_config_find_region(&config, "ram_map");
  const BoardRegionConfig *uart_config =
      board_config_find_region(&config, "uart_map");
  if (ram_config == NULL || uart_config == NULL) {
    fprintf(stderr, "expected demo regions are missing\n");
    board_runtime_destroy(&runtime);
    return EXIT_FAILURE;
  }

  word_t value = UINT64_C(0x1122334455667788);
  MemResult result = address_space_write(&runtime.space, ram_config->base + 8,
                                         8, value);
  print_result("RAM write 8 bytes", result);
  value = 0;
  result = address_space_read(&runtime.space, ram_config->base + 8, 8,
                              MEM_ACCESS_READ, &value);
  print_result("RAM read 8 bytes", result);
  printf("  value = %#018llx\n", (unsigned long long)value);

  uint8_t *host_ptr =
      address_space_host_ptr(&runtime.space, ram_config->base + 8, 8);
  printf("RAM host_ptr             -> %s\n", host_ptr != NULL ? "available" : "missing");

  result = address_space_write(&runtime.space, uart_config->base, 1, 'R');
  print_result("UART write 1 byte", result);
  value = 0;
  result = address_space_read(&runtime.space, uart_config->base, 1,
                              MEM_ACCESS_READ, &value);
  print_result("UART read 1 byte", result);
  printf("  value = '%c'\n", (char)value);

  result = address_space_read(&runtime.space, uart_config->base, 4,
                              MEM_ACCESS_READ, &value);
  print_result("UART read 4 bytes", result);
  result = address_space_read(&runtime.space, UINT64_C(0x40000000), 4,
                              MEM_ACCESS_READ, &value);
  print_result("unmapped read", result);

  MemRegion overlap = runtime.space.regions[0];
  overlap.name = "intentional_overlap";
  overlap.base = ram_config->base + 1;
  bool accepted = address_space_add_region(&runtime.space, &overlap);
  printf("overlap rejected         -> %s\n", accepted ? "FAIL" : "ok");

  board_runtime_destroy(&runtime);
  return (accepted || value != 'R') ? EXIT_FAILURE : EXIT_SUCCESS;
}
