#include <common.h>
#include <device/device.h>
#include <device/ram.h>
#include <memory/paddr.h>
#include <string.h>

/*
 * 客户机物理访问只走 paddr_access。
 * 先查 MemRegion，没有就是 access fault。
 * 页表访问还要求 object.name 是 RAM。然后做 PMP，再调用 read/write。
 * 返回 false 也是 access fault。
 */

enum { BITS_PER_BYTE = 8 };

static bool region_name_is_ram(const MemRegion* region) {
  const char* name = region->object.name;
  return name != NULL && strcmp(name, RAM_OBJECT_NAME) == 0;
}

static word_t access_mask(word_t len) {
  if (len >= sizeof(word_t)) return ~UINT64_C(0);
  return (UINT64_C(1) << (len * BITS_PER_BYTE)) - 1;
}

MemResult paddr_access(MemRequest* req) {
  Assert(req != NULL, "paddr_access 缺少请求");
  Assert(req->len >= 1 && req->len <= sizeof(word_t), "paddr_access 长度非法");

  const MemRegion* region = mem_region_find(req->addr, req->len);
  if (region == NULL ||
      (mem_access_is_pte(req->type) && !region_name_is_ram(region)) ||
      !isa_pmp_permit(req->addr, req->len, req->type, req->privilege)) {
    return mem_result_fault(mem_access_fault(req->type), req->addr);
  }

  paddr_t offset = req->addr - region->base;
  if (mem_access_is_write(req->type)) {
    if (region->object.write == NULL ||
        !region->object.write(region->object.ctx, offset, (int)req->len,
                              req->data & access_mask(req->len))) {
      return mem_result_fault(mem_access_fault(req->type), req->addr);
    }
    return mem_result_ok();
  }

  word_t data = 0;
  if (region->object.read == NULL ||
      !region->object.read(region->object.ctx, offset, (int)req->len, &data)) {
    return mem_result_fault(mem_access_fault(req->type), req->addr);
  }
  return mem_result_data(data);
}
