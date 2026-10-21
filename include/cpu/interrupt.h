#ifndef RVISS_CPU_INTERRUPT_H
#define RVISS_CPU_INTERRUPT_H

#include <common.h>
#include <cpu/cpu.h>

#define IRQ_BIT (UINT64_C(1) << 63)

/* 板级接线入口；设备只持有 IrqLine，不读取 CPU 状态。 */
struct IrqLine;
struct IrqLine* cpu_irq_input(int mip_bit);
/* 软件写 mip：只采纳 SSIP、STIP，SEIP 与 PLIC 电平取或。 */
void cpu_write_mip(CpuState* hart, word_t value);
/* 软件写 sip：只采纳已委托的 SSIP。 */
void cpu_write_sip(CpuState* hart, word_t value);
/* 外部装入完整 mip。SEIP 里的 1 记成软件位，再和 PLIC 电平取或。 */
void cpu_load_mip(CpuState* hart, word_t value);

#endif /* RVISS_CPU_INTERRUPT_H */
