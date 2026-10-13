#include <cpu/cpu.h>
#include <cpu/csr_list.h>
#include <cpu/interrupt.h>
#include <memory/paddr.h>
#include <memory/vaddr.h>
#include <device/device.h>
#include <util/state.h>
#include "../../src/engine/engine.h"
#include "csr.h"
#include "mmu.h"
#include "pmp.h"
#include "test.h"

#define BASE UINT64_C(0x80000000)
#define ROOT (BASE+0x8000)
#define L1 (BASE+0x9000)
#define L0 (BASE+0xa000)
#define DATA (BASE+0x6000)
#define CODE UINT64_C(0x4000)
#define VA UINT64_C(0x5000)
#define PTE_DATA (L0+5*8)
#define SATP (UINT64_C(0x8000000000000000)|(ROOT>>12))
#define OLD UINT64_C(0x1122334455667788)

#ifdef CONFIG_RV_SV39
static void put(paddr_t address, word_t value) { CHECK(paddr_write(address,8,value)); }
static word_t get(paddr_t address) { word_t v=0; CHECK(paddr_try_read(address,8,&v,NULL)); return v; }
static word_t leaf(paddr_t address, word_t flags) { return ((address>>12)<<10)|flags; }
static void setup(void) {
  memset(mem_host_ptr(BASE),0,65536); cpu_reset(CODE); sim_reset();
  riscv_pmp_set_regions(0); cpu.mtvec=BASE+256;
  CHECK(csr_write(CSR_SATP,SATP));
  put(ROOT,leaf(L1,1)); put(L1,leaf(L0,1));
  put(L0+4*8,leaf(BASE,0xcb)); put(PTE_DATA,leaf(DATA,0xc7));
  put(DATA,OLD); cpu.priv=PRV_S; cpu.gpr[5]=0xdeadbeef; cpu.gpr[6]=VA; cpu.gpr[7]=42;
}
static StepResult execute(uint32_t raw) {
  CHECK(paddr_write(BASE,4,raw)); StepResult r; CHECK(engine_step(&r)); return r;
}
static void check_fault(MemAccess a, word_t cause, word_t va) {
  CHECK_EQ(a.status,MEM_FAULT); CHECK_EQ(a.cause,cause); CHECK_EQ(a.tval,va);
}
static void test_satp(void) {
  setup(); cpu.priv=PRV_M;
  CHECK(csr_write(CSR_SATP,UINT64_MAX)); CHECK_EQ(cpu.satp,SATP);
  CHECK(csr_write(CSR_SATP,UINT64_C(0x8fffffffffffffff)));
  CHECK_EQ(cpu.satp,UINT64_C(0x80000fffffffffff));
  CHECK(csr_write(CSR_SATP,0x1234)); CHECK_EQ(cpu.satp,0x1234);
  CHECK(csr_write(CSR_SATP,SATP)); CHECK_EQ(cpu.satp,SATP);
  cpu.priv=PRV_S; cpu.mstatus|=MSTATUS_TVM; word_t v;
  CHECK(!csr_read(CSR_SATP,&v)); CHECK(!csr_write(CSR_SATP,0));
}
static void test_permissions(void) {
  /* Independent explicit legal PTE permission table: R/W/X 000..111. */
  const bool load[8]={false,true,false,true,false,true,false,true};
  const bool store[8]={false,false,false,true,false,false,false,true};
  const bool fetch[8]={false,false,false,false,true,true,false,true};
  const bool mxr_load[8]={false,true,false,true,true,true,false,true};
  for(unsigned permissions=0;permissions<8;permissions++)
  for(unsigned user=0;user<2;user++) for(unsigned priv=0;priv<2;priv++)
  for(unsigned sum=0;sum<2;sum++) for(unsigned mxr=0;mxr<2;mxr++)
  for(int type=0;type<3;type++) {
    setup(); cpu.priv=priv; cpu.mstatus|=(sum?MSTATUS_SUM:0)|(mxr?MSTATUS_MXR:0);
    word_t pte=leaf(DATA,1|permissions*2|user*16|0xc0); put(PTE_DATA,pte);
    bool mode=priv==PRV_U?user:(!user||(sum&&type!=MEM_TYPE_IFETCH));
    bool allowed=mode&&(type==MEM_TYPE_IFETCH?fetch[permissions]:
                      type==MEM_TYPE_READ?(mxr?mxr_load[permissions]:load[permissions]):store[permissions]);
    MemAccess a=isa_mem_prepare(VA,8,type,true);
    CHECK_EQ(a.status,allowed?MEM_OK:MEM_FAULT);
    if(allowed) CHECK_EQ(a.paddr,DATA);
    else { CHECK_EQ(a.cause,type==0?12:type==1?13:15); CHECK_EQ(a.tval,VA); }
    CHECK_EQ(get(PTE_DATA),pte);
  }
}
static void test_invalid_and_pages(void) {
  const word_t invalid[]={0,5,0x80000000000000c7,0x40000000000000c7,0x20000000000000c7,
                          0x10000000000000c7,0x00400000000000c7,1,0x11,0x41,0x81};
  for(unsigned i=0;i<ARRLEN(invalid);i++) for(int type=0;type<3;type++) {
    setup(); put(PTE_DATA,leaf(DATA,0)|invalid[i]);
    check_fault(isa_mem_prepare(VA,8,type,true),type==0?12:type==1?13:15,VA);
  }
  for(unsigned level=1;level<=2;level++) for(unsigned bit=0;bit<3;bit++) {
    setup(); paddr_t entry=level==2?ROOT:L1;
    put(entry,leaf(L0,1)|((word_t[]){16,64,128})[bit]);
    check_fault(vaddr_read(VA,8),13,VA);
  }
  const vaddr_t noncanonical[]={UINT64_C(1)<<38,UINT64_C(1)<<39,UINT64_C(0xffffff8000005000)};
  for(unsigned i=0;i<ARRLEN(noncanonical);i++) {
    setup(); check_fault(vaddr_read(noncanonical[i],8),13,noncanonical[i]);
  }
  setup(); const vaddr_t high=UINT64_C(0xffffffc000005000);
  put(ROOT+256*8,leaf(L1,1)); CHECK_EQ(vaddr_read(high,8).data,OLD);
  setup(); put(L1,leaf(BASE,0xc7)); MemAccess a=vaddr_read(DATA-BASE,8);
  CHECK_EQ(a.status,MEM_OK); CHECK_EQ(a.paddr,DATA); CHECK_EQ(a.data,OLD); /* 2M */
  setup(); put(ROOT+8,leaf(BASE,0xc7)); a=vaddr_read(UINT64_C(0x40006000),8);
  CHECK_EQ(a.status,MEM_OK); CHECK_EQ(a.paddr,DATA); CHECK_EQ(a.data,OLD); /* 1G */
  for(unsigned level=1;level<=2;level++) {
    setup(); put(level==2?ROOT:L1,leaf(BASE+4096,0xc7));
    check_fault(vaddr_read(VA,8),13,VA);
  }
  setup(); put(ROOT,leaf(BASE+0x10000,1)); check_fault(vaddr_read(VA,8),5,VA);
  setup(); put(PTE_DATA,leaf(UINT64_C(0x00fffffffffff000),0xc7));
  check_fault(vaddr_read(VA,8),5,VA);
  setup(); check_fault(vaddr_read(VA+1,8),4,VA+1); /* misaligned before walk */
}
static void test_ad_and_execution(void) {
  setup(); word_t initial=leaf(DATA,7); put(PTE_DATA,initial);
  CHECK_EQ(vaddr_read(VA,8).data,OLD); CHECK_EQ(get(PTE_DATA),initial|64);
  MemAccess a=vaddr_write(VA,8,42); CHECK_EQ(a.status,MEM_OK); CHECK(a.write_performed);
  CHECK_EQ(get(PTE_DATA),initial|192); CHECK_EQ(get(DATA),42);
  setup(); put(PTE_DATA,leaf(DATA,1)); check_fault(vaddr_write(VA,8,42),15,VA);
  CHECK_EQ(get(PTE_DATA),leaf(DATA,1)); CHECK_EQ(get(DATA),OLD);
  /* AMO must pass the load permission path as well as the store path. */
#ifdef CONFIG_RV_A
  setup(); put(PTE_DATA,leaf(DATA,0xc9));
  check_fault(isa_mem_prepare_atomic(VA,8,true),15,VA);
  CHECK_EQ(get(DATA),OLD);
#endif
  setup(); put(PTE_DATA,leaf(BASE+65536,7)); check_fault(vaddr_write(VA,8,42),7,VA);
  CHECK_EQ(get(PTE_DATA),leaf(BASE+65536,0xc7)); /* A/D can survive final PMA fault */
  setup(); put(PTE_DATA,0); StepResult r=execute(0x00033283);
  CHECK(!r.retired); CHECK_EQ(cpu.gpr[5],0xdeadbeef); CHECK_EQ(cpu.mcause,13);
  CHECK_EQ(cpu.mtval,VA); CHECK_EQ(cpu.mepc,CODE); CHECK_EQ(r.memory.vaddr,VA);
  setup(); r=execute(0x00033283); CHECK(r.retired); CHECK_EQ(cpu.gpr[5],OLD);
  CHECK_EQ(r.pc_before,CODE); CHECK_EQ(cpu.pc,CODE+4); CHECK_EQ(r.memory.paddr,DATA);
  setup(); r=execute(0x00733023); CHECK(r.retired); CHECK_EQ(get(DATA),42);
#ifdef CONFIG_RV_A
  setup(); put(PTE_DATA,leaf(DATA,7)); r=execute(0x100332af); CHECK(r.retired);
  CHECK_EQ(cpu.gpr[5],OLD); CHECK_EQ(get(PTE_DATA),leaf(DATA,0x47));
  CHECK(paddr_write(BASE+4,4,0x187333af)); CHECK(engine_step(&r)); CHECK(r.retired);
  CHECK_EQ(cpu.gpr[7],0); CHECK_EQ(get(DATA),42); CHECK_EQ(get(PTE_DATA),leaf(DATA,0xc7));
  setup(); r=execute(0x007332af); CHECK(r.retired); CHECK_EQ(cpu.gpr[5],OLD);
  CHECK_EQ(get(DATA),OLD+42);
  setup(); put(PTE_DATA,leaf(DATA,3)); r=execute(0x007332af); CHECK(!r.retired);
  CHECK_EQ(cpu.mcause,15); CHECK_EQ(cpu.gpr[5],0xdeadbeef); CHECK_EQ(get(DATA),OLD);
#endif
  setup(); cpu.priv=PRV_M; cpu.pc=BASE; cpu.mstatus|=MSTATUS_MPRV|(1ull<<11);
  r=execute(0x00033283); CHECK(r.retired); CHECK_EQ(cpu.gpr[5],OLD); CHECK_EQ(cpu.priv,3);
  setup(); put(L0+4*8,leaf(BASE,0x1b)); cpu.priv=PRV_U; cpu.medeleg=1ull<<13;
  cpu.stvec=CODE+256; put(PTE_DATA,0); r=execute(0x00033283);
  CHECK(!r.retired); CHECK_EQ(cpu.priv,PRV_S); CHECK_EQ(cpu.scause,13);
  CHECK_EQ(cpu.stval,VA); CHECK_EQ(cpu.sepc,CODE); CHECK_EQ(cpu.pc,CODE+256);
  /* An IRQ boundary must neither walk nor update the executable PTE. */
  setup(); word_t code_pte=leaf(BASE,11); put(L0+4*8,code_pte);
  cpu.mie=MIP_MEIP; irq_raise(cpu_irq_input(11));
  CHECK(engine_step(&r)); CHECK_EQ(r.outcome,STEP_INTERRUPT);
  CHECK_EQ(r.raw_bytes,0); CHECK_EQ(get(L0+4*8),code_pte); CHECK_EQ(sim.attempts,0);
}
static void test_sfence_and_cross_fetch(void) {
  for(unsigned priv=0;priv<=3;priv++) if(priv!=2) for(unsigned tvm=0;tvm<2;tvm++) {
    setup(); cpu.priv=priv; cpu.mstatus|=tvm?MSTATUS_TVM:0;
    if(priv==0) put(L0+4*8,leaf(BASE,0xdb));
    if(priv==3) cpu.pc=BASE;
    StepResult r=execute(0x12000073); CHECK_EQ(r.retired,priv==3||(priv==1&&!tvm));
    if(!r.retired) CHECK_EQ(cpu.mcause,2);
  }
  setup(); CHECK_EQ(vaddr_read(VA,8).data,OLD);
  put(PTE_DATA,leaf(DATA+4096,0xc7)); put(DATA+4096,42);
  CHECK(execute(0x12630073).retired); CHECK_EQ(vaddr_read(VA,8).data,42);
#ifdef CONFIG_RV_C
  setup(); cpu.pc=CODE+4094; put(PTE_DATA,leaf(DATA,0xcb));
  CHECK(paddr_write(BASE+4094,2,0x0293)); CHECK(paddr_write(DATA,2,0x0070));
  StepResult r; CHECK(engine_step(&r)); CHECK(r.retired); CHECK_EQ(cpu.gpr[5],7);
  CHECK_EQ(r.raw_instruction,0x00700293); CHECK_EQ(cpu.pc,CODE+4098);
  setup(); cpu.pc=CODE+4094; put(PTE_DATA,0); CHECK(paddr_write(BASE+4094,2,0x0293));
  CHECK(engine_step(&r)); CHECK(!r.retired); CHECK_EQ(cpu.mcause,12); CHECK_EQ(cpu.mtval,VA);
  CHECK_EQ(cpu.mepc,CODE+4094); CHECK_EQ(r.raw_bytes,2); CHECK_EQ(r.instruction_length,4);
  CHECK(!r.instruction_valid); CHECK_EQ(cpu.gpr[5],0xdeadbeef);
#endif
}
#ifdef CONFIG_RV_PMP
static void test_ptw_pmp(void) {
  for(unsigned write=0;write<2;write++) for(unsigned level=0;level<3;level++) {
    setup(); riscv_pmp_set_regions(16);
    CHECK(csr_write(CSR_PMPADDR0+15,UINT64_C(0x003fffffffffffff))==false); /* S cannot configure */
    cpu.priv=PRV_M;
    paddr_t page=level==2?ROOT:level==1?L1:L0;
    CHECK(csr_write(CSR_PMPADDR0,(page>>2)|511));
    CHECK(csr_write(CSR_PMPADDR0+15,UINT64_C(0x003fffffffffffff)));
    CHECK(csr_write(CSR_PMPCFG0,PMP_NAPOT|(write?PMP_R:0)));
    CHECK(csr_write(CSR_PMPCFG0+2,UINT64_C(0x1f00000000000000)));
    cpu.priv=PRV_S;
    if(write) put(PTE_DATA,leaf(DATA,7));
    MemAccess a=vaddr_write(VA,8,42);
    if(!write||level==0) { check_fault(a,7,VA); CHECK_EQ(get(DATA),OLD); }
    else { CHECK_EQ(a.status,MEM_OK); CHECK_EQ(get(DATA),42); }
  }
  setup(); riscv_pmp_set_regions(16); cpu.priv=PRV_M;
  CHECK(csr_write(CSR_PMPADDR0,(DATA>>2)|511));
  CHECK(csr_write(CSR_PMPADDR0+15,UINT64_C(0x003fffffffffffff)));
  CHECK(csr_write(CSR_PMPCFG0,PMP_NAPOT));
  CHECK(csr_write(CSR_PMPCFG0+2,UINT64_C(0x1f00000000000000)));
  cpu.priv=PRV_S; put(PTE_DATA,leaf(DATA,7));
  check_fault(vaddr_write(VA,8,42),7,VA); CHECK_EQ(get(PTE_DATA),leaf(DATA,0xc7));
  CHECK_EQ(get(DATA),OLD);
  /* A partial 8-byte PTE match fails before the full read. */
  setup(); riscv_pmp_set_regions(16); cpu.priv=PRV_M;
  CHECK(csr_write(CSR_PMPADDR0,ROOT>>2));
  CHECK(csr_write(CSR_PMPCFG0,PMP_NA4|PMP_R));
  CHECK(csr_write(CSR_PMPADDR0+15,UINT64_C(0x003fffffffffffff)));
  CHECK(csr_write(CSR_PMPCFG0+2,UINT64_C(0x1f00000000000000)));
  cpu.priv=PRV_S; check_fault(vaddr_read(VA,8),5,VA);
}
#endif
#endif

int main(void) {
  CHECK(mem_init(BASE,65536)); bus_init(BASE,65536); engine_init();
#ifdef CONFIG_RV_SV39
  test_satp(); test_permissions(); test_invalid_and_pages(); test_ad_and_execution();
  test_sfence_and_cross_fetch();
#ifdef CONFIG_RV_PMP
  test_ptw_pmp();
#endif
#else
  cpu_reset(BASE); CHECK(!cpu.sv39_supported);
  CHECK(csr_write(CSR_SATP,UINT64_C(0x8000000000000000))); CHECK_EQ(cpu.satp,0);
#endif
  CHECK(cpu.misa & MISA_S); CHECK(cpu.misa & MISA_U);
  mem_shutdown(); TEST_MAIN_END();
}
