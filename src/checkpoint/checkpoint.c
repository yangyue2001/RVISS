#include <checkpoint/checkpoint.h>
#include <common.h>
#include <cpu/cpu.h>
#include <host/host.h>
#include <memory/paddr.h>
#include <stdio.h>
#include <util/log.h>

/*
 * Checkpoint：把 CPU + 整块 RAM 存成文件（骨架版）。
 *
 * 格式：[头部 | CPU_state | RAM 全量数据]，头部含校验和。
 * TODO(M7)：稀疏存储（只存非零页）+ 设备状态（CLINT/PLIC）+ 压缩，
 *          见 docs/implementation_plan.md。
 */

#define CPT_MAGIC 0x5256495353435054ull /* "RVISSCPT" */
#define CPT_VERSION 2u

typedef struct {
  uint64_t magic;
  uint32_t version;
  uint32_t header_size;
  uint64_t ram_base;
  uint64_t ram_size;
  CPU_state cpu;
  uint64_t checksum; /* 固定头部 + CPU 状态 + RAM 内容的 FNV-1a */
} CptHeader;

static uint64_t fnv1a(uint64_t h, const void* data, size_t n) {
  const uint8_t* p = data;
  for (size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 0x100000001b3ull;
  }
  return h;
}

bool cpt_save(const char* path) {
  Logw("checkpoint is CPU/RAM only: event counters, platform time and devices are not saved");
  FILE* fp = fopen(path, "wb");
  if (fp == NULL) return false;

  CptHeader h = {
      .magic = CPT_MAGIC,
      .version = CPT_VERSION,
      .header_size = sizeof(CptHeader),
      .ram_base = mem_base,
      .ram_size = mem_size,
      .cpu = cpu,
  };
  h.checksum = fnv1a(0xcbf29ce484222325ull, &h, offsetof(CptHeader, checksum));
  h.checksum = fnv1a(h.checksum, mem_host_ptr(mem_base), mem_size);

  const bool ok = fwrite(&h, sizeof(h), 1, fp) == 1 &&
                  fwrite(mem_host_ptr(mem_base), 1, mem_size, fp) == mem_size;
  fclose(fp);

  if (ok)
    Log("checkpoint 已保存: %s (" FMT_PADDR " + %zu MiB)", path, mem_base,
        mem_size >> 20);
  return ok;
}

bool cpt_load(const char* path) {
  FILE* fp = fopen(path, "rb");
  if (fp == NULL) return false;

  CptHeader h;
  if (fread(&h, sizeof(h), 1, fp) != 1) {
    fclose(fp);
    return false;
  }

  if (h.magic != CPT_MAGIC || h.version != CPT_VERSION ||
      h.header_size != sizeof(CptHeader)) {
    Loge("checkpoint 格式不匹配: %s", path);
    fclose(fp);
    return false;
  }
  if (h.ram_base != mem_base || h.ram_size != mem_size) {
    Loge("checkpoint RAM 布局不一致（文件 " FMT_PADDR "+%luM，当前 " FMT_PADDR
         "+%luM）",
         (paddr_t)h.ram_base, h.ram_size >> 20, mem_base, mem_size >> 20);
    fclose(fp);
    return false;
  }

  /* Verify into a temporary mapping first. A corrupt checkpoint must not
   * partially replace the running machine before its checksum is known. */
  uint8_t* ram_copy = host_mmap_anon(mem_size);
  if (ram_copy == NULL || fread(ram_copy, 1, mem_size, fp) != mem_size) {
    if (ram_copy != NULL) host_munmap(ram_copy, mem_size);
    fclose(fp);
    return false;
  }
  fclose(fp);

  uint64_t sum =
      fnv1a(0xcbf29ce484222325ull, &h, offsetof(CptHeader, checksum));
  sum = fnv1a(sum, ram_copy, mem_size);
  if (sum != h.checksum) {
    Loge("checkpoint 校验和错误: %s", path);
    host_munmap(ram_copy, mem_size);
    return false;
  }

  memcpy(mem_host_ptr(mem_base), ram_copy, mem_size);
  host_munmap(ram_copy, mem_size);
  cpu = h.cpu;
  Logw("checkpoint is CPU/RAM only: continuation equivalence is not supported");
  Log("checkpoint 已恢复: %s (pc = " FMT_WORD ")", path, cpu.pc);
  return true;
}
