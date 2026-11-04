#ifndef RVISS_DEVICE_INTC_PLIC_H
#define RVISS_DEVICE_INTC_PLIC_H

#include <device/device.h>

/*
 * PLIC。全局只有一个。plic_init 分配并复位。
 * 板卡把 external_irq[0] 接到 CPU 的机器外部中断，[1] 接到监管者外部中断。
 */

#define PLIC_NR_SOURCES 32 /* ID 0 保留；source 1..31 */
#define PLIC_MMIO_SIZE UINT64_C(0x4000000)
#define PLIC_NR_CONTEXTS 2

typedef struct Plic {
  IrqLine* external_irq[PLIC_NR_CONTEXTS];
  uint32_t priority[PLIC_NR_SOURCES];
  uint32_t enable_bits[PLIC_NR_CONTEXTS];
  uint32_t threshold[PLIC_NR_CONTEXTS];
  uint32_t levels;
  uint32_t pending;
  uint32_t busy;
} Plic;

MemObject plic_init(void);
/* 没有编进 PLIC 时这是空符号，调用方要先判断。 */
void plic_set_source(Plic* plic, unsigned source, bool level)
    __attribute__((weak));

#endif /* RVISS_DEVICE_INTC_PLIC_H */
