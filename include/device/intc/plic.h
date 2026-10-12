#ifndef RVISS_DEVICE_INTC_PLIC_H
#define RVISS_DEVICE_INTC_PLIC_H

#include <common.h>
#include <device/device.h>
#include <device/irq.h>

/*
 * PLIC（最小实现）：单上下文（hart0 / M 模式）所需的
 * priority / pending / enable / threshold / claim-complete。
 * TODO(M4)：多上下文（每 hart 的 M/S 各一个）、优先级仲裁、真实的 claim 语义。
 */

#define PLIC_NR_SOURCES 8

typedef struct {
  MmioRegion mmio;
  IrqLine *meip_irq; /* 输出线：有可响应的外部中断时拉高（板子接到 CPU 的 MEIP） */
  uint32_t priority[PLIC_NR_SOURCES];
  uint32_t enable_bits;
  uint32_t threshold;
} PlicState;

void plic_init(PlicState *s);
void plic_reset(PlicState *s);

#endif /* RVISS_DEVICE_INTC_PLIC_H */
