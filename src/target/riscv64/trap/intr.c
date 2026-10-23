#include <common.h>
#include <cpu/cpu.h>
#include <cpu/interrupt.h>
#include <device/irq.h>
#include <util/log.h>

#include "csr.h"
#include "trap.h"

/*
 * 中断输入与查询。
 *
 * 设备线直接改 mip 的对应位。MEIP、MTIP、MSIP 只由设备驱动。
 * mip.SEIP 只存软件位，PLIC 电平在 hw_seip。读出和中断判断取或。
 * SSIP、STIP 和软件 SEIP 只由 CSR 写修改。
 */

#define NR_IRQ_INPUTS 16

typedef struct {
  int mip_bit;
} IrqInput;

static IrqInput input_ctx[NR_IRQ_INPUTS];
static IrqLine input_lines[NR_IRQ_INPUTS];

word_t cpu_read_mip(const CpuState* hart) {
  word_t mip = hart->mip;
  if (hart->hw_seip) mip |= MIP_SEIP;
  return mip;
}

static void irq_input_set(void* ctx, bool level) {
  const IrqInput* in = ctx;
  const word_t mask = 1ull << in->mip_bit;
  if (in->mip_bit == INT_SEI) {
    cpu.hw_seip = level;
  } else if (level) {
    cpu.mip |= mask;
  } else {
    cpu.mip &= ~mask;
  }
  LogTrap("irq input (mip bit %d) -> %s", in->mip_bit, level ? "high" : "low");
}

IrqLine* cpu_irq_input(int mip_bit) {
  Assert(mip_bit >= 0 && mip_bit < NR_IRQ_INPUTS, "mip 位号非法: %d", mip_bit);
  input_ctx[mip_bit].mip_bit = mip_bit;
  input_lines[mip_bit].set = irq_input_set;
  input_lines[mip_bit].ctx = &input_ctx[mip_bit];
  return &input_lines[mip_bit];
}

/* 同一目标级的优先顺序；目标 M 必须整体优先于目标 S。 */
static const int intr_priority[] = {
    INT_MEI, INT_MSI, INT_MTI, INT_SEI, INT_SSI, INT_STI,
};

word_t riscv_query_intr(void) {
  const word_t pending = cpu_read_mip(&cpu) & cpu.mie;  // 有挂起的中断
  word_t eligible = 0;
  if (cpu.priv < PRV_M ||
      (cpu.priv == PRV_M && (cpu.mstatus & MSTATUS_MIE)))  // M态中断检查
    eligible = pending & ~cpu.mideleg;

  if (!eligible &&
      (cpu.priv < PRV_S ||
       (cpu.priv == PRV_S && (cpu.mstatus & MSTATUS_SIE))))  // S态中断检查
    eligible = pending & cpu.mideleg;

  for (size_t i = 0; i < ARRLEN(intr_priority); i++) {
    const int line = intr_priority[i];
    if (eligible & (1ull << line)) return MCAUSE_INTERRUPT | (word_t)line;
  }

  return 0;
}

bool riscv_wait_resume(void) { return (cpu_read_mip(&cpu) & cpu.mie) != 0; }
