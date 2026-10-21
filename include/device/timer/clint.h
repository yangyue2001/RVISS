#ifndef RVISS_DEVICE_TIMER_CLINT_H
#define RVISS_DEVICE_TIMER_CLINT_H

#include <common.h>
#include <device/device.h>
#include <device/irq.h>

/*
 * CLINT：mtime / mtimecmp / msip。
 * mtime 每 128 个逻辑 tick 加 1。实例在 clint.c 的固定池里，
 * 结构体不对外。MMIO 基址由板卡 mem_region_add，不放在实例里。
 */

typedef struct Clint Clint;

Clint* clint_create(IrqLine* timer_irq, IrqLine* software_irq);
void clint_release(Clint* clint);
void clint_reset(Clint* clint);
#define CLINT_MMIO_SIZE UINT64_C(0x10000)

void clint_mmio(Clint* clint, MemObject* mmio);
void clint_advance(Clint* clint, uint64_t logical_ticks);
void clint_update(Clint* clint);
uint64_t clint_deadline(const Clint* clint);
uint64_t clint_mtime(const Clint* clint);
uint64_t clint_mtime_now(void);

#endif /* RVISS_DEVICE_TIMER_CLINT_H */
