#ifndef RVISS_DEVICE_DEVICE_H
#define RVISS_DEVICE_DEVICE_H

#include <common.h>
#include <device/irq.h>

/*
 * MemObject 只有名字和读写方法，不保存基址。
 * 板卡把基址、长度和 MemObject 放进 MemRegion。RAM 也是一个区域。
 * 页表访问靠 object.name 识别 RAM，不在区域上另加标志。
 */

typedef struct MemObject {
  const char* name;
  void* ctx;
  bool (*read)(void* ctx, paddr_t offset, int len, word_t* data);
  bool (*write)(void* ctx, paddr_t offset, int len, word_t data);
} MemObject;

typedef struct MemRegion {
  paddr_t base;
  paddr_t size;
  MemObject object;
} MemRegion;

void device_set_time_reader(word_t (*reader)(void* opaque), void* opaque);
word_t device_time(void);

/* 拷贝 object。name 和 ctx 必须在区域存活期间保持有效。 */
bool mem_region_add(paddr_t base, paddr_t size, const MemObject* object);
const MemRegion* mem_region_find(paddr_t addr, word_t len);

#endif /* RVISS_DEVICE_DEVICE_H */
