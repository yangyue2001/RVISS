#include <common.h>
#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <device/device.h>
#include <memory/paddr.h>
#include <util/state.h>
#include "../../src/engine/engine.h"
#include "csr.h"
#include "test.h"

#define BASE UINT64_C(0x80000000)
#define DATA (BASE + 1024)

static void setup(void) {
  cpu_reset(BASE);
  cpu.mtvec=BASE+128;
  sim_reset();
}

static StepResult execute(uint32_t instruction,int len) {
  CHECK(paddr_write(cpu.pc,len,instruction));
  StepResult r;
  CHECK(engine_step(&r));
  return r;
}

static void test_arithmetic(void) {
  /* GNU assembler 的明确 c.* 编码 + 独立常量期望；不用 DUT 展开计算期望。 */
  const struct {uint16_t instruction;uint8_t rd;word_t expected;} v[]={
    {0x0001,0,0},{0x0285,5,43},{0x0281,5,42},{0x3281,5,10},
    {0x52fd,5,UINT64_MAX},{0x72fd,5,UINT64_C(0xfffffffffffff000)},
    {0x717d,2,DATA-16},{0x0800,8,DATA+16},{0x907d,8,1},
    {0x947d,8,UINT64_MAX},{0x987d,8,UINT64_C(0x8000000080000003)},
    {0x8c05,8,UINT64_C(0x8000000080000001)},
    {0x8c25,8,UINT64_C(0x8000000080000001)},
    {0x8c45,8,UINT64_C(0x8000000080000003)},{0x8c65,8,2},
    {0x9c05,8,UINT64_C(0xffffffff80000001)},
    {0x9c25,8,UINT64_C(0xffffffff80000005)},
    {0x12fe,5,0},{0x829a,5,7},{0x929a,5,49}
  };
  for(size_t i=0;i<ARRLEN(v);i++) {
    setup();cpu.gpr[2]=DATA;cpu.gpr[5]=42;cpu.gpr[6]=7;
    cpu.gpr[8]=UINT64_C(0x8000000080000003);cpu.gpr[9]=2;
    StepResult r=execute(v[i].instruction,2);
    CHECK_EQ(r.raw_instruction,v[i].instruction);
    CHECK_EQ(r.raw_bytes,2);CHECK_EQ(r.instruction_length,2);
#ifdef CONFIG_RV_C
    CHECK(r.retired && !r.trap.valid);
    CHECK_EQ(cpu.gpr[v[i].rd],v[i].expected);CHECK_EQ(cpu.pc,BASE+2);
#else
    CHECK(!r.retired && r.trap.valid && !r.register_write);
    CHECK_EQ(cpu.mcause,2);CHECK_EQ(cpu.mtval,v[i].instruction);
#endif
    CHECK_EQ(cpu.gpr[0],0);
  }
}

#ifdef CONFIG_RV_C
static void test_memory_and_control(void) {
  const uint16_t instructions[]={0x5c64,0x7c64,0xdc64,0xfc64,0x52fe,0x72fe,0xdf96,0xff96};
  const int widths[]={4,8,4,8,4,8,4,8};
  const unsigned offsets[]={124,248,124,248,252,504,252,504};
  for(size_t i=0;i<ARRLEN(instructions);i++) {
    setup();cpu.gpr[2]=cpu.gpr[8]=DATA;
    cpu.gpr[5]=cpu.gpr[9]=UINT64_C(0x8877665580000003);
    CHECK(paddr_write(DATA+offsets[i],widths[i],UINT64_C(0x8877665580000003)));
    StepResult r=execute(instructions[i],2);
    CHECK(r.retired);CHECK_EQ(r.memory.vaddr,DATA+offsets[i]);
    CHECK_EQ(r.memory.length,widths[i]);
    bool is_store=i==2||i==3||i==6||i==7;
    CHECK_EQ(r.memory.write_performed,is_store);
    if(!is_store) CHECK_EQ(cpu.gpr[i<4?9:5],widths[i]==4 ?
                           UINT64_C(0xffffffff80000003) : UINT64_C(0x8877665580000003));
    setup();cpu.gpr[2]=cpu.gpr[8]=0x40000000;cpu.gpr[5]=cpu.gpr[9]=0xdeadbeef;
    r=execute(instructions[i],2);
    CHECK(!r.retired && !r.register_write && !r.memory.write_performed);
    CHECK_EQ(cpu.mcause,is_store?7:5);CHECK_EQ(cpu.mtval,0x40000000+offsets[i]);
    CHECK_EQ(cpu.gpr[i<4?9:5],0xdeadbeef);
  }
  setup();cpu.gpr[5]=BASE+7;
  CHECK(execute(0x8282,2).retired);CHECK_EQ(cpu.pc,BASE+6);
  setup();cpu.gpr[1]=BASE+7;
  CHECK(execute(0x9082,2).retired);CHECK_EQ(cpu.pc,BASE+6);CHECK_EQ(cpu.gpr[1],BASE+2);
  setup();CHECK(execute(0xa009,2).retired);CHECK_EQ(cpu.pc,BASE+2);
  setup();CHECK(execute(0xc009,2).retired);CHECK_EQ(cpu.pc,BASE+2);
  setup();cpu.gpr[8]=1;CHECK(execute(0xe009,2).retired);CHECK_EQ(cpu.pc,BASE+2);
  setup();StepResult r=execute(0x9002,2);
  CHECK(!r.retired && r.trap.valid);CHECK_EQ(cpu.mcause,3);CHECK_EQ(cpu.mtval,BASE);
  setup();cpu.pc=BASE+2;cpu.gpr[5]=42;
  r=execute(0x00128293,4);CHECK(r.retired);CHECK_EQ(cpu.pc,BASE+6);CHECK_EQ(cpu.gpr[5],43);
  CHECK_EQ(r.raw_instruction,0x00128293);CHECK_EQ(r.instruction_length,4);
}

static void test_misa(void) {
  setup();word_t supported=cpu.misa;
  CHECK(supported&MISA_C);CHECK_EQ(supported&MISA_MXL,MISA_MXL_64);
  CHECK(!(supported&(MISA_F|MISA_D|MISA_H|MISA_V|MISA_G)));
  CHECK(csr_write(CSR_MISA,UINT64_MAX));CHECK_EQ(cpu.misa,supported);
  CHECK(csr_write(CSR_MISA,0));CHECK_EQ(cpu.misa,supported&~MISA_C);
  CHECK(csr_write(CSR_MISA,supported));
  cpu.gpr[5]=MISA_C;
  StepResult r=execute(0x3012b073,4); /* csrrc x0,misa,x5 */
  CHECK(r.retired);CHECK_EQ(cpu.misa,supported&~MISA_C);
  r=execute(0x0001,2);CHECK(!r.retired);CHECK_EQ(cpu.mcause,2);
  setup();supported=cpu.misa;cpu.pc=BASE+2;cpu.gpr[5]=MISA_C;
  r=execute(0x3012b073,4);CHECK(r.retired);CHECK_EQ(cpu.misa,supported); /* snpc%4=2 */
  r=execute(0x0001,2);CHECK(r.retired);CHECK_EQ(cpu.pc,BASE+8);
}

static void test_boundary_and_illegal(void) {
  const uint16_t invalid[]={0,0x0004,0x2001,0x6001,0x6101,0x4002,0x6002,
                            0x8002,0x9c41,0x9c61,0x2000,0xa000,0x2002,0xa002};
  for(size_t i=0;i<ARRLEN(invalid);i++) {
    setup();StepResult r=execute(invalid[i],2);
    CHECK(!r.retired && !r.register_write);CHECK_EQ(cpu.mcause,2);CHECK_EQ(cpu.mtval,invalid[i]);
  }
  /* HINTs 自身不改变 GPR，不把其当成非法指令。 */
  const uint16_t hints[]={0x0005,0x4001,0x6005,0x8006,0x9006,0x0002,0x8001,0x8401};
  for(size_t i=0;i<ARRLEN(hints);i++) {
    setup();cpu.gpr[8]=0x1234;CHECK(execute(hints[i],2).retired);
    CHECK_EQ(cpu.gpr[0],0);CHECK_EQ(cpu.gpr[8],0x1234);
  }
  mem_shutdown();CHECK(mem_init(BASE,126));bus_init(BASE,126);
  setup();cpu.pc=BASE+124;StepResult r=execute(0x0001,2);
  CHECK(r.retired);CHECK_EQ(cpu.pc,BASE+126);CHECK_EQ(r.raw_bytes,2);
  setup();cpu.pc=BASE+124;r=execute(0x0013,2);
  CHECK(!r.retired && !r.instruction_valid);CHECK_EQ(cpu.mcause,1);
  CHECK_EQ(cpu.mepc,BASE+124);CHECK_EQ(cpu.mtval,BASE+126);CHECK_EQ(r.raw_bytes,2);
}
#endif

int main(void) {
  CHECK(mem_init(BASE,4096));bus_init(BASE,4096);engine_init();
  test_arithmetic();
#ifdef CONFIG_RV_C
  test_memory_and_control();test_misa();test_boundary_and_illegal();
#endif
  mem_shutdown();TEST_MAIN_END();
}
