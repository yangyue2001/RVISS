#ifndef RVISS_DEVICE_DEVICE_H
#define RVISS_DEVICE_DEVICE_H

#include <common.h>
#include <device/irq.h>

/*
 * MemObject 是一块可映射对象，不保存基址。
 * 具体设备的 init 分配 ctx，并填好 read/write。复位在 init 里完成。
 * update、advance 可以为空。板卡按登记顺序调用 advance（推进逻辑时间）
 * 和 update（刷新输出）。
 * 页表访问靠 object.name 识别 RAM，不在区域上另加标志。
 */

typedef struct MemObject {
  const char* name;
  void* ctx;
  bool (*read)(void* ctx, paddr_t offset, int len, word_t* data);
  bool (*write)(void* ctx, paddr_t offset, int len, word_t data);
  void (*update)(void* ctx);
  void (*advance)(void* ctx, uint64_t logical_ticks);
} MemObject;

typedef struct MemRegion {
  paddr_t base;
  paddr_t size;
  MemObject object;
} MemRegion;

/* 拷贝 object。name 和 ctx 必须在区域存活期间保持有效。 */
bool mem_region_add(paddr_t base, paddr_t size, const MemObject* object);
const MemRegion* mem_region_find(paddr_t addr, word_t len);
int mem_region_count(void);
const MemRegion* mem_region_at(int index);

#endif /* RVISS_DEVICE_DEVICE_H */
