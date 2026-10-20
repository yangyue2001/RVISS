#ifndef RVISS_CPU_INTERRUPT_H
#define RVISS_CPU_INTERRUPT_H

#include <common.h>

#define IRQ_BIT (UINT64_C(1) << 63)

/* 板级接线入口；设备只持有 IrqLine，不读取 CPU 状态。 */
struct IrqLine;
struct IrqLine* cpu_irq_input(int mip_bit);
void cpu_sync_irq_inputs(void);

#endif /* RVISS_CPU_INTERRUPT_H */
