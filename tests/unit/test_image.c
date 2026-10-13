#include <common.h>
#include <elf.h>
#include <memory/image.h>
#include <memory/paddr.h>
#include <stdlib.h>
#include <unistd.h>
#include "test.h"

#define BASE UINT64_C(0x80000000)
#define RAM_SIZE 4096
#define FILE_SIZE 256

static paddr_t load(const void *buf, size_t size, paddr_t addr) {
  char path[] = "/tmp/rviss-image-XXXXXX";
  int fd = mkstemp(path);
  CHECK(fd >= 0);
  if (fd < 0) return 0;
  FILE *fp = fdopen(fd, "wb");
  CHECK(fp != NULL);
  if (fp == NULL) { close(fd); unlink(path); return 0; }
  CHECK_EQ(fwrite(buf, 1, size, fp), size);
  CHECK_EQ(fclose(fp), 0);
  paddr_t end = image_load(path, addr);
  CHECK_EQ(unlink(path), 0);
  return end;
}

static Elf64_Ehdr header(void) {
  Elf64_Ehdr eh = {.e_type = ET_EXEC, .e_machine = EM_RISCV,
                  .e_version = EV_CURRENT, .e_ehsize = sizeof(Elf64_Ehdr),
                  .e_phoff = sizeof(Elf64_Ehdr),
                  .e_phentsize = sizeof(Elf64_Phdr), .e_phnum = 1};
  memcpy(eh.e_ident, ELFMAG, SELFMAG);
  eh.e_ident[EI_CLASS] = ELFCLASS64;
  eh.e_ident[EI_DATA] = ELFDATA2LSB;
  eh.e_ident[EI_VERSION] = EV_CURRENT;
  return eh;
}

static Elf64_Phdr segment(void) {
  return (Elf64_Phdr){.p_type = PT_LOAD, .p_offset = 192,
                      .p_paddr = BASE + 128, .p_filesz = 4, .p_memsz = 16};
}

static void make_elf(uint8_t *buf, Elf64_Ehdr eh, Elf64_Phdr ph) {
  memset(buf, 0, FILE_SIZE);
  memcpy(buf, &eh, sizeof(eh));
  memcpy(buf + sizeof(eh), &ph, sizeof(ph));
  memcpy(buf + 192, "RV64", 4);
}

static void rejected(const uint8_t *buf, size_t size, paddr_t addr) {
  uint8_t unchanged[RAM_SIZE];
  memset(unchanged, 0xa5, sizeof(unchanged));
  memset(mem_host_ptr(BASE), 0xa5, RAM_SIZE);
  CHECK_EQ(load(buf, size, addr), 0);
  /* Invalid input must not leave an earlier segment partially installed. */
  CHECK(memcmp(mem_host_ptr(BASE), unchanged, RAM_SIZE) == 0);
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  CHECK(mem_init(BASE, RAM_SIZE));
  const uint8_t raw[] = {1, 2, 3, 4};
  CHECK_EQ(load(raw, sizeof(raw), BASE + RAM_SIZE - 4), BASE + RAM_SIZE);
  CHECK_EQ(paddr_read(BASE + RAM_SIZE - 4, 4), 0x04030201);

  uint8_t buf[FILE_SIZE];
  Elf64_Ehdr eh = header();
  Elf64_Phdr ph = segment();
  make_elf(buf, eh, ph);
  memset(mem_host_ptr(BASE), 0xa5, RAM_SIZE);
  CHECK_EQ(load(buf, sizeof(buf), BASE), BASE + 144);
  CHECK_EQ(paddr_read(BASE + 128, 4), 0x34365652);
  CHECK_EQ(paddr_read(BASE + 132, 8), 0);
  CHECK_EQ(paddr_read(BASE + 140, 4), 0);
  CHECK_EQ(paddr_read(BASE + 144, 1), 0xa5);

  /* A magic-only/truncated ELF must not be treated as a raw binary. */
  rejected(buf, SELFMAG, BASE);
  eh.e_phentsize = 1; make_elf(buf, eh, ph); rejected(buf, sizeof(buf), BASE);
  eh = header(); eh.e_phoff = UINT64_MAX - 16;
  make_elf(buf, eh, ph); rejected(buf, sizeof(buf), BASE);
  eh = header(); eh.e_ident[EI_DATA] = ELFDATA2MSB;
  make_elf(buf, eh, ph); rejected(buf, sizeof(buf), BASE);
  eh = header(); ph.p_filesz = 17;
  make_elf(buf, eh, ph); rejected(buf, sizeof(buf), BASE);
  ph = segment(); ph.p_offset = FILE_SIZE - 2;
  make_elf(buf, eh, ph); rejected(buf, sizeof(buf), BASE);
  ph.p_offset = UINT64_MAX - 1;
  make_elf(buf, eh, ph); rejected(buf, sizeof(buf), BASE);
  ph = segment(); ph.p_paddr = UINT64_MAX - 7;
  make_elf(buf, eh, ph); rejected(buf, sizeof(buf), BASE);
  ph = segment(); ph.p_memsz = UINT64_C(0x100000010);
  make_elf(buf, eh, ph); rejected(buf, sizeof(buf), BASE);
  rejected(raw, sizeof(raw), BASE + RAM_SIZE - 2);
  rejected(raw, sizeof(raw), UINT64_MAX - 1);

  /* First segment valid, second invalid: validate the complete image first. */
  eh = header(); eh.e_phnum = 2; ph = segment(); make_elf(buf, eh, ph);
  ph.p_offset = FILE_SIZE - 2;
  memcpy(buf + sizeof(eh) + sizeof(ph), &ph, sizeof(ph));
  rejected(buf, sizeof(buf), BASE);

  /* Program headers need not be host-aligned. */
  eh = header(); eh.e_phoff++; ph = segment(); make_elf(buf, eh, ph);
  memcpy(buf + eh.e_phoff, &ph, sizeof(ph));
  CHECK_EQ(load(buf, sizeof(buf), BASE), BASE + 144);
  mem_shutdown();
  TEST_MAIN_END();
}
