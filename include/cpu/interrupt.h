#ifndef RVISS_CPU_INTERRUPT_H
#define RVISS_CPU_INTERRUPT_H

#include <common.h>

/* RV64 M/S/U cause 编号：Privileged Architecture v1.12 的 mcause 表。
 * 编号是编码定义，不表示相应 MMU/访存异常路径已经实现。
 * 不包含 H 或其它可选扩展的 cause。
 */
enum {
  EX_IAM = 0,  /* instruction address misaligned */
  EX_IAF = 1,  /* instruction access fault */
  EX_II  = 2,  /* illegal instruction */
  EX_BP  = 3,  /* breakpoint */
  EX_LAM = 4,  /* load address misaligned */
  EX_LAF = 5,  /* load access fault */
  EX_SAM = 6,  /* store/AMO address misaligned */
  EX_SAF = 7,  /* store/AMO access fault */
  EX_ECU = 8,  /* environment call from U */
  EX_ECS = 9,  /* environment call from S */
  EX_ECM = 11, /* environment call from M */
  EX_IPF = 12, /* instruction page fault */
  EX_LPF = 13, /* load page fault */
  EX_SPF = 15, /* store/AMO page fault */
};

/* 中断编号同时是 mie/mip 的位号；cause 还需置 RV64 的最高位。 */
enum {
  IRQ_SSIP = 1,
  IRQ_MSIP = 3,
  IRQ_STIP = 5,
  IRQ_MTIP = 7,
  IRQ_SEIP = 9,
  IRQ_MEIP = 11,
};

#define IRQ_BIT (UINT64_C(1) << 63)

/* 板级接线入口；设备只持有 IrqLine，不读取 CPU 状态。 */
struct IrqLine;
struct IrqLine *cpu_irq_input(int mip_bit);
void cpu_sync_irq_inputs(void);

#endif /* RVISS_CPU_INTERRUPT_H */
