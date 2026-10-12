#ifndef RVISS_DEVICE_TIMER_CLINT_H
#define RVISS_DEVICE_TIMER_CLINT_H

#include <common.h>
#include <device/device.h>
#include <device/irq.h>

/*
 * CLINT：mtime / mtimecmp / msip。
 *
 * mtime 由平台逻辑 tick 驱动（每 128 ticks +1），不读宿主时钟；
 * 客户机 cycle/instret 的写入不改变平台节拍。
 *
 * 实例状态由板子提供存储（QEMU 的板子也这么做：static PLICState plic; ...），
 * 所以同一设备可以有多个实例，且单元测试可以随意构造。
 */

typedef struct {
  MmioRegion mmio;
  IrqLine *mtimer_irq; /* 输出线：mtime >= mtimecmp 时拉高（板子接到 CPU 的 MTIP） */
  IrqLine *msip_irq;   /* 输出线：MSIP 置位时拉高（板子接到 MSIP） */
  uint64_t mtime;
  uint64_t mtimecmp;
  uint32_t msip;
} ClintState;

/* 初始化实例（置 mmio 回调、复位寄存器、注册设备节拍）；不涉及地址 */
word_t clint_time(void *opaque);
void clint_init(ClintState *s);
void clint_reset(ClintState *s);

#endif /* RVISS_DEVICE_TIMER_CLINT_H */
