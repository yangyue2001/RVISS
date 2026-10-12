#include <common.h>
#include <cpu/cpu.h>
#include <cpu/interrupt.h>
#include <device/irq.h>

#include "csr.h"  /* target 私有头：MSTATUS_* 位（测试允许看内部） */
#include "intr.h" /* target 私有头：riscv_query_intr / riscv_intr_reset */
#include "test.h"

/*
 * 中断输入线单元测试。
 * 验证设备/ISA 解耦后的接线语义：
 *   - 线拉高/拉低 → 只改变对应 mip 位的电平（不粘滞）
 *   - mstatus.MIE / mie 决定是否真的可响应
 * 这条路径以前是靠"设备直接写全局中断编号"实现的，现在走 IrqLine 对象。
 */

int main(void) {
  cpu_reset(0x80000000ull);

  IrqLine *mtip = cpu_irq_input(IRQ_MTIP);

  /* 线为低、MIE=0：没有可响应中断 */
  CHECK_EQ(riscv_query_intr(), 0);

  /* 拉高：MIE=0 时仍不响应，但 mip 应反映出挂起 */
  irq_raise(mtip);
  CHECK_EQ(riscv_query_intr(), 0);
  CHECK(cpu.mip & (1ull << IRQ_MTIP));

  /* 打开 MIE 与该中断使能 → 可响应 */
  cpu.mstatus |= MSTATUS_MIE;
  cpu.mie |= 1ull << IRQ_MTIP;
  CHECK_EQ(riscv_query_intr(), IRQ_BIT | (word_t)IRQ_MTIP);

  /* 拉低：必须立即变为不挂起（防"粘滞导致误报中断"） */
  irq_lower(mtip);
  CHECK_EQ(riscv_query_intr(), 0);
  CHECK(!(cpu.mip & (1ull << IRQ_MTIP)));

  /* 未接线的位不受影响：软件写 mip 的其他位应保留 */
  cpu.mip |= 1ull << IRQ_SEIP;
  CHECK(cpu.mip & (1ull << IRQ_SEIP));

  cpu_reset(0x80000000ull); /* 复位应清掉设备线快照 */
  CHECK_EQ(riscv_query_intr(), 0);
  CHECK(!(cpu.mip & (1ull << IRQ_MTIP)));

  TEST_MAIN_END();
}
