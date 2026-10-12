#include <common.h>
#include <memory/paddr.h>
#include <memory/image.h>
#include <host/host.h>
#include <util/log.h>

/*
 * 镜像加载：自动探测 ELF / 裸二进制。
 * ELF 按 p_paddr 装载（客户机物理地址）；注意 ELF 的 entry 只打印不采用——
 * 复位 pc 由机器配置决定（和 NEMU 的行为一致，避免"image 决定 pc"的隐式约定）。
 * TODO(M8)：gzip/zstd 压缩镜像。
 */

#include <elf.h>

static bool is_elf(const void *buf, size_t size) {
  if (size < sizeof(Elf64_Ehdr)) return false;
  const Elf64_Ehdr *eh = buf;
  return memcmp(eh->e_ident, ELFMAG, SELFMAG) == 0 &&
         eh->e_ident[EI_CLASS] == ELFCLASS64;
}

paddr_t image_load_raw(const char *path, paddr_t load_addr) {
  void *buf = NULL;
  size_t size = 0;
  if (!host_read_file(path, &buf, &size)) return 0;

  Assert(mem_in_ram(load_addr, (int)size),
         "镜像 %zu 字节放不进 RAM（装载地址 " FMT_PADDR "）", size, load_addr);
  memcpy(mem_host_ptr(load_addr), buf, size);
  free(buf);

  Log("raw image: %zu bytes -> " FMT_PADDR, size, load_addr);
  return load_addr + size;
}

paddr_t image_load_elf(const char *path, paddr_t load_addr) {
  void *buf = NULL;
  size_t size = 0;
  if (!host_read_file(path, &buf, &size)) return 0;

  if (!is_elf(buf, size)) {
    free(buf);
    return 0;
  }

  const Elf64_Ehdr *eh = buf;
  Assert(eh->e_machine == EM_RISCV, "不是 RISC-V ELF（e_machine=%u）", eh->e_machine);
  Assert(eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize <= size,
         "ELF program header 越界");

  paddr_t end = load_addr;
  for (int i = 0; i < eh->e_phnum; i++) {
    const Elf64_Phdr *ph = (const Elf64_Phdr *)((const uint8_t *)buf +
                                                eh->e_phoff + (uint64_t)i * eh->e_phentsize);
    if (ph->p_type != PT_LOAD) continue;

    Assert(mem_in_ram(ph->p_paddr, (int)ph->p_memsz),
           "ELF 段 " FMT_PADDR " (%lu 字节) 不在 RAM 内",
           (paddr_t)ph->p_paddr, (unsigned long)ph->p_memsz);

    uint8_t *dst = mem_host_ptr(ph->p_paddr);
    if (ph->p_filesz > 0) {
      memcpy(dst, (const uint8_t *)buf + ph->p_offset, ph->p_filesz);
    }
    if (ph->p_memsz > ph->p_filesz) {
      memset(dst + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
    }
    if (ph->p_paddr + ph->p_memsz > end) {
      end = (paddr_t)(ph->p_paddr + ph->p_memsz);
    }
  }

  Log("ELF image: entry=" FMT_PADDR ", %u phdrs", (paddr_t)eh->e_entry, eh->e_phnum);
  free(buf);
  return end;
}

paddr_t image_load(const char *path, paddr_t load_addr) {
  void *buf = NULL;
  size_t size = 0;
  if (!host_read_file(path, &buf, &size)) return 0;
  const bool elf = is_elf(buf, size);
  free(buf);

  return elf ? image_load_elf(path, load_addr)
             : image_load_raw(path, load_addr);
}
