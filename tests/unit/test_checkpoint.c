#include <checkpoint/checkpoint.h>
#include <cpu/cpu.h>
#include <memory/paddr.h>
#include <stdlib.h>
#include <unistd.h>
#include "test.h"

#define BASE UINT64_C(0x80000000)
#define RAM_SIZE 4096

/* Native checkpoint fixed prefix; CPU_state follows this in version 2. */
typedef struct {
  uint64_t magic;
  uint32_t version, header_size;
  uint64_t ram_base, ram_size;
} Prefix;

static void flip_byte(const char *path, size_t offset) {
  FILE *fp = fopen(path, "r+b");
  CHECK(fp != NULL);
  if (fp == NULL) return;
  CHECK_EQ(fseek(fp, (long)offset, SEEK_SET), 0);
  int byte = fgetc(fp);
  CHECK(byte != EOF);
  CHECK_EQ(fseek(fp, (long)offset, SEEK_SET), 0);
  CHECK(fputc(byte ^ 1, fp) != EOF);
  CHECK_EQ(fclose(fp), 0);
}

static void reject_without_mutation(const char *path) {
  cpu_reset(BASE + 256);
  cpu.gpr[5] = 0xdeadbeef;
  memset(mem_host_ptr(BASE), 0xa5, RAM_SIZE);
  CPU_state before = cpu;
  uint8_t ram_before[RAM_SIZE];
  memcpy(ram_before, mem_host_ptr(BASE), RAM_SIZE);
  CHECK(!cpt_load(path));
  CHECK(memcmp(&cpu, &before, sizeof(cpu)) == 0);
  CHECK(memcmp(mem_host_ptr(BASE), ram_before, RAM_SIZE) == 0);
}

int main(void) {
  CHECK(mem_init(BASE, RAM_SIZE));
  char path[] = "/tmp/rviss-checkpoint-XXXXXX";
  int fd = mkstemp(path);
  CHECK(fd >= 0);
  if (fd < 0) { mem_shutdown(); TEST_MAIN_END(); }
  CHECK_EQ(close(fd), 0);
  cpu_reset(BASE);
  cpu.gpr[5] = 0x12345678;
  CHECK(paddr_write(BASE, 8, UINT64_C(0x1122334455667788)));
  CHECK(cpt_save(path));
  cpu.gpr[5] = 0;
  CHECK(paddr_write(BASE, 8, 0));
  CHECK(cpt_load(path));
  CHECK_EQ(cpu.gpr[5], 0x12345678);
  CHECK_EQ(paddr_read(BASE, 8), UINT64_C(0x1122334455667788));

  FILE *fp = fopen(path, "rb");
  CHECK(fp != NULL);
  Prefix h = {0};
  if (fp != NULL) {
    CHECK_EQ(fread(&h, sizeof(h), 1, fp), 1);
    CHECK_EQ(fclose(fp), 0);
  }
  CHECK_EQ(h.ram_base, BASE);
  CHECK_EQ(h.ram_size, RAM_SIZE);
  CHECK_EQ(h.header_size, sizeof(Prefix) + sizeof(CPU_state) + sizeof(uint64_t));

  flip_byte(path, sizeof(Prefix) + offsetof(CPU_state, gpr) + 5 * sizeof(word_t));
  reject_without_mutation(path);
  CHECK(cpt_save(path));
  flip_byte(path, h.header_size + RAM_SIZE - 1);
  reject_without_mutation(path);
  CHECK(cpt_save(path));
  CHECK_EQ(truncate(path, h.header_size + RAM_SIZE - 1), 0);
  reject_without_mutation(path);
  CHECK(cpt_save(path));
  /* Version 1 did not protect CPU state and must not be accepted as v2. */
  fp = fopen(path, "r+b");
  CHECK(fp != NULL);
  if (fp != NULL) {
    uint32_t old_version = 1;
    CHECK_EQ(fseek(fp, offsetof(Prefix, version), SEEK_SET), 0);
    CHECK_EQ(fwrite(&old_version, sizeof(old_version), 1, fp), 1);
    CHECK_EQ(fclose(fp), 0);
  }
  reject_without_mutation(path);
  CHECK_EQ(unlink(path), 0);
  mem_shutdown();
  TEST_MAIN_END();
}
