#include <common.h>
#include <cpu/cpu.h>
#include <cpu/interrupt.h>
#include <device/irq.h>
#include <device/device.h>
#include <memory/paddr.h>
#include <util/state.h>
#include "../../src/engine/engine.h"

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

#define BASE UINT64_C(0x80000000)

static void test_privilege_matrix(void) {
  CHECK(mem_init(BASE,256)); bus_init(BASE,256); engine_init();
  CHECK(paddr_write(BASE,4,0x00000013));
  const unsigned levels[]={PRV_U,PRV_S,PRV_M};
  const unsigned bits[]={IRQ_MSIP,IRQ_MTIP,IRQ_MEIP,IRQ_SSIP,IRQ_STIP,IRQ_SEIP};
  const bool m_allowed[3][4]={{1,1,1,1},{1,1,1,1},{0,1,0,1}};
  const bool s_allowed[3][4]={{1,1,1,1},{0,0,1,1},{0,0,0,0}};
  for(unsigned p=0;p<3;p++) for(unsigned i=0;i<6;i++)
  for(unsigned delegated=0;delegated<2;delegated++) for(unsigned ie=0;ie<4;ie++)
  for(unsigned enabled=0;enabled<2;enabled++) {
    cpu_reset(BASE); sim_reset(); cpu.mtvec=BASE+64; cpu.stvec=BASE+128;
    cpu.priv=levels[p]; cpu.mstatus|=((ie&1)?MSTATUS_MIE:0)|((ie&2)?MSTATUS_SIE:0);
    cpu.mideleg=delegated?(SIE_MASK&(1ull<<bits[i])):0;
    cpu.mie=enabled?(1ull<<bits[i]):0;
    IrqLine *line=cpu_irq_input(bits[i]); irq_raise(line);
    bool to_s=delegated && i>=3;
    bool eligible=enabled && (to_s?s_allowed[p][ie]:m_allowed[p][ie]);
    CHECK_EQ(riscv_query_intr(),eligible?IRQ_BIT|bits[i]:0);
    StepResult r; CHECK(engine_step(&r));
    CHECK_EQ(r.outcome,eligible?STEP_INTERRUPT:STEP_RETIRED);
    CHECK_EQ(r.instruction_attempted,!eligible); CHECK_EQ(r.retired,!eligible);
    CHECK_EQ(sim.attempts,!eligible); CHECK_EQ(sim.instret,!eligible);
    CHECK_EQ(sim.event_seq,1); CHECK_EQ(sim.logical_ticks,1);
    CHECK_EQ(cpu.pc,eligible?(to_s?BASE+128:BASE+64):BASE+4);
    CHECK_EQ(cpu.priv,eligible?(to_s?PRV_S:PRV_M):levels[p]);
    if(eligible) {
      CHECK_EQ(r.trap.cause,IRQ_BIT|bits[i]); CHECK_EQ(r.trap.epc,BASE);
      CHECK_EQ(r.trap.tval,0); CHECK(!r.memory.valid && !r.instruction_valid);
      CHECK_EQ(r.raw_bytes,0); CHECK_EQ(cpu.gpr[5],0);
    }
    irq_lower(line); CHECK_EQ(riscv_query_intr(),0);
    CHECK(!(cpu.mip&(1ull<<bits[i])));
  }
  /* 全目标 M 的顺序是 MEI > MSI > MTI > SEI > SSI > STI。 */
  cpu_reset(BASE); cpu.priv=PRV_U; cpu.mie=MIE_MASK;
  const unsigned priority[]={IRQ_MEIP,IRQ_MSIP,IRQ_MTIP,IRQ_SEIP,IRQ_SSIP,IRQ_STIP};
  for(unsigned i=0;i<6;i++) irq_raise(cpu_irq_input(priority[i]));
  for(unsigned i=0;i<6;i++) {
    CHECK_EQ(riscv_query_intr(),IRQ_BIT|priority[i]); irq_lower(cpu_irq_input(priority[i]));
  }
  CHECK_EQ(riscv_query_intr(),0);
  /* 目标 M 的 SSI 即使同位优先级低于目标 S 的 SEI，仍应先响应。 */
  cpu_reset(BASE); cpu.priv=PRV_S; cpu.mie=MIE_SSIE|MIE_SEIE;
  cpu.mideleg=MIP_SEIP; cpu.mstatus|=MSTATUS_SIE;
  irq_raise(cpu_irq_input(IRQ_SSIP)); irq_raise(cpu_irq_input(IRQ_SEIP));
  CHECK_EQ(riscv_query_intr(),IRQ_BIT|IRQ_SSIP);
  irq_lower(cpu_irq_input(IRQ_SSIP)); CHECK_EQ(riscv_query_intr(),IRQ_BIT|IRQ_SEIP);
  /* 未映射 PC 不应先取指，中断是独立边界事件。 */
  cpu_reset(0x40000000); sim_reset(); cpu.priv=PRV_U;
  cpu.mie=MIE_MTIE; cpu.mtvec=BASE+64; irq_raise(cpu_irq_input(IRQ_MTIP));
  StepResult r; CHECK(engine_step(&r)); CHECK_EQ(r.outcome,STEP_INTERRUPT);
  CHECK(!r.instruction_attempted && !r.memory.valid); CHECK_EQ(cpu.mepc,0x40000000);
  CHECK_EQ(sim.attempts,0); CHECK_EQ(sim.instret,0); CHECK_EQ(sim.event_seq,1);
  cpu_reset(BASE); mem_shutdown();
}

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
  CHECK(csr_write(CSR_MIP, 1ull << IRQ_SEIP));
  CHECK(cpu.mip & (1ull << IRQ_SEIP));

  cpu_reset(0x80000000ull); /* 复位应清掉设备线快照 */
  CHECK_EQ(riscv_query_intr(), 0);
  CHECK(!(cpu.mip & (1ull << IRQ_MTIP)));

  test_privilege_matrix();
  TEST_MAIN_END();
}
