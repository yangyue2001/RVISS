#include <cpu/cpu.h>
#include <cpu/csr_list.h>
#include <memory/paddr.h>
#include <memory/vaddr.h>
#include <device/device.h>
#include <util/state.h>
#include "../../src/engine/engine.h"
#include "csr.h"
#include "pmp.h"
#include "test.h"

#define BASE UINT64_C(0x80000000)
#define DATA (BASE+1024)
#define ADDR_MASK UINT64_C(0x003fffffffffffff)
#define OLD UINT64_C(0x1122334455667788)

static void setup(void) {
  cpu_reset(BASE); sim_reset(); cpu.mtvec=BASE+128;
  cpu.gpr[5]=0xdeadbeef; cpu.gpr[6]=DATA; cpu.gpr[7]=1;
  CHECK(paddr_write(DATA,8,OLD));
}

static StepResult execute(uint32_t raw) {
  CHECK(paddr_write(cpu.pc,4,raw)); StepResult r; CHECK(engine_step(&r)); return r;
}

#ifdef CONFIG_RV_PMP
static void allow_fallback(void) {
  CHECK(csr_write(CSR_PMPADDR0+15,ADDR_MASK));
  CHECK(csr_write(CSR_PMPCFG0+2,UINT64_C(0x1f00000000000000)));
}

static void test_csr(void) {
  setup(); CHECK_EQ(cpu.pmp_count,16);
  word_t value;
  for(unsigned i=0;i<64;i++) {
    CHECK(csr_write(CSR_PMPADDR0+i,UINT64_MAX));
    CHECK(csr_read(CSR_PMPADDR0+i,&value)); CHECK_EQ(value,i<16?ADDR_MASK:0);
  }
  CHECK(csr_write(CSR_PMPCFG0,UINT64_MAX));
  CHECK(csr_read(CSR_PMPCFG0,&value)); CHECK_EQ(value,UINT64_C(0x9f9f9f9f9f9f9f9f));
  CHECK(csr_write(CSR_PMPCFG0,0)); CHECK(csr_read(CSR_PMPCFG0,&value));
  CHECK_EQ(value,UINT64_C(0x9f9f9f9f9f9f9f9f));
  CHECK(csr_write(CSR_PMPADDR0,0)); CHECK_EQ(cpu.pmpaddr[0],ADDR_MASK);
  setup(); CHECK(csr_write(CSR_PMPCFG0,0x72));
  CHECK(csr_read(CSR_PMPCFG0,&value)); CHECK_EQ(value,0x10); /* 保留位/W无R */
  CHECK(csr_write(CSR_PMPCFG0+2,UINT64_C(0x1716151413121110)));
  CHECK(csr_read(CSR_PMPCFG0+2,&value)); CHECK_EQ(value,UINT64_C(0x1714151413101110));
  for(unsigned i=1;i<16;i+=2) CHECK(!csr_read(CSR_PMPCFG0+i,&value));
  for(unsigned i=4;i<16;i+=2) {
    CHECK(csr_write(CSR_PMPCFG0+i,UINT64_MAX)); CHECK(csr_read(CSR_PMPCFG0+i,&value)); CHECK_EQ(value,0);
  }
  setup(); CHECK(csr_write(CSR_PMPADDR0,DATA>>2));
  CHECK(csr_write(CSR_PMPADDR0+1,(DATA+16)>>2)); CHECK(csr_write(CSR_PMPCFG0,0x8900));
  CHECK(csr_write(CSR_PMPADDR0,0)); CHECK_EQ(cpu.pmpaddr[0],DATA>>2);
  CHECK(csr_write(CSR_PMPADDR0+1,0)); CHECK_EQ(cpu.pmpaddr[1],(DATA+16)>>2);
  CHECK(csr_write(CSR_PMPCFG0,0)); CHECK_EQ(cpu.pmpcfg[1],0x89);
  setup(); CHECK(csr_write(CSR_PMPADDR0,DATA>>2)); CHECK(csr_write(CSR_PMPCFG0,PMP_L));
  CHECK(csr_write(CSR_PMPADDR0,0)); CHECK_EQ(cpu.pmpaddr[0],DATA>>2); /* OFF 也锁地址 */
  cpu_reset(BASE); CHECK_EQ(cpu.pmpcfg[0],0); CHECK_EQ(cpu.pmpaddr[0],0);
  CHECK(csr_write(CSR_PMPADDR0,42)); CHECK_EQ(cpu.pmpaddr[0],42);
  cpu.priv=PRV_S; CHECK(!csr_read(CSR_PMPADDR0,&value));
}

static void test_rules(void) {
  const unsigned levels[]={PRV_U,PRV_S,PRV_M};
  const struct {unsigned input,canonical;bool read,write,fetch;} v[]={
    {0,0,0,0,0},{1,1,1,0,0},{2,0,0,0,0},{3,3,1,1,0},
    {4,4,0,0,1},{5,5,1,0,1},{6,4,0,0,1},{7,7,1,1,1}};
  for(unsigned p=0;p<3;p++) for(unsigned l=0;l<2;l++) for(unsigned i=0;i<8;i++) {
    setup(); CHECK(csr_write(CSR_PMPADDR0,(DATA>>2)|1));
    CHECK(csr_write(CSR_PMPCFG0,PMP_NAPOT|v[i].input|(l?PMP_L:0)));
    CHECK_EQ(cpu.pmpcfg[0],PMP_NAPOT|v[i].canonical|(l?PMP_L:0));
    bool bypass=p==2 && !l;
    CHECK_EQ(riscv_pmp_check(DATA,8,MEM_TYPE_READ,levels[p]),bypass||v[i].read);
    CHECK_EQ(riscv_pmp_check(DATA,8,MEM_TYPE_WRITE,levels[p]),bypass||v[i].write);
    CHECK_EQ(riscv_pmp_check(DATA,4,MEM_TYPE_IFETCH,levels[p]),bypass||v[i].fetch);
    CHECK_EQ(riscv_pmp_check(DATA+16,1,MEM_TYPE_READ,levels[p]),p==2);
  }
  setup(); CHECK(!riscv_pmp_check(DATA,8,MEM_TYPE_READ,PRV_S));
  CHECK(riscv_pmp_check(DATA,8,MEM_TYPE_READ,PRV_M));
  CHECK(csr_write(CSR_PMPADDR0,(DATA+4)>>2)); CHECK(csr_write(CSR_PMPCFG0,PMP_NA4|7));
  CHECK(!riscv_pmp_check(DATA,8,MEM_TYPE_READ,PRV_M)); /* partial match 未锁M也拒绝 */
  CHECK(riscv_pmp_check(DATA+4,4,MEM_TYPE_READ,PRV_U));
  CHECK(!riscv_pmp_check(DATA+8,1,MEM_TYPE_READ,PRV_U));
  setup(); CHECK(csr_write(CSR_PMPADDR0,DATA>>2));
  CHECK(csr_write(CSR_PMPADDR0+1,(DATA+16)>>2)); CHECK(csr_write(CSR_PMPCFG0,0x0b00));
  CHECK(riscv_pmp_check(DATA,8,MEM_TYPE_WRITE,PRV_S));
  CHECK(!riscv_pmp_check(DATA+16,1,MEM_TYPE_READ,PRV_S));
  CHECK(csr_write(CSR_PMPADDR0+1,DATA>>2)); CHECK(!riscv_pmp_check(DATA,1,MEM_TYPE_READ,PRV_S));
  CHECK(csr_write(CSR_PMPADDR0+1,(DATA-4)>>2)); CHECK(!riscv_pmp_check(DATA,1,MEM_TYPE_READ,PRV_S));
  setup(); CHECK(csr_write(CSR_PMPADDR0,ADDR_MASK)); CHECK(csr_write(CSR_PMPCFG0,PMP_NAPOT|7));
  CHECK(riscv_pmp_check(0,8,MEM_TYPE_READ,PRV_U));
  CHECK(riscv_pmp_check((UINT64_C(1)<<56)-8,8,MEM_TYPE_WRITE,PRV_S));
}

static void test_execution(void) {
  const struct {uint32_t raw;unsigned type,cause;word_t expected;} ops[]={
    {0x00033283,MEM_TYPE_READ,5,OLD},{0x00733023,MEM_TYPE_WRITE,7,1},
    {0x100332af,MEM_TYPE_READ,5,OLD},{0x007332af,MEM_TYPE_WRITE,7,OLD+1}};
  const unsigned levels[]={PRV_U,PRV_S,PRV_M};
  for(unsigned p=0;p<3;p++) for(unsigned lock=0;lock<2;lock++)
  for(unsigned allow=0;allow<2;allow++) for(unsigned op=0;op<ARRLEN(ops);op++) {
    setup(); allow_fallback();
    CHECK(csr_write(CSR_PMPADDR0,(DATA>>2)|1));
    CHECK(csr_write(CSR_PMPCFG0,PMP_NAPOT|(allow?3:0)|(lock?PMP_L:0)));
    cpu.priv=levels[p]; StepResult r=execute(ops[op].raw);
    bool permitted=allow || (p==2 && !lock);
    CHECK_EQ(r.retired,permitted); CHECK_EQ(r.memory.write_performed,permitted && ops[op].type==MEM_TYPE_WRITE);
    if(permitted) {
      if(ops[op].type==MEM_TYPE_READ || op==3) CHECK_EQ(cpu.gpr[5],OLD);
      else CHECK_EQ(cpu.gpr[5],0xdeadbeef);
      CHECK_EQ(paddr_read(DATA,8),ops[op].type==MEM_TYPE_WRITE?ops[op].expected:OLD);
    } else {
      CHECK_EQ(cpu.mcause,ops[op].cause); CHECK_EQ(cpu.mtval,DATA); CHECK_EQ(cpu.mepc,BASE);
      CHECK_EQ(cpu.gpr[5],0xdeadbeef); CHECK_EQ(paddr_read(DATA,8),OLD); CHECK_EQ(sim.instret,0);
    }
  }
  for(unsigned p=0;p<3;p++) for(unsigned lock=0;lock<2;lock++) for(unsigned write=0;write<2;write++) {
    setup(); allow_fallback(); CHECK(csr_write(CSR_PMPADDR0,(DATA>>2)|1));
    CHECK(csr_write(CSR_PMPCFG0,PMP_NAPOT|PMP_R|(write?PMP_W:0)|(lock?PMP_L:0)));
    cpu.priv=levels[p]; CHECK(execute(0x100332af).retired);
    StepResult r=execute(0x187332af); bool allowed=write || (p==2 && !lock);
    CHECK_EQ(r.retired,allowed); CHECK_EQ(r.memory.write_performed,allowed);
    CHECK_EQ(cpu.gpr[5],allowed?0:OLD); CHECK_EQ(paddr_read(DATA,8),allowed?1:OLD);
    CHECK(!cpu.reservation_valid); CHECK_EQ(sim.instret,allowed?2:1);
    if(!allowed) {CHECK_EQ(cpu.mcause,7); CHECK_EQ(cpu.mtval,DATA);}
  }
  setup(); allow_fallback(); CHECK(csr_write(CSR_PMPADDR0,DATA>>2));
  CHECK(csr_write(CSR_PMPCFG0,PMP_NA4));
  cpu.mstatus|=MSTATUS_MPRV|MSTATUS_MPP_S;
  StepResult r=execute(0x00033283); CHECK(!r.retired); CHECK_EQ(cpu.mcause,5); CHECK_EQ(cpu.mtval,DATA);
  setup(); allow_fallback(); CHECK(csr_write(CSR_PMPADDR0,(DATA+4)>>2));
  CHECK(csr_write(CSR_PMPCFG0,PMP_NA4|7));
  r=execute(0x00733023); CHECK(!r.retired); CHECK_EQ(cpu.mcause,7); CHECK_EQ(paddr_read(DATA,8),OLD);
  setup(); CHECK(csr_write(CSR_PMPADDR0,BASE>>2)); CHECK(csr_write(CSR_PMPCFG0,PMP_NA4|PMP_L|PMP_R));
  r=execute(0x13); CHECK(!r.retired); CHECK_EQ(cpu.mcause,1); CHECK_EQ(cpu.mtval,BASE);
  CHECK_EQ(r.raw_bytes,0); CHECK(!r.instruction_valid);
#ifdef CONFIG_RV_C
  setup(); allow_fallback(); CHECK(csr_write(CSR_PMPADDR0,(BASE+4)>>2));
  CHECK(csr_write(CSR_PMPCFG0,PMP_NA4|PMP_R)); cpu.priv=PRV_S; cpu.pc=BASE+2;
  r=execute(0x00100013); CHECK(!r.retired); CHECK_EQ(cpu.mcause,1); CHECK_EQ(cpu.mtval,BASE+4);
  CHECK_EQ(r.trap.epc,BASE+2); CHECK_EQ(r.raw_instruction,0x0013); CHECK_EQ(r.raw_bytes,2);
  CHECK_EQ(r.instruction_length,4); CHECK(!r.instruction_valid);
#endif
}
#endif

int main(void) {
  CHECK(mem_init(BASE,4096)); bus_init(BASE,4096); engine_init();
#ifdef CONFIG_RV_PMP
  test_csr(); test_rules(); test_execution();
#else
  setup(); CHECK_EQ(cpu.pmp_count,0); word_t v;
  CHECK(!csr_read(CSR_PMPCFG0,&v)); CHECK(!csr_write(CSR_PMPADDR0,1));
  CHECK(riscv_pmp_check(DATA,8,MEM_TYPE_WRITE,PRV_S));
  CHECK(execute(0x00033283).retired); CHECK_EQ(cpu.gpr[5],OLD);
#endif
  mem_shutdown(); TEST_MAIN_END();
}
