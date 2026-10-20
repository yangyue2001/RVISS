#ifndef RVISS_DEVICE_DEVICE_H
#define RVISS_DEVICE_DEVICE_H

#include <common.h>
#include <device/irq.h>

/*
 * 外设对 CPU 只暴露一段 MMIO。状态留在设备 .c 的实例池里，
 * 板子只保存不透明指针。基址和映射记录由总线掌握，不放进外设结构体。
 */

void device_set_time_reader(word_t (*reader)(void* opaque), void* opaque);
word_t device_time(void);

typedef struct MemObject {
  const char* name;
  paddr_t size;
  void* ctx;
  bool (*read)(void* ctx, paddr_t offset, int len, word_t* data);
  bool (*write)(void* ctx, paddr_t offset, int len, word_t data);
} MemObject;

void bus_init(paddr_t ram_base, size_t ram_size);
/* 拷贝 object。调用方的 MemObject 可以是临时量；name 和 ctx 必须保持有效。 */
bool mmio_map(const MemObject* object, paddr_t base);
bool mmio_dispatch_read(paddr_t addr, int len, word_t* data);
bool mmio_dispatch_write(paddr_t addr, int len, word_t data);

/* bus_init 时调用，用来撤销板卡登记的实例。 */
bool device_add_bus_reset(void (*fn)(void));

#endif /* RVISS_DEVICE_DEVICE_H */
