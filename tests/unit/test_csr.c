#include <common.h>
#include <cpu/cpu.h>
#include <cpu/csr_list.h>
#include <device/device.h>
#include <device/irq.h>
#include <memory/paddr.h>
#include <util/state.h>
#include "../../src/engine/engine.h"

#include "csr.h" /* target 私有：csr_read / csr_write */
#include "test.h"

/*
 * CSR 访问语义单测。
 *
 * 重点验证"从地址推导特权级/只读"这条设计（它取代了以前手工填的
 * min_priv / writable 字段）：
 *   - M 模式 CSR 在 U/S 模式必须非法；S 模式 CSR 在 U 模式非法；M 模式通吃
 *   - 只读 CSR（0xf1x 段）写必须非法（由 csr[11:10]==3 自动判定，不再靠字段）
 *   - 视图语义：写 sstatus 不能覆盖 mstatus 的 M 侧位；sie 与 mip 互不影响
 */

#define BASE UINT64_C(0x80000000)

static StepResult instruction(unsigned address, unsigned funct3, unsigned rd, unsigned rs1) {
  uint32_t raw = (address << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | 0x73;
  CHECK(paddr_write(cpu.pc, 4, raw));
  StepResult r;
  CHECK(engine_step(&r));
  return r;
}

static void setup(void) {
  cpu_reset(BASE);
  sim_reset();
  cpu.mtvec = BASE + 128;
  cpu.gpr[5] = 0xdeadbeef;
}

static void test_instruction_rules(void) {
  CHECK(mem_init(BASE, 256));
  bus_init(BASE, 256);
  engine_init();
  /* 写请求取决于寄存器编号，而不是其运行时值。错误不能提交 rd。 */
  const unsigned functions[] = {1,2,3,5,6,7};
  for (size_t i=0; i<ARRLEN(functions); i++) {
    setup();
    StepResult r=instruction(CSR_MHARTID,functions[i],5,6);
    CHECK(!r.retired && r.trap.valid && !r.register_write);
    CHECK_EQ(cpu.mcause,2); CHECK_EQ(cpu.gpr[5],0xdeadbeef);
  }
  for (unsigned f=2; f<=7; f++) {
    if (f==4 || f==5) continue;
    setup();
    CHECK(instruction(CSR_MHARTID,f,5,0).retired);
    CHECK_EQ(cpu.gpr[5],0); CHECK_EQ(cpu.mhartid,0);
  }
  /* RW rd=rs1 必须先捕获源。RW x0 仍写；RS/RC 源 x0 不写。 */
  setup(); cpu.mscratch=7;
  CHECK(instruction(CSR_MSCRATCH,1,5,5).retired);
  CHECK_EQ(cpu.mscratch,0xdeadbeef); CHECK_EQ(cpu.gpr[5],7);
  setup(); cpu.mscratch=7;
  CHECK(instruction(CSR_MSCRATCH,1,0,0).retired); CHECK_EQ(cpu.mscratch,0);
  setup(); cpu.mscratch=7;
  CHECK(instruction(CSR_MSCRATCH,2,5,0).retired); CHECK_EQ(cpu.mscratch,7);
  CHECK_EQ(cpu.gpr[5],7);
  setup(); cpu.mscratch=7;
  CHECK(instruction(CSR_MSCRATCH,7,5,3).retired); CHECK_EQ(cpu.mscratch,4);
  CHECK_EQ(cpu.gpr[5],7);

  setup(); cpu.gpr[6]=UINT64_MAX;
  CHECK(instruction(CSR_MSTATUS,1,5,6).retired);
  CHECK_EQ(cpu.gpr[5],UINT64_C(0xa00000000));
  CHECK_EQ(cpu.mstatus,UINT64_C(0x8000000a007e79aa));
  setup(); cpu.gpr[6]=2ull<<11;
  CHECK(instruction(CSR_MSTATUS,1,0,6).retired);
  CHECK_EQ(cpu.mstatus,UINT64_C(0xa00000000));
  setup(); cpu.gpr[6]=UINT64_MAX;
  CHECK(instruction(CSR_MEDELEG,1,0,6).retired); CHECK_EQ(cpu.medeleg,0xb3ff);
  setup(); cpu.gpr[6]=UINT64_MAX;
  CHECK(instruction(CSR_MIDELEG,1,0,6).retired); CHECK_EQ(cpu.mideleg,0x222);
  setup(); cpu.gpr[6]=UINT64_MAX;
  CHECK(instruction(CSR_MIE,1,0,6).retired); CHECK_EQ(cpu.mie,0xaaa);
  for (unsigned mode=0; mode<4; mode++) {
    setup(); cpu.gpr[6]=BASE+64+mode;
    CHECK(instruction(CSR_MTVEC,1,0,6).retired);
    CHECK_EQ(cpu.mtvec,BASE+64+(mode&1));
  }
  setup(); cpu.misa &= ~MISA_C; cpu.gpr[6]=BASE+3;
  CHECK(instruction(CSR_MEPC,1,0,6).retired);
  CHECK(instruction(CSR_MEPC,2,5,0).retired); CHECK_EQ(cpu.gpr[5],BASE);
#ifdef CONFIG_RV_C
  cpu.misa |= MISA_C;
  CHECK(instruction(CSR_MEPC,2,5,0).retired); CHECK_EQ(cpu.gpr[5],BASE+2);
#else
  CHECK_EQ(cpu.mepc,BASE);
#endif
  setup(); cpu.priv=PRV_S; cpu.mstatus |= MSTATUS_TVM;
  StepResult r=instruction(CSR_SATP,2,5,0);
  CHECK(!r.retired && !r.register_write); CHECK_EQ(cpu.mcause,2);
  CHECK_EQ(cpu.gpr[5],0xdeadbeef); CHECK_EQ(cpu.satp,0);
  setup(); cpu.gpr[6]=UINT64_MAX;
  CHECK(instruction(CSR_SATP,1,5,6).retired); CHECK_EQ(cpu.satp,0);

  /* SEIP 设备位不能被 CSRRS 顺手锁存到软件位。 */
  setup(); IrqLine *external=cpu_irq_input(IRQ_SEIP); irq_raise(external);
  cpu.gpr[6]=MIP_SSIP;
  CHECK(instruction(CSR_MIP,2,5,6).retired);
  CHECK_EQ(cpu.gpr[5],MIP_SEIP); CHECK_EQ(cpu.mip_software,MIP_SSIP);
  irq_lower(external);
  CHECK(instruction(CSR_MIP,2,5,0).retired); CHECK_EQ(cpu.gpr[5],MIP_SSIP);
  irq_raise(external); cpu.gpr[6]=MIP_SEIP;
  CHECK(instruction(CSR_MIP,3,5,6).retired);
  CHECK_EQ(cpu.gpr[5],MIP_SSIP|MIP_SEIP);
  CHECK_EQ(cpu.mip_software,MIP_SSIP);
  irq_lower(external);
  CHECK(csr_write(CSR_MIDELEG,SIE_MASK)); CHECK(csr_write(CSR_MIP,SIE_MASK));
  CHECK(csr_write(CSR_SIP,0)); CHECK_EQ(cpu.mip_software,MIP_STIP|MIP_SEIP);
  CHECK(csr_write(CSR_MIDELEG,0)); word_t v;
  CHECK(csr_read(CSR_SIP,&v)); CHECK_EQ(v,0);
  CHECK(csr_write(CSR_SIE,UINT64_MAX)); CHECK_EQ(cpu.mie,0);
  mem_shutdown();
}

int main(void) {
  cpu_reset(0x80000000ull);
  word_t v = 0;

  /* ---- 特权级：由 csr[9:8] 推导 ---- */
  cpu.priv = PRV_U;
  CHECK(!csr_read(CSR_MSTATUS, &v));  /* M CSR：U 模式不可访问 */
  CHECK(!csr_write(CSR_MSTATUS, 0));
  CHECK(!csr_read(CSR_SSTATUS, &v));  /* S CSR：U 模式也不可访问 */

  cpu.priv = PRV_S;
  CHECK(csr_read(CSR_SSTATUS, &v));   /* S 模式可访问 S CSR */
  CHECK(!csr_read(CSR_MSTATUS, &v));  /* 但访问不了 M CSR */

  cpu.priv = PRV_M;
  CHECK(csr_read(CSR_MSTATUS, &v));   /* M 模式通吃 */
  CHECK(csr_read(CSR_SSTATUS, &v));

  /* ---- 只读：0xf1x 段由地址位自动判定 ---- */
  CHECK(csr_read(CSR_MVENDORID, &v));
  CHECK(!csr_write(CSR_MVENDORID, 0x1234));
  CHECK(csr_read(CSR_MHARTID, &v));
  CHECK(!csr_write(CSR_MHARTID, 1));

  /* ---- 新加的 CSR 自动获得派生属性 ----
   * mconfigptr 是在 csr_list.h 里"加两行"就有的：M 模式可读，
   * 且因为落在 0xf1x 段被自动判为只读（无需任何额外声明）。*/
  CHECK(csr_read(CSR_MCONFIGPTR, &v));
  CHECK(!csr_write(CSR_MCONFIGPTR, 0x1000));

  /* ---- 未实现的 CSR → 非法（不静默返回 0） ---- */
  CHECK(!csr_read(0x123, &v));
  CHECK(!csr_write(0x123, 0));

  /* ---- 视图：写 sstatus 只动 S 侧位，M 侧独有位必须保留 ---- */
  cpu.mstatus = MSTATUS_MIE | MSTATUS_MPIE;
  CHECK(csr_write(CSR_SSTATUS, ~0ull)); /* 全 1 写 sstatus */
  CHECK(cpu.mstatus & MSTATUS_MIE);     /* M 侧位被保留 */
  CHECK(cpu.mstatus & MSTATUS_MPIE);
  CHECK(cpu.mstatus & MSTATUS_SIE);     /* S 侧位被写入 */
  CHECK(cpu.mstatus & MSTATUS_SUM);
  CHECK(csr_read(CSR_SSTATUS, &v));     /* 读回只看到 S 侧位 */
  CHECK(!(v & MSTATUS_MIE));

  /* ---- 视图：sie 是 mie 的视图，与 mip 是不同存储 ---- */
  cpu.mie = 0;
  cpu.mip = 1ull << 7; /* 假装 MTIP 挂起 */
  CHECK(csr_write(CSR_MIDELEG, SIE_MASK)); /* S 视图仅包含已经委派的中断 */
  CHECK(csr_write(CSR_SIE, ~0ull));
  CHECK_EQ(cpu.mip, 1ull << 7);         /* 写 sie 不碰 mip */
  CHECK(csr_read(CSR_SIE, &v));
  CHECK(v & (1ull << 1));               /* SSIP 在 SIE 视图内 */
  CHECK(!(v & (1ull << 7)));            /* MTIP 不在 SIE 视图内 */

  /* ---- 写 satp 的副作用钩子仍应生效（M3 前是空实现，只验证不崩） ---- */
  cpu.priv = PRV_M;
  CHECK(csr_write(CSR_SATP, 0));
  CHECK(csr_read(CSR_SATP, &v));

  test_instruction_rules();
  TEST_MAIN_END();
}
