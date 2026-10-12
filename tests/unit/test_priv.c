#include <cpu/cpu.h>
#include <cpu/csr_list.h>
#include <device/device.h>
#include <memory/paddr.h>
#include <util/state.h>
#include "../../src/engine/engine.h"
#include "csr.h"
#include "test.h"
#include "pmp.h"

#define BASE UINT64_C(0x80000000)
#define MTVEC (BASE+128)
#define STVEC (BASE+192)
#define XL UINT64_C(0xa00000000)

static void setup(uint32_t raw) {
  cpu_reset(BASE);
  riscv_pmp_set_regions(0); /* 本 fixture 固定 Bare、0 PMP entries */
  sim_reset();
  cpu.mtvec=MTVEC; cpu.stvec=STVEC;
  cpu.mepc=BASE+64; cpu.sepc=BASE+96;
  cpu.mcause=cpu.scause=0x1234;
  cpu.mtval=cpu.stval=0x5678;
  cpu.gpr[5]=0xdeadbeef; cpu.gpr[6]=0x40000000;
  CHECK(paddr_write(BASE,4,raw));
}

static StepResult step(void) {
  StepResult r;
  CHECK(engine_step(&r));
  return r;
}

static void test_sync_delegation(void) {
  const unsigned levels[]={PRV_U,PRV_S,PRV_M};
  const uint32_t raw[]={0x00000073,0x00100073,0x0000000b,0x00033283,0x00533023};
  const word_t m_status[3][4]={
    {0x20020,0x200a0,0x20022,0x200a2},
    {0x20820,0x208a0,0x20822,0x208a2},
    {0x21820,0x218a0,0x21822,0x218a2}};
  const word_t s_status[2][4]={
    {0x20080,0x20088,0x200a0,0x200a8},
    {0x20180,0x20188,0x201a0,0x201a8}};
  for(unsigned p=0;p<3;p++) for(unsigned delegated=0;delegated<2;delegated++)
  for(unsigned ie=0;ie<4;ie++) for(unsigned vector=0;vector<2;vector++)
  for(unsigned i=0;i<ARRLEN(raw);i++) {
    setup(raw[i]); cpu.priv=levels[p]; cpu.medeleg=delegated?MEDELEG_MASK:0;
    cpu.mtvec|=vector; cpu.stvec|=vector;
    cpu.mstatus=XL|MSTATUS_MPRV|MSTATUS_MPIE|MSTATUS_SPIE|
                ((ie&1)?MSTATUS_MIE:0)|((ie&2)?MSTATUS_SIE:0);
    StepResult r=step();
    word_t cause=i==0?8+levels[p]:i==1?3:i==2?2:i==3?5:7;
    word_t tval=i==0?0:i==1?BASE:i==2?raw[i]:0x40000000;
    bool to_s=delegated && p<2;
    CHECK_EQ(r.outcome,STEP_SYNCHRONOUS_TRAP);
    CHECK(!r.retired && !r.register_write && !r.memory.write_performed);
    CHECK_EQ(r.trap.cause,cause); CHECK_EQ(r.trap.epc,BASE); CHECK_EQ(r.trap.tval,tval);
    CHECK_EQ(cpu.priv,to_s?PRV_S:PRV_M); CHECK_EQ(cpu.pc,to_s?STVEC:MTVEC);
    CHECK_EQ(cpu.mstatus,XL|(to_s?s_status[p][ie]:m_status[p][ie]));
    CHECK_EQ(to_s?cpu.sepc:cpu.mepc,BASE);
    CHECK_EQ(to_s?cpu.scause:cpu.mcause,cause);
    CHECK_EQ(to_s?cpu.stval:cpu.mtval,tval);
    CHECK_EQ(to_s?cpu.mepc:cpu.sepc,to_s?BASE+64:BASE+96);
    CHECK_EQ(to_s?cpu.mcause:cpu.scause,0x1234);
    CHECK_EQ(to_s?cpu.mtval:cpu.stval,0x5678);
    CHECK_EQ(cpu.gpr[5],0xdeadbeef); CHECK_EQ(sim.instret,0);
  }
}

static void test_returns(void) {
  const unsigned levels[]={PRV_U,PRV_S,PRV_M};
  for(unsigned p=0;p<3;p++) for(unsigned pie=0;pie<2;pie++)
  for(unsigned mprv=0;mprv<2;mprv++) {
    setup(0x30200073); /* mret */
    cpu.mstatus=XL|(levels[p]<<11)|MSTATUS_SIE|MSTATUS_SPIE|
        (pie?MSTATUS_MPIE:0)|(mprv?MSTATUS_MPRV:0);
    CHECK(csr_write(CSR_MEPC,BASE+67));
    cpu.misa &= ~MISA_C;
    StepResult r=step();
    CHECK(r.retired); CHECK_EQ(cpu.priv,levels[p]); CHECK_EQ(cpu.pc,BASE+64);
    CHECK_EQ(cpu.mstatus,XL|0xa2|(pie?8:0)|((p==2 && mprv)?0x20000:0));
    CHECK_EQ(sim.instret,1);
  }
  for(unsigned current=PRV_S;current<=PRV_M;current+=2)
  for(unsigned previous=0;previous<2;previous++) for(unsigned pie=0;pie<2;pie++) {
    setup(0x10200073); cpu.priv=current;
    cpu.mstatus=XL|MSTATUS_MIE|MSTATUS_MPIE|MSTATUS_MPRV|
        (previous?MSTATUS_SPP:0)|(pie?MSTATUS_SPIE:0);
    if(current==PRV_M) cpu.mstatus|=MSTATUS_TSR; /* TSR 不拦 M 的 SRET */
    cpu.sepc=BASE+96;
    StepResult r=step(); CHECK(r.retired);
    CHECK_EQ(cpu.priv,previous?PRV_S:PRV_U); CHECK_EQ(cpu.pc,BASE+96);
    CHECK_EQ(cpu.mstatus,XL|0xa8|(pie?2:0)|(current==PRV_M?0x400000:0));
  }
#ifdef CONFIG_RV_C
  setup(0x30200073); CHECK(csr_write(CSR_MEPC,BASE+67));
  CHECK(step().retired); CHECK_EQ(cpu.pc,BASE+66);
  setup(0x10200073); CHECK(csr_write(CSR_SEPC,BASE+99));
  CHECK(step().retired); CHECK_EQ(cpu.pc,BASE+98);
#endif
}

static void test_privilege_guards(void) {
  const struct {uint32_t raw;unsigned priv;word_t control;bool illegal;} cases[]={
    {0x30200073,PRV_U,0,true},{0x30200073,PRV_S,0,true},
    {0x10200073,PRV_U,0,true},{0x10200073,PRV_S,MSTATUS_TSR,true},
    {0x10500073,PRV_U,0,true},{0x10500073,PRV_S,MSTATUS_TW,true},
    {0x10500073,PRV_S,0,false},{0x10500073,PRV_M,MSTATUS_TW,false},
    {0x12000073,PRV_U,0,true},{0x12000073,PRV_S,MSTATUS_TVM,true},
    {0x12000073,PRV_M,0,true} /* Bare-only 尚无 SFENCE.VMA */
  };
  for(unsigned i=0;i<ARRLEN(cases);i++) {
    setup(cases[i].raw); cpu.priv=cases[i].priv; cpu.mstatus|=cases[i].control;
    StepResult r=step();
    CHECK_EQ(r.retired,!cases[i].illegal);
    if(cases[i].illegal) {
      CHECK_EQ(cpu.mcause,2); CHECK_EQ(cpu.mepc,BASE); CHECK_EQ(cpu.mtval,cases[i].raw);
      CHECK_EQ(cpu.pc,MTVEC); CHECK_EQ(sim.instret,0);
    } else {
      CHECK_EQ(cpu.pc,BASE+4); CHECK_EQ(cpu.priv,cases[i].priv); CHECK_EQ(sim.instret,1);
    }
  }
}

int main(void) {
  CHECK(mem_init(BASE,256)); bus_init(BASE,256); engine_init();
  test_sync_delegation(); test_returns(); test_privilege_guards();
  mem_shutdown(); TEST_MAIN_END();
}
