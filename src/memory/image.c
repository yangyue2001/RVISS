#include <common.h>
#include <host/host.h>
#include <memory/image.h>
#include <memory/paddr.h>
#include <util/log.h>

/*
 * 镜像加载：自动探测 ELF / 裸二进制。
 * ELF 按 p_paddr 装载（客户机物理地址）；注意 ELF 的 entry 只打印不采用——
 * 复位 pc 由机器配置决定（和 NEMU 的行为一致，避免"image 决定 pc"的隐式约定）。
 * TODO(M8)：gzip/zstd 压缩镜像。
 */

#include <elf.h>

static bool is_elf(const void* buf, size_t size) {
  /* Recognize the magic first, so a truncated/wrong-class ELF is rejected
   * rather than silently installed as raw bytes. */
  return size >= SELFMAG && memcmp(buf, ELFMAG, SELFMAG) == 0;
}

static paddr_t image_load_raw_buf(const void* buf, size_t size,
                                  paddr_t load_addr) {
  if (!mem_in_ram(load_addr, size)) {
    Loge("raw image does not fit RAM: %zu bytes @ " FMT_PADDR, size, load_addr);
    return 0;
  }
  memcpy(mem_host_ptr(load_addr), buf, size);

  Log("raw image: %zu bytes -> " FMT_PADDR, size, load_addr);
  return load_addr + size;
}

static bool file_range(size_t size, uint64_t offset, uint64_t length) {
  return offset <= size && length <= size - offset;
}

static Elf64_Phdr program_header(const void* buf, const Elf64_Ehdr* eh,
                                 unsigned i) {
  Elf64_Phdr ph;
  /* File offsets need not have the host alignment of Elf64_Phdr. */
  memcpy(&ph, (const uint8_t*)buf + eh->e_phoff + (uint64_t)i * eh->e_phentsize,
         sizeof(ph));
  return ph;
}

static paddr_t image_load_elf_buf(const void* buf, size_t size,
                                  paddr_t load_addr) {
  Elf64_Ehdr eh;
  if (size < sizeof(eh)) {
    Loge("truncated ELF header");
    return 0;
  }
  memcpy(&eh, buf, sizeof(eh));
  if (eh.e_ident[EI_CLASS] != ELFCLASS64 ||
      eh.e_ident[EI_DATA] != ELFDATA2LSB ||
      eh.e_ident[EI_VERSION] != EV_CURRENT || eh.e_version != EV_CURRENT ||
      eh.e_machine != EM_RISCV || eh.e_ehsize != sizeof(eh) ||
      eh.e_phentsize < sizeof(Elf64_Phdr) || eh.e_phnum == 0 ||
      eh.e_phnum == PN_XNUM ||
      !file_range(size, eh.e_phoff, (uint64_t)eh.e_phnum * eh.e_phentsize)) {
    Loge("unsupported or out-of-range RV64 ELF header");
    return 0;
  }

  /* Validate every segment before writing any RAM: a bad later segment must
   * not leave a partially installed image. Subtraction avoids wraparound. */
  bool has_load = false;
  paddr_t end = load_addr;
  for (unsigned i = 0; i < eh.e_phnum; i++) {
    Elf64_Phdr ph = program_header(buf, &eh, i);
    if (ph.p_type != PT_LOAD) continue;
    if (ph.p_filesz > ph.p_memsz ||
        !file_range(size, ph.p_offset, ph.p_filesz) || ph.p_memsz > SIZE_MAX ||
        (ph.p_memsz && !mem_in_ram(ph.p_paddr, (size_t)ph.p_memsz))) {
      Loge("ELF segment %u is outside file/RAM bounds", i);
      return 0;
    }
    if (ph.p_memsz == 0) continue;
    has_load = true;
    if (ph.p_paddr + ph.p_memsz > end) end = ph.p_paddr + ph.p_memsz;
  }
  if (!has_load) {
    Loge("ELF has no nonempty loadable segment");
    return 0;
  }

  for (unsigned i = 0; i < eh.e_phnum; i++) {
    Elf64_Phdr ph = program_header(buf, &eh, i);
    if (ph.p_type != PT_LOAD || ph.p_memsz == 0) continue;
    uint8_t* dst = mem_host_ptr(ph.p_paddr);
    if (ph.p_filesz > 0) {
      memcpy(dst, (const uint8_t*)buf + ph.p_offset, (size_t)ph.p_filesz);
    }
    if (ph.p_memsz > ph.p_filesz) {
      memset(dst + ph.p_filesz, 0, (size_t)(ph.p_memsz - ph.p_filesz));
    }
  }

  Log("ELF image: entry=" FMT_PADDR ", %u phdrs", (paddr_t)eh.e_entry,
      eh.e_phnum);
  return end;
}

static paddr_t image_load_file(const char* path, paddr_t load_addr, bool elf) {
  void* buf = NULL;
  size_t size = 0;
  if (!host_read_file(path, &buf, &size)) return 0;
  paddr_t end =
      elf ? (is_elf(buf, size) ? image_load_elf_buf(buf, size, load_addr) : 0)
          : image_load_raw_buf(buf, size, load_addr);
  free(buf);
  return end;
}

paddr_t image_load_raw(const char* path, paddr_t load_addr) {
  return image_load_file(path, load_addr, false);
}

paddr_t image_load_elf(const char* path, paddr_t load_addr) {
  return image_load_file(path, load_addr, true);
}

paddr_t image_load(const char* path, paddr_t load_addr) {
  void* buf = NULL;
  size_t size = 0;
  if (!host_read_file(path, &buf, &size)) return 0;
  const bool elf = is_elf(buf, size);
  const paddr_t end = elf ? image_load_elf_buf(buf, size, load_addr)
                          : image_load_raw_buf(buf, size, load_addr);
  free(buf);
  return end;
}
