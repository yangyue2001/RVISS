#ifndef RVISS_CPU_INTERRUPT_H
#define RVISS_CPU_INTERRUPT_H

#include <common.h>
#include <cpu/cpu.h>

/* 板级接线入口；设备只持有 IrqLine，不读取 CPU 状态。 */
struct IrqLine;
struct IrqLine* cpu_irq_input(int mip_bit);
/* 可见 mip。SEIP 是软件位与 hw_seip 的或。 */
word_t cpu_read_mip(const CpuState* hart);

#endif /* RVISS_CPU_INTERRUPT_H */
