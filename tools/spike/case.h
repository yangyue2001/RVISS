#ifndef RVISS_TOOLS_SPIKE_CASE_H
#define RVISS_TOOLS_SPIKE_CASE_H

/* 仅供两个独立测试进程使用的文本输入/JSON 输出，不是模拟器 ABI。
 * 数值全部由输入决定；执行过程中不向参考复制 DUT 状态。 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const unsigned diff_csrs[] = {
  0x300, 0x302, 0x303, 0x304, 0x344, 0x305, 0x341, 0x342, 0x343, 0x340,
  0x306, 0x105, 0x141, 0x142, 0x143, 0x140, 0x180, 0x106
};
#define DIFF_CSR_COUNT (sizeof(diff_csrs) / sizeof(diff_csrs[0]))

typedef struct {
  uint64_t base, size, pc, steps, priv;
  uint64_t gpr[32], csr[4096];
  bool csr_set[4096];
  char isa[128];
  uint8_t *ram;
} DiffCase;

static inline void case_error(const char *reason) {
  fprintf(stderr, "invalid diff case: %s\n", reason);
  exit(2);
}

static inline DiffCase load_case(const char *path) {
  DiffCase c;
  memset(&c, 0, sizeof(c));
  FILE *file = fopen(path, "r");
  if (!file) case_error("open");
  char key[32], value[128];
  while (fscanf(file, "%31s", key) == 1) {
    if (!strcmp(key, "isa")) {
      if (fscanf(file, "%127s", c.isa) != 1) case_error("isa");
    } else if (!strcmp(key, "gpr") || !strcmp(key, "csr")) {
      unsigned index;
      if (fscanf(file, "%u %127s", &index, value) != 2) case_error(key);
      if (index >= (!strcmp(key, "gpr") ? 32u : 4096u)) case_error("index");
      if (!strcmp(key, "gpr")) c.gpr[index] = strtoull(value, NULL, 0);
      else { c.csr[index] = strtoull(value, NULL, 0); c.csr_set[index] = true; }
    } else if (!strcmp(key, "mem")) {
      char address[128], data[128];
      unsigned length;
      if (fscanf(file, "%127s %u %127s", address, &length, data) != 3) case_error("mem");
      uint64_t addr = strtoull(address, NULL, 0), v = strtoull(data, NULL, 0);
      if (!c.ram || addr < c.base || addr - c.base >= c.size ||
          length > c.size - (addr - c.base) || !length || length > 8) case_error("range");
      for (unsigned i = 0; i < length; i++) c.ram[addr - c.base + i] = (uint8_t)(v >> (8*i));
    } else {
      if (fscanf(file, "%127s", value) != 1) case_error(key);
      uint64_t v = strtoull(value, NULL, 0);
      if (!strcmp(key, "base")) c.base = v;
      else if (!strcmp(key, "size")) {
        if (c.ram || !v || v > 65536) case_error("size");
        c.size = v;
        c.ram = (uint8_t *)calloc(1, (size_t)v);
        if (!c.ram) case_error("allocation");
      } else if (!strcmp(key, "pc")) c.pc = v;
      else if (!strcmp(key, "priv")) c.priv = v;
      else if (!strcmp(key, "steps")) c.steps = v;
      else case_error(key);
    }
  }
  fclose(file);
  if (!c.ram || !c.isa[0] || c.steps > 100000 || c.gpr[0]) case_error("incomplete");
  return c;
}

typedef struct {
  uint64_t seq, pc_before, pc, privilege_before, privilege, retired_total;
  uint64_t raw, gpr[32], csr[DIFF_CSR_COUNT];
  unsigned length, raw_bytes;
  bool valid, retired;
  const char *outcome;
  bool store;
  uint64_t store_addr, store_data;
  unsigned store_length;
} DiffSnapshot;

static inline void emit_snapshot(const DiffSnapshot *s, const uint8_t *ram, size_t size) {
  uint64_t hash = UINT64_C(0xcbf29ce484222325);
  for (size_t i = 0; i < size; i++) hash = (hash ^ ram[i]) * UINT64_C(0x100000001b3);
  printf("{\"seq\":%" PRIu64 ",\"pc_before\":\"%016" PRIx64 "\",\"pc\":\"%016" PRIx64
         "\",\"privilege_before\":%" PRIu64 ",\"privilege\":%" PRIu64
         ",\"retired_total\":%" PRIu64 ",\"outcome\":\"%s\",\"retired\":%s,"
         "\"raw\":\"%016" PRIx64 "\",\"length\":%u,\"raw_bytes\":%u,\"valid\":%s,\"gpr\":[",
         s->seq, s->pc_before, s->pc, s->privilege_before, s->privilege, s->retired_total,
         s->outcome, s->retired ? "true" : "false", s->raw, s->length, s->raw_bytes,
         s->valid ? "true" : "false");
  for (unsigned i = 0; i < 32; i++) printf("%s\"%016" PRIx64 "\"", i ? "," : "", s->gpr[i]);
  printf("],\"csrs\":{");
  for (unsigned i = 0; i < DIFF_CSR_COUNT; i++)
    printf("%s\"%03x\":\"%016" PRIx64 "\"", i ? "," : "", diff_csrs[i], s->csr[i]);
  printf("},\"ram_hash\":\"%016" PRIx64 "\",\"store\":[", hash);
  if (s->store) printf("\"%016" PRIx64 "\",%u,\"%016" PRIx64 "\"",
                        s->store_addr, s->store_length, s->store_data);
  printf("]}\n");
}
#endif
