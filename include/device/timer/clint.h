#ifndef RVISS_DEVICE_TIMER_CLINT_H
#define RVISS_DEVICE_TIMER_CLINT_H

#include <common.h>
#include <device/device.h>
#include <device/irq.h>

/*
 * CLINT 全局唯一。mtime 所有 hart 共用，每 128 个逻辑 tick 加 1。
 * msip、mtimecmp 和中断线按 hart 存放。当前只有 hart 0。
 * 全局只有一个。clint_init 分配并复位。板卡随后把中断线写进 mtimer_irq / msip_irq。
 * MMIO 基址不在这里。
 */

#define CLINT_NHART 1
#define CLINT_MMIO_SIZE UINT64_C(0x10000)

typedef struct Clint {
  IrqLine* mtimer_irq[CLINT_NHART];
  IrqLine* msip_irq[CLINT_NHART];
  uint64_t last_ticks;
  uint64_t mtime;
  uint64_t mtimecmp[CLINT_NHART];
  uint32_t msip[CLINT_NHART];
} Clint;

MemObject clint_init(void);

uint64_t clint_mtime_now(void);

#endif /* RVISS_DEVICE_TIMER_CLINT_H */
