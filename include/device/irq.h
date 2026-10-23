#ifndef RVISS_DEVICE_IRQ_H
#define RVISS_DEVICE_IRQ_H

#include <common.h>

/*
 * 中断线对象。设备只持有输出线，不知道对端是 CPU 的哪一位。
 * 板子在外设 init 之后把线写进设备：clint->mtimer_irq[0] = cpu_irq_input(INT_MTI)。
 */

typedef struct IrqLine {
  void (*set)(void* ctx, bool level);
  void* ctx;
} IrqLine;

static inline void irq_raise(IrqLine* line) {
  if (line != NULL) line->set(line->ctx, true);
}

static inline void irq_lower(IrqLine* line) {
  if (line != NULL) line->set(line->ctx, false);
}

#endif /* RVISS_DEVICE_IRQ_H */
