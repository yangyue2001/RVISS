#include <cpu/cpu.h>
#include <cpu/csr_list.h>
#include <memory/paddr.h>
#include <memory/vaddr.h>
#include <device/device.h>
#include <util/state.h>
#include "../../src/engine/engine.h"
#include "csr.h"
#include "pmp.h"
#include "fp.h"
#include "test.h"

#define BASE UINT64_C(0x80000000)
#define DATA (BASE+1024)
#define BOX UINT64_C(0xffffffff00000000)

static void setup(void) {
  cpu_reset(BASE); sim_reset(); riscv_pmp_set_regions(0); cpu.mtvec=BASE+256;
  cpu.mstatus=csr_status_value(MSTATUS_FS_INITIAL);
  cpu.fpr[5]=BOX|0x12345678; cpu.gpr[5]=0xdeadbeef; cpu.gpr[6]=DATA;
}
static StepResult execute(uint32_t raw) {
  CHECK(paddr_write(BASE,4,raw)); StepResult r; CHECK(engine_step(&r)); return r;
}
#ifdef CONFIG_RV_F
static uint32_t op(unsigned fn, bool d, unsigned rm, unsigned rs2) {
  return (fn<<27)|(d<<25)|(rs2<<20)|(1<<15)|(rm<<12)|(5<<7)|0x53;
}
static void test_constants(void) {
  /* Expected IEEE bit patterns are literal constants, never SoftFloat calls. */
  struct {unsigned fn; uint32_t a,b,out; unsigned flags;} cases[]={
    {0,0x3fc00000,0x40100000,0x40700000,0},
    {1,0x3fc00000,0x40100000,0xbf400000,0},
    {2,0x3fc00000,0x40100000,0x40580000,0},
    {3,0x3fc00000,0x40100000,0x3f2aaaab,1},
    {11,0x40800000,0,0x40000000,0},
    {3,0x3f800000,0,0x7f800000,8},
    {3,0,0,0x7fc00000,16},
    {0,0x7f800000,0xff800000,0x7fc00000,16},
    {2,0x7f7fffff,0x40000000,0x7f800000,5},
    {3,1,0x40000000,0,3},
    {3,0x00800000,0x40000000,0x00400000,0},
    {2,0x00800000,0x3f7fffff,0x00800000,3}, /* unbounded exponent: tiny despite final normal */
    {2,0x007fffff,0x3f800001,0x00800000,1}, /* rounds normal at full precision: no UF */
    {0,0x7fc12345,0x3f800000,0x7fc00000,0},
    {0,0x7f812345,0x3f800000,0x7fc00000,16},
    {11,0xbf800000,0,0x7fc00000,16},
    {0,0x80000000,0x80000000,0x80000000,0},
  };
  for(unsigned i=0;i<ARRLEN(cases);i++) {
    setup(); cpu.fpr[1]=BOX|cases[i].a; cpu.fpr[2]=BOX|cases[i].b;
    StepResult r=execute(op(cases[i].fn,false,0,cases[i].fn==11?0:2));
    CHECK(r.retired); CHECK_EQ((uint32_t)cpu.fpr[5],cases[i].out);
    CHECK(r.register_write && r.register_is_fp); CHECK_EQ(r.rd,5);
    CHECK_EQ((uint32_t)r.register_value,cases[i].out);
    CHECK_EQ(cpu.fcsr,cases[i].flags); CHECK_EQ(cpu.mstatus&MSTATUS_FS,MSTATUS_FS_DIRTY);
#ifdef CONFIG_RV_D
    CHECK_EQ(cpu.fpr[5]>>32,UINT32_MAX);
#endif
  }
  const uint32_t rounding_result[]={0x3f800000,0x3f800000,0x3f800000,0x3f800001,0x3f800001};
  for(unsigned mode=0;mode<5;mode++) for(unsigned dynamic=0;dynamic<2;dynamic++) {
    setup(); cpu.fpr[1]=BOX|0x3f800000; cpu.fpr[2]=BOX|0x33800000;
    cpu.fcsr=mode<<5;
    CHECK(execute(op(0,false,dynamic?7:mode,2)).retired);
    CHECK_EQ((uint32_t)cpu.fpr[5],rounding_result[mode]); CHECK_EQ(cpu.fcsr,(mode<<5)|1);
  }
  setup(); cpu.fpr[1]=BOX|0x3f800001; cpu.fpr[2]=BOX|0x3f7ffffe;cpu.fpr[3]=BOX|0xbf800000;
  CHECK(execute((3u<<27)|(2<<20)|(1<<15)|(5<<7)|0x43).retired);
  CHECK_EQ((uint32_t)cpu.fpr[5],0xa8800000); CHECK_EQ(cpu.fcsr,0); /* single-round fused -2^-46 */
  setup(); cpu.fpr[1]=BOX;cpu.fpr[2]=BOX|0x7f800000;cpu.fpr[3]=BOX|0x7fc12345;
  CHECK(execute((3u<<27)|(2<<20)|(1<<15)|(5<<7)|0x43).retired);
  CHECK_EQ((uint32_t)cpu.fpr[5],0x7fc00000); CHECK_EQ(cpu.fcsr,16);
}
static void test_nonrounding(void) {
  struct {unsigned fn,rm;uint32_t a,b,out,flags;bool gpr;} cases[]={
    {5,0,0,0x80000000,0x80000000,0,false}, {5,1,0,0x80000000,0,0,false},
    {5,0,0x7fc12345,0x3f800000,0x3f800000,0,false},
    {5,1,0x7f812345,0x3f800000,0x3f800000,16,false},
    {5,0,0x7fc12345,0xffc12345,0x7fc00000,0,false},
    {4,0,0x7fc12345,0xbf800000,0xffc12345,0,false},
    {4,1,0x3f800000,0xbf800000,0x3f800000,0,false},
    {4,2,0xbf800000,0xbf800000,0x3f800000,0,false},
    {20,2,0x7fc12345,0x3f800000,0,0,true},
    {20,1,0x7fc12345,0x3f800000,0,16,true},
    {20,2,0x7f812345,0x3f800000,0,16,true},
    {20,2,0,0x80000000,1,0,true},
    {20,0,0xbf800000,0x3f800000,1,0,true},
  };
  for(unsigned i=0;i<ARRLEN(cases);i++) {
    setup();cpu.fpr[1]=BOX|cases[i].a;cpu.fpr[2]=BOX|cases[i].b;
    CHECK(execute(op(cases[i].fn,false,cases[i].rm,2)).retired);
    CHECK_EQ(cases[i].gpr?cpu.gpr[5]:(uint32_t)cpu.fpr[5],cases[i].out);
    CHECK_EQ(cpu.fcsr,cases[i].flags);
    CHECK_EQ(cpu.mstatus&MSTATUS_FS,cases[i].gpr&&!cases[i].flags?MSTATUS_FS_INITIAL:MSTATUS_FS_DIRTY);
  }
  const uint32_t classes[]={0xff800000,0xbf800000,0x80000001,0x80000000,0,1,0x3f800000,0x7f800000,0x7f812345,0x7fc12345};
  for(unsigned n=0;n<10;n++) {
    setup();cpu.fpr[1]=BOX|classes[n]; CHECK(execute(op(28,false,1,0)).retired);
    CHECK_EQ(cpu.gpr[5],1u<<n); CHECK_EQ(cpu.fcsr,0);
    CHECK_EQ(cpu.mstatus&MSTATUS_FS,MSTATUS_FS_INITIAL);
  }
  setup();cpu.fpr[1]=UINT64_C(0x1234567887654321);
  CHECK(execute(op(28,false,0,0)).retired);CHECK_EQ(cpu.gpr[5],UINT64_C(0xffffffff87654321));
  CHECK_EQ(cpu.mstatus&MSTATUS_FS,MSTATUS_FS_INITIAL); /* raw move ignores boxing */
  setup();cpu.gpr[1]=UINT64_C(0x1234567887654321);
  StepResult r=execute(op(30,false,0,0)&~(31u<<7)); CHECK(r.retired);
  CHECK(r.register_write && r.register_is_fp); CHECK_EQ(r.rd,0);
  CHECK_EQ((uint32_t)cpu.fpr[0],0x87654321);
  setup();cpu.fpr[1]=BOX|0x3fc00000; r=execute(op(24,false,0,0));
  CHECK(r.register_write && !r.register_is_fp); CHECK_EQ(r.rd,5); CHECK_EQ(r.register_value,2);
  setup();cpu.fpr[1]=BOX|0x3fc00000; r=execute(op(24,false,0,0)&~(31u<<7));
  CHECK(r.retired && !r.register_write); CHECK_EQ(cpu.gpr[0],0); CHECK_EQ(cpu.fcsr,1);
#ifdef CONFIG_RV_D
  setup();cpu.fpr[1]=0x3fc00000;cpu.fpr[2]=BOX|0x40100000;
  CHECK(execute(op(0,false,0,2)).retired);CHECK_EQ(cpu.fpr[5],BOX|0x7fc00000);CHECK_EQ(cpu.fcsr,0);
  setup();cpu.fpr[1]=0x3fc00000;
  CHECK(execute(op(28,false,1,0)).retired);CHECK_EQ(cpu.gpr[5],1u<<9);
#endif
}
static void test_conversions_and_illegal(void) {
  const word_t rounded[]={2,1,1,2,2};
  for(unsigned mode=0;mode<5;mode++) {
    setup();cpu.fpr[1]=BOX|0x3fc00000;
    CHECK(execute(op(24,false,mode,0)).retired);CHECK_EQ(cpu.gpr[5],rounded[mode]);CHECK_EQ(cpu.fcsr,1);
  }
  const uint32_t input[]={0x7fc12345,0x7f800000,0xff800000,0xbf800000};
  const word_t signed_out[]={0x7fffffff,0x7fffffff,UINT64_C(0xffffffff80000000),UINT64_MAX};
  const word_t unsigned_out[]={UINT64_MAX,UINT64_MAX,0,0};
  for(unsigned n=0;n<4;n++) for(unsigned unsign=0;unsign<2;unsign++) {
    setup();cpu.fpr[1]=BOX|input[n];CHECK(execute(op(24,false,0,unsign)).retired);
    CHECK_EQ(cpu.gpr[5],unsign?unsigned_out[n]:signed_out[n]);
    CHECK_EQ(cpu.fcsr,n==3&&!unsign?0:16);
  }
  setup();cpu.gpr[1]=UINT64_MAX;CHECK(execute(op(26,false,0,0)).retired);
  CHECK_EQ((uint32_t)cpu.fpr[5],0xbf800000);CHECK_EQ(cpu.fcsr,0);
  setup();cpu.gpr[1]=UINT64_C(0xffffffffffffffff);CHECK(execute(op(26,false,0,1)).retired);
  CHECK_EQ((uint32_t)cpu.fpr[5],0x4f800000);CHECK_EQ(cpu.fcsr,1);
  for(unsigned rm=5;rm<8;rm++) for(unsigned frm=5;frm<8;frm++) {
    setup();cpu.fcsr=frm<<5;word_t before=cpu.fpr[5];
    CHECK(!execute(op(0,false,rm,2)).retired);CHECK_EQ(cpu.mcause,2);CHECK_EQ(cpu.fpr[5],before);
    CHECK_EQ(cpu.fcsr,frm<<5);CHECK_EQ(cpu.mstatus&MSTATUS_FS,MSTATUS_FS_INITIAL);
  }
  setup();cpu.mstatus=csr_status_value(0);word_t before=cpu.fpr[5];
  CHECK(!execute(0x00033287).retired);CHECK_EQ(cpu.mcause,2);CHECK_EQ(cpu.fpr[5],before);
  setup();CHECK(!execute(op(11,false,0,1)).retired);CHECK_EQ(cpu.mcause,2); /* sqrt rs2 reserved */
  setup();CHECK(!execute(op(24,false,0,4)).retired);CHECK_EQ(cpu.mcause,2);
}
static void test_csr_and_memory(void) {
  setup();word_t v;CHECK(csr_write(CSR_FCSR,UINT64_MAX));CHECK_EQ(cpu.fcsr,255);
  CHECK(csr_read(CSR_FFLAGS,&v));CHECK_EQ(v,31);CHECK(csr_read(CSR_FRM,&v));CHECK_EQ(v,7);
  CHECK(csr_write(CSR_FFLAGS,2));CHECK_EQ(cpu.fcsr,226);CHECK(csr_write(CSR_FRM,1));CHECK_EQ(cpu.fcsr,34);
  CHECK_EQ(cpu.mstatus&MSTATUS_FS,MSTATUS_FS_DIRTY);
  cpu.mstatus=csr_status_value(0);CHECK(!csr_read(CSR_FCSR,&v));CHECK(!csr_write(CSR_FFLAGS,0));
  setup();CHECK(paddr_write(DATA,4,0x7f812345));CHECK(execute(0x00032287).retired);
  CHECK_EQ((uint32_t)cpu.fpr[5],0x7f812345);CHECK_EQ(cpu.fcsr,0);
  setup();cpu.fpr[1]=UINT64_C(0x1234567887654321);CHECK(execute(0x00132027).retired);
  CHECK_EQ(paddr_read(DATA,4),0x87654321);CHECK_EQ(cpu.mstatus&MSTATUS_FS,MSTATUS_FS_INITIAL);
  setup();cpu.gpr[6]=DATA+1;word_t old=cpu.fpr[5];CHECK(!execute(0x00032287).retired);
  CHECK_EQ(cpu.mcause,4);CHECK_EQ(cpu.mtval,DATA+1);CHECK_EQ(cpu.fpr[5],old);
  CHECK_EQ(cpu.mstatus&MSTATUS_FS,MSTATUS_FS_INITIAL);
  setup();cpu.gpr[6]=BASE+4096;CHECK(!execute(0x00132027).retired);CHECK_EQ(cpu.mcause,7);
}
#ifdef CONFIG_RV_D
static void test_double(void) {
  const word_t a=UINT64_C(0x3ff8000000000000),b=UINT64_C(0x4002000000000000);
  const word_t out[]={UINT64_C(0x400e000000000000),UINT64_C(0xbfe8000000000000),
                     UINT64_C(0x400b000000000000),UINT64_C(0x3fe5555555555555)};
  for(unsigned fn=0;fn<4;fn++) {
    setup();cpu.fpr[1]=a;cpu.fpr[2]=b;CHECK(execute(op(fn,true,0,2)).retired);
    CHECK_EQ(cpu.fpr[5],out[fn]);CHECK_EQ(cpu.fcsr,fn==3?1:0);
  }
  setup();cpu.fpr[1]=BOX|0x3fc00000;CHECK(execute(op(8,true,0,0)).retired);CHECK_EQ(cpu.fpr[5],a);
  setup();cpu.fpr[1]=a;CHECK(execute(op(8,false,0,1)).retired);CHECK_EQ(cpu.fpr[5],BOX|0x3fc00000);
  setup();cpu.fpr[1]=UINT64_C(0x4170000010000000);CHECK(execute(op(8,false,0,1)).retired);
  CHECK_EQ(cpu.fpr[5],BOX|0x4b800000);CHECK_EQ(cpu.fcsr,1); /* 16777217 -> nearest even */
  setup();cpu.fpr[1]=UINT64_C(0x7ff8123456789abc);CHECK(execute(op(24,true,0,2)).retired);
  CHECK_EQ(cpu.gpr[5],UINT64_C(0x7fffffffffffffff));CHECK_EQ(cpu.fcsr,16);
  setup();cpu.fpr[1]=UINT64_C(0xfff0000000000000);CHECK(execute(op(24,true,0,3)).retired);
  CHECK_EQ(cpu.gpr[5],0);CHECK_EQ(cpu.fcsr,16);
  setup();cpu.fpr[1]=UINT64_C(0x7ff123456789abcd);CHECK(execute(op(28,true,0,0)).retired);
  CHECK_EQ(cpu.gpr[5],UINT64_C(0x7ff123456789abcd));CHECK_EQ(cpu.fcsr,0);
  setup();CHECK(paddr_write(DATA,8,UINT64_C(0x7ff123456789abcd)));CHECK(execute(0x00033287).retired);
  CHECK_EQ(cpu.fpr[5],UINT64_C(0x7ff123456789abcd));CHECK_EQ(cpu.fcsr,0);
#ifdef CONFIG_RV_C
  setup();cpu.gpr[2]=DATA;CHECK(paddr_write(DATA,8,b));CHECK(execute(0x2002).retired);
  CHECK_EQ(cpu.fpr[0],b); /* C.FLDSP f0 is legal */
  setup();cpu.gpr[8]=DATA;cpu.fpr[8]=a;CHECK(execute(0xa000).retired);CHECK_EQ(paddr_read(DATA,8),a);
#endif
}
#endif
#endif
int main(void) {
  CHECK(mem_init(BASE,4096));bus_init(BASE,4096);engine_init();
#ifdef CONFIG_RV_F
  test_constants();test_nonrounding();test_conversions_and_illegal();test_csr_and_memory();
#ifdef CONFIG_RV_D
  test_double();
#else
  setup();CHECK(!execute(0x020002d3).retired);CHECK_EQ(cpu.mcause,2);
#endif
#else
  setup();word_t v;CHECK(!csr_read(CSR_FCSR,&v));CHECK(!csr_write(CSR_FRM,0));
  CHECK(!execute(0x002082d3).retired);CHECK_EQ(cpu.mcause,2);
#endif
  mem_shutdown();TEST_MAIN_END();
}
