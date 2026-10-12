#ifndef RVISS_DEVICE_DEVICE_H
#define RVISS_DEVICE_DEVICE_H

#include <common.h>
#include <device/irq.h>

/*
 * 设备框架：MMIO 区域 + 总线 + 设备节拍。
 * 本头只放框架，不随设备数量增长 —— 具体设备见 clint.h / uart16550.h / plic.h。
 *
 * 三条规则（对照 QEMU 的设备模型）：
 *   1. 设备不知道自己的地址：MmioRegion 只描述"多大、怎么读写"，
 *      由板子用 mmio_map() 挂到地址上（等价 memory_region_add_subregion）。
 *   2. 设备状态放实例结构体，经 opaque 传入回调（不用文件级全局变量，
 *      这样同一设备可以有多个实例）。
 *   3. 设备之间互不认识，也不认识 ISA：中断用 IrqLine 对象（见 irq.h）。
 */

/* 板级选择共享时钟；time CSR 只读此回调，不访问设备内部布局。 */
void device_set_time_reader(word_t (*reader)(void *), void *opaque);
word_t device_time(void);

typedef struct MmioRegion {
  const char *name;
  paddr_t size; /* 设备只回答"我多大"；地址由板子决定 */
  word_t (*read)(void *opaque, paddr_t offset, int len);
  void   (*write)(void *opaque, paddr_t offset, int len, word_t data);
  bool (*accepts)(void *opaque, paddr_t offset, int len, bool write);
  void   *opaque; /* 指向设备实例（如 ClintState） */
} MmioRegion;

/*
 * 总线：维护 (地址 → 区域) 映射表，并知道自己服务的 RAM 范围
 * （用于在映射时拒绝"压在 RAM 上"和"互相重叠"的错误配置）。
 * 这些错误必须当场暴露 —— 静默的地址冲突是最难查的一类 bug。
 */
void bus_init(paddr_t ram_base, size_t ram_size);
bool mmio_map(MmioRegion *region, paddr_t base); /* 失败返回 false 并打印原因 */
bool mmio_dispatch_read(paddr_t addr, int len, word_t *data);
bool mmio_dispatch_write(paddr_t addr, int len, word_t data);

/*
 * 设备节拍：引擎只调这一个与具体设备无关的入口。
 * 设备在自己的 init 里注册（如 CLINT 注册 mtime 推进），
 * 于是引擎完全不知道"机器上有什么设备"。
 */
typedef void (*DeviceTickFn)(void *opaque, uint64_t logical_ticks);
typedef uint64_t (*DeviceDeadlineFn)(void *opaque);
bool device_register_tick(DeviceTickFn fn, void *opaque);
bool device_register_deadline(DeviceDeadlineFn fn, void *opaque);
void devices_tick(uint64_t logical_ticks);
uint64_t devices_next_deadline(void);

#endif /* RVISS_DEVICE_DEVICE_H */
