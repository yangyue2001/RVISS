#include <cpu/cpu.h>
#include <cpu/csr_list.h>
#include <device/device.h>
#include <device/timer/clint.h>
#include <memory/paddr.h>
#include <util/state.h>
#include "../../src/engine/engine.h"
#include "csr.h"
#include "test.h"
#include "pmp.h"
#include "mmu.h"

#define BASE UINT64_C(0x80000000)

#ifdef CONFIG_RV_ZICNTR
static word_t clock_read(void *opaque) { return *(word_t *)opaque; }
#endif

static void setup(void) {
  cpu_reset(BASE);
  riscv_pmp_set_regions(0); riscv_mmu_set_sv39(false); /* 本 fixture 固定 Bare、0 PMP entries */ sim_reset(); cpu.mtvec=BASE+384;
  cpu.instret=17; cpu.cycle=23; cpu.gpr[5]=0xdeadbeef; cpu.gpr[6]=0;
}

static StepResult execute(uint32_t raw) {
  CHECK(paddr_write(cpu.pc,4,raw));
  StepResult r; CHECK(engine_step(&r)); return r;
}

static StepResult instruction(unsigned csr,unsigned fn,unsigned rd,unsigned rs1) {
  return execute((csr<<20)|(rs1<<15)|(fn<<12)|(rd<<7)|0x73);
}

#ifdef CONFIG_RV_ZICNTR
static void test_writes(void) {
  setup(); cpu.gpr[6]=UINT64_MAX;
  StepResult r=instruction(CSR_MINSTRET,1,5,6);
  CHECK(r.retired && r.counter_write_instret); CHECK_EQ(cpu.gpr[5],17);
  CHECK_EQ(cpu.instret,UINT64_MAX); CHECK_EQ(sim.instret,1); CHECK_EQ(r.retired_total,1);
  CHECK(execute(0x13).retired); CHECK_EQ(cpu.instret,0); CHECK_EQ(sim.instret,2);
  CHECK(instruction(CSR_INSTRET,2,5,0).retired); CHECK_EQ(cpu.gpr[5],0);
  CHECK_EQ(cpu.instret,1); CHECK_EQ(sim.instret,3);
  setup(); cpu.gpr[6]=UINT64_MAX;
  r=instruction(CSR_MCYCLE,1,5,6);
  CHECK(r.retired && r.counter_write_cycle); CHECK_EQ(cpu.gpr[5],23);
  CHECK_EQ(cpu.cycle,UINT64_MAX); CHECK_EQ(sim.logical_ticks,1);
  CHECK(execute(0x13).retired); CHECK_EQ(cpu.cycle,0);
  CHECK(instruction(CSR_CYCLE,2,5,0).retired); CHECK_EQ(cpu.gpr[5],0);
  CHECK_EQ(cpu.cycle,1);
  setup(); cpu.gpr[6]=0;
  CHECK(instruction(CSR_MINSTRET,2,5,6).retired); CHECK_EQ(cpu.instret,17);
  CHECK(instruction(CSR_MINSTRET,2,5,0).retired); CHECK_EQ(cpu.instret,18);
  CHECK(instruction(CSR_MINSTRET,7,5,0).retired); CHECK_EQ(cpu.instret,19);
  setup(); cpu.gpr[6]=7;
  CHECK(instruction(CSR_MINSTRET,1,6,6).retired);
  CHECK_EQ(cpu.instret,7); CHECK_EQ(cpu.gpr[6],17);
  setup(); cpu.gpr[6]=UINT64_MAX;
  CHECK(instruction(CSR_MINSTRET,3,0,6).retired); CHECK_EQ(cpu.instret,0);
  CHECK_EQ(sim.instret,1);
  setup(); CHECK(execute(0x73).trap.valid); CHECK_EQ(cpu.instret,17);
  CHECK_EQ(sim.instret,0); CHECK_EQ(cpu.cycle,24); CHECK_EQ(sim.logical_ticks,1);
}

static void test_permissions(void) {
  word_t time=42; device_set_time_reader(clock_read,&time);
  const unsigned levels[]={PRV_U,PRV_S,PRV_M};
  for(unsigned p=0;p<3;p++) for(unsigned m=0;m<2;m++) for(unsigned s=0;s<2;s++)
  for(unsigned index=0;index<3;index++) {
    setup(); cpu.priv=levels[p]; cpu.mcounteren=m?7:0; cpu.scounteren=s?7:0;
    StepResult r=instruction(CSR_CYCLE+index,2,5,0);
    bool allowed=p==2 || (m && (p==1 || s));
    CHECK_EQ(r.retired,allowed);
    if(allowed) {
      const word_t values[]={23,42,17}; CHECK_EQ(cpu.gpr[5],values[index]);
      CHECK_EQ(cpu.instret,18); CHECK_EQ(sim.instret,1);
    } else {
      CHECK_EQ(cpu.gpr[5],0xdeadbeef); CHECK_EQ(cpu.mcause,2);
      CHECK(!r.register_write); CHECK_EQ(cpu.instret,17); CHECK_EQ(sim.instret,0);
    }
  }
  for(unsigned index=0;index<3;index++) for(unsigned fn=1;fn<=7;fn++) {
    if(fn==4) continue;
    setup(); cpu.gpr[6]=0;
    StepResult r=instruction(CSR_CYCLE+index,fn,5,6);
    CHECK(!r.retired && !r.register_write); CHECK_EQ(cpu.mcause,2);
    CHECK_EQ(cpu.gpr[5],0xdeadbeef); CHECK_EQ(cpu.instret,17);
  }
  const unsigned rv32_csrs[]={0xb80,0xb82,0xc80,0xc81,0xc82,0xc03};
  for(unsigned i=0;i<ARRLEN(rv32_csrs);i++) {
    setup(); StepResult r=instruction(rv32_csrs[i],2,5,0);
    CHECK(!r.retired); CHECK_EQ(cpu.mcause,2); CHECK_EQ(cpu.gpr[5],0xdeadbeef);
  }
  device_set_time_reader(NULL,NULL);
}

static void test_zero_hpm(void) {
  setup(); cpu.gpr[6]=UINT64_MAX;
  CHECK(instruction(CSR_MCOUNTINHIBIT,1,5,6).retired);
  CHECK(instruction(CSR_MCOUNTINHIBIT,2,5,0).retired); CHECK_EQ(cpu.gpr[5],0);
  CHECK_EQ(cpu.instret,19); CHECK_EQ(cpu.cycle,25);
  for(unsigned i=3;i<=31;i++) for(unsigned kind=0;kind<2;kind++) {
    setup(); cpu.gpr[6]=UINT64_MAX;
    unsigned address=(kind?CSR_MHPMEVENT3:CSR_MHPMCOUNTER3)+i-3;
    CHECK(instruction(address,1,5,6).retired); CHECK_EQ(cpu.gpr[5],0);
    CHECK(instruction(address,2,5,0).retired); CHECK_EQ(cpu.gpr[5],0);
    setup(); cpu.priv=PRV_S; StepResult r=instruction(address,2,5,0);
    CHECK(!r.retired); CHECK_EQ(cpu.mcause,2); CHECK_EQ(cpu.gpr[5],0xdeadbeef);
  }
}

static void test_clint_time(void) {
#ifdef CONFIG_HAS_CLINT
  bus_init(BASE,512); ClintState clint; clint_init(&clint);
  CHECK(mmio_map(&clint.mmio,0x02000000)); device_set_time_reader(clint_time,&clint);
  setup(); clint.mtime=55; sim.logical_ticks=127; cpu.gpr[6]=0;
  CHECK(instruction(CSR_MCYCLE,1,0,6).retired);
  CHECK_EQ(cpu.cycle,0); CHECK_EQ(sim.logical_ticks,128); CHECK_EQ(clint.mtime,56);
  CHECK(instruction(CSR_TIME,2,5,0).retired); CHECK_EQ(cpu.gpr[5],56);
  CHECK(instruction(CSR_MINSTRET,1,0,6).retired);
  CHECK_EQ(cpu.instret,0); CHECK_EQ(sim.instret,3); CHECK_EQ(clint.mtime,56);
  word_t value; CHECK(paddr_try_read(0x0200bff8,8,&value,NULL)); CHECK_EQ(value,56);
  CHECK(paddr_write(0x0200bff8,8,200));
  CHECK(instruction(CSR_TIME,2,5,0).retired); CHECK_EQ(cpu.gpr[5],200);
  CHECK_EQ(sim.logical_ticks,131); CHECK_EQ(sim.instret,4);
  cpu_reset(BASE);
  riscv_pmp_set_regions(0); riscv_mmu_set_sv39(false); /* 本 fixture 固定 Bare、0 PMP entries */ clint_reset(&clint); CHECK_EQ(device_time(),0);
  bus_init(BASE,512); CHECK_EQ(device_time(),0); /* 清掉局部实例回调 */
#endif
}
#else
static void test_counters_disabled(void) {
  const unsigned counters[] = {CSR_MCYCLE, CSR_MINSTRET, CSR_CYCLE,
                               CSR_TIME, CSR_INSTRET, CSR_MCOUNTINHIBIT,
                               CSR_MCOUNTEREN, CSR_SCOUNTEREN};
  for (size_t i = 0; i < ARRLEN(counters); i++) {
    setup();
    word_t value = 0;
    CHECK(!csr_read(counters[i], &value));
    CHECK(!csr_write(counters[i], UINT64_MAX));
    StepResult r = instruction(counters[i], 2, 5, 0);
    CHECK(!r.retired && r.trap.valid);
    CHECK_EQ(r.trap.cause, 2);
    CHECK_EQ(cpu.gpr[5], UINT64_C(0xdeadbeef));
  }
}
#endif

int main(void) {
  CHECK(mem_init(BASE,512)); bus_init(BASE,512); engine_init();
#ifdef CONFIG_RV_ZICNTR
  test_writes(); test_permissions(); test_zero_hpm(); test_clint_time();
#else
  test_counters_disabled();
#endif
  mem_shutdown(); TEST_MAIN_END();
}
