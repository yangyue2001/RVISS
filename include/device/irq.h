#ifndef RVISS_DEVICE_IRQ_H
#define RVISS_DEVICE_IRQ_H

#include <common.h>

/*
 * 中断线对象（QEMU 的 qemu_irq 最小版）。
 *
 * 设备只持有"输出线"，在状态变化时 raise/lower；
 * 对端是谁、接到 CPU 的哪一位，只有**板子**（machine）知道。
 * 因此设备层完全不认识 RISC-V 的中断编号 —— 这是设备/ISA 解耦的关键一步。
 *
 * 接线由板子完成：line = cpu_irq_input(IRQ_MTIP); clint.mtimer_irq = line;
 */

typedef struct IrqLine {
  void (*set)(void *ctx, bool level);
  void *ctx;
} IrqLine;

static inline void irq_raise(IrqLine *line) {
  if (line != NULL) line->set(line->ctx, true);
}

static inline void irq_lower(IrqLine *line) {
  if (line != NULL) line->set(line->ctx, false);
}

#endif /* RVISS_DEVICE_IRQ_H */
