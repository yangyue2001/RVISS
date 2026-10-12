#include "intr.h"

#include <common.h>
#include <cpu/cpu.h>
#include <cpu/interrupt.h>
#include <device/irq.h>
#include <util/log.h>

#include "cpu_state.h"
#include "csr.h"

/*
 * 中断输入与查询。
 *
 * 设备侧看到的是"输出线对象"（IrqLine），不认识 ISA；
 * 板子调用 cpu_irq_input(mip_bit) 把线接到 CPU 的某一位上 ——
 * "位 ↔ 设备"的映射只存在于板级接线处（等价 QEMU 的 sysbus_connect_irq）。
 *
 * 设备线的电平记录在 hw_pending，由 riscv_query_intr() 折进 mip。
 * 不直接写 cpu.mip 的原因：mip 同时是软件可写的 CSR，
 * 分开保存可以避免"软件写 mip 把设备线写没"。
 */

#define NR_IRQ_INPUTS 16

typedef struct {
  int mip_bit;
} IrqInput;

static IrqInput input_ctx[NR_IRQ_INPUTS];
static IrqLine input_lines[NR_IRQ_INPUTS];
static word_t hw_pending; /* 设备线电平快照 */
static word_t hw_mask;    /* 哪些 mip 位由设备线驱动 */

static void irq_input_set(void* ctx, bool level) {
  const IrqInput* in = ctx;
  const word_t mask = 1ull << in->mip_bit;
  if (level) {
    hw_pending |= mask;
  } else {
    hw_pending &= ~mask;
  }
  LogTrap("irq input (mip bit %d) -> %s", in->mip_bit, level ? "high" : "low");
}

IrqLine* cpu_irq_input(int mip_bit) {
  Assert(mip_bit >= 0 && mip_bit < NR_IRQ_INPUTS, "mip 位号非法: %d", mip_bit);
  input_ctx[mip_bit].mip_bit = mip_bit;
  input_lines[mip_bit].set = irq_input_set;
  input_lines[mip_bit].ctx = &input_ctx[mip_bit];
  hw_mask |= 1ull << mip_bit;
  return &input_lines[mip_bit];
}

void riscv_intr_reset(void) { hw_pending = 0; }

/* 同一目标级的优先顺序；目标 M 必须整体优先于目标 S。 */
static const int intr_priority[] = {
    IRQ_MEIP, IRQ_MSIP, IRQ_MTIP, IRQ_SEIP, IRQ_SSIP, IRQ_STIP,
};

word_t riscv_query_intr(void) {
  /*
   * 设备驱动的 mip 位必须反映"当前电平"，不能是粘滞的：
   * 直接 |= 会让设备拉低线之后 mip 里的位仍为 1，导致永远误报中断。
   * 所以先清掉这些位，再写入当前电平。
   */
  cpu_sync_irq_inputs();

  const word_t pending = cpu.mip & cpu.mie;
  word_t eligible = 0;
  if (cpu.priv < PRV_M || (cpu.priv == PRV_M && (cpu.mstatus & MSTATUS_MIE)))
    eligible = pending & ~cpu.mideleg;
  if (!eligible && (cpu.priv < PRV_S ||
      (cpu.priv == PRV_S && (cpu.mstatus & MSTATUS_SIE))))
    eligible = pending & cpu.mideleg;
  for (size_t i = 0; i < ARRLEN(intr_priority); i++) {
    const int line = intr_priority[i];
    if (eligible & (1ull << line)) return IRQ_BIT | (word_t)line;
  }
  return 0;
}

void cpu_sync_irq_inputs(void) {
  cpu.mip = (cpu.mip & ~hw_mask & ~SIE_MASK) |
            (cpu.mip_software & SIE_MASK) | hw_pending;
}
