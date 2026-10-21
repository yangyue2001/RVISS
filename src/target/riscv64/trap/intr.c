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
 * SEIP 是 PLIC 电平和软件可写位的或，任一侧变化都立刻写回 mip。
 * SSIP、STIP 只由 CSR 写修改。
 */

#define NR_IRQ_INPUTS 16

typedef struct {
  int mip_bit;
} IrqInput;

static IrqInput input_ctx[NR_IRQ_INPUTS];
static IrqLine input_lines[NR_IRQ_INPUTS];
static bool seip_level;
static bool seip_soft;

static void publish_seip(CpuState* hart) {
  if (seip_level || seip_soft) hart->mip |= MIP_SEIP;
  else hart->mip &= ~MIP_SEIP;
}

static void irq_input_set(void* ctx, bool level) {
  const IrqInput* in = ctx;
  const word_t mask = 1ull << in->mip_bit;
  if (in->mip_bit == INT_SEI) {
    seip_level = level;
    publish_seip(&cpu);
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

void cpu_write_mip(CpuState* hart, word_t value) {
  const word_t software = MIP_SSIP | MIP_STIP;
  hart->mip = (hart->mip & ~software) | (value & software);
  seip_soft = (value & MIP_SEIP) != 0;
  publish_seip(hart);
}

void cpu_write_sip(CpuState* hart, word_t value) {
  const word_t mask = hart->mideleg & MIP_SSIP;
  hart->mip = (hart->mip & ~mask) | (value & mask);
}

void cpu_load_mip(CpuState* hart, word_t value) {
  hart->mip = value & ~MIP_SEIP;
  seip_soft = (value & MIP_SEIP) != 0;
  publish_seip(hart);
}

void riscv_intr_reset(void) {
  seip_level = false;
  seip_soft = false;
  cpu.mip &= ~(MIP_MSIP | MIP_MTIP | MIP_MEIP | MIP_SEIP);
}

/* 同一目标级的优先顺序；目标 M 必须整体优先于目标 S。 */
static const int intr_priority[] = {
    INT_MEI, INT_MSI, INT_MTI, INT_SEI, INT_SSI, INT_STI,
};

word_t riscv_query_intr(void) {
  const word_t pending = cpu.mip & cpu.mie;  // 有挂起的中断
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
    if (eligible & (1ull << line)) return IRQ_BIT | (word_t)line;
  }

  return 0;
}

bool riscv_wait_resume(void) { return (cpu.mip & cpu.mie) != 0; }
