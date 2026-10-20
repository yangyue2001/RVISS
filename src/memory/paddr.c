#include <common.h>
#include <device/device.h>
#include <host/host.h>
#include <memory/paddr.h>
#include <util/log.h>

/*
 * 物理地址空间：RAM 和 MMIO。
 * 客户机只从 paddr_access 进来，检查完成后再调用 phys_transfer。
 * loader/debug 的 paddr_try_* 复用同一次传输，但不做 PMP 和地址宽度检查。
 */

enum { BITS_PER_BYTE = 8 };

paddr_t mem_base;
size_t mem_size;

static uint8_t* ram;

bool mem_init(paddr_t base, size_t size) {
  if (size == 0 || size > UINT64_MAX - base) return false;
  ram = host_mmap_anon(size);
  if (ram == NULL) {
    Loge("内存分配失败：%zu MiB", size >> 20);
    return false;
  }
  mem_base = base;
  mem_size = size;
  memset(ram, 0, size);
  Log("memory: %zu MiB @ " FMT_PADDR, size >> 20, base);
  return true;
}

void mem_shutdown(void) {
  if (ram != NULL) {
    host_munmap(ram, mem_size);
    ram = NULL;
  }
}

uint8_t* mem_host_ptr(paddr_t addr) {
  Assert(mem_in_ram(addr, 1), "地址 " FMT_PADDR " 不在 RAM 内", addr);
  return ram + (addr - mem_base);
}

static word_t read_le(const uint8_t* bytes, int len) {
  word_t value = 0;
  for (int i = 0; i < len; i++) {
    value |= (word_t)bytes[i] << (BITS_PER_BYTE * i);
  }
  return value;
}

static void write_le(uint8_t* bytes, int len, word_t value) {
  for (int i = 0; i < len; i++) {
    bytes[i] = (uint8_t)(value >> (BITS_PER_BYTE * i));
  }
}

static bool try_len_ok(int len) {
  return len == 1 || len == 2 || len == 4 || len == 8;
}

static bool range_fits(paddr_t addr, word_t len) {
  return len > 0 && addr <= UINT64_MAX - (len - 1);
}

static word_t access_mask(word_t len) {
  if (len >= sizeof(word_t)) return ~UINT64_C(0);
  return (UINT64_C(1) << (len * BITS_PER_BYTE)) - 1;
}

/* RAM 和 MMIO 在这里汇合。调用前必须已经做完客户机检查。 */
static bool phys_transfer(paddr_t addr, word_t len, MemAccessType type,
                          word_t* data, bool* mmio) {
  if (mmio != NULL) *mmio = false;
  if (data == NULL || len == 0 || len > sizeof(word_t) ||
      !range_fits(addr, len)) {
    return false;
  }
  if (mem_in_ram(addr, (size_t)len)) {
    uint8_t* host = ram + (addr - mem_base);
    if (mem_access_is_write(type))
      write_le(host, (int)len, *data);
    else
      *data = read_le(host, (int)len);
    return true;
  }
  if (mem_access_is_write(type)) {
    if (!mmio_dispatch_write(addr, (int)len, *data)) return false;
    if (mmio != NULL) *mmio = true;
    LogMem("mmio write " FMT_PADDR " len=%u <- 0x%lx", addr, (unsigned)len,
           *data);
    return true;
  }
  word_t value = 0;
  if (!mmio_dispatch_read(addr, (int)len, &value)) return false;
  *data = value;
  if (mmio != NULL) *mmio = true;
  LogMem("mmio read  " FMT_PADDR " len=%u -> 0x%lx", addr, (unsigned)len,
         value);
  return true;
}

MemResult paddr_access(MemRequest* req) {
  Assert(req != NULL, "paddr_access 缺少请求");
  Assert(req->len >= 1 && req->len <= sizeof(word_t), "paddr_access 长度非法");
  if (!isa_pa_format_ok(req->addr, req->len) ||
      !isa_pmp_permit(req->addr, req->len, req->type, req->privilege)) {
    return mem_result_fault(mem_access_fault(req->type), req->addr);
  }

  word_t data = mem_access_is_write(req->type) ? req->data & access_mask(req->len) : 0;
  if (!phys_transfer(req->addr, req->len, req->type, &data, NULL)) {
    return mem_result_fault(mem_access_fault(req->type), req->addr);
  }
  if (mem_access_is_write(req->type)) return mem_result_ok();
  return mem_result_data(data);
}

bool paddr_try_read(paddr_t addr, int len, word_t* data, bool* mmio) {
  if (mmio != NULL) *mmio = false;
  if (!try_len_ok(len) || !range_fits(addr, (word_t)len)) return false;
  return phys_transfer(addr, (word_t)len, kMemRead, data, mmio);
}

word_t paddr_read(paddr_t addr, int len) {
  word_t data = 0;
  if (!paddr_try_read(addr, len, &data, NULL)) {
    LogMem("debug read unmapped " FMT_PADDR " (len=%d)", addr, len);
  }
  return data;
}

bool paddr_try_write(paddr_t addr, int len, word_t data, bool* mmio) {
  if (mmio != NULL) *mmio = false;
  if (!try_len_ok(len) || !range_fits(addr, (word_t)len)) return false;
  return phys_transfer(addr, (word_t)len, kMemWrite, &data, mmio);
}

bool paddr_write(paddr_t addr, int len, word_t data) {
  return paddr_try_write(addr, len, data, NULL);
}
