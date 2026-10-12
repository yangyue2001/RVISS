#include <cpu/cpu.h>
#include <cpu/interrupt.h>
#include <device/char/uart16550.h>
#include <device/intc/plic.h>
#include <device/misc/test_finisher.h>
#include <device/timer/clint.h>
#include <memory/paddr.h>
#include <util/state.h>
#include "../../src/engine/engine.h"
#include "mmu.h"
#include "pmp.h"
#include "test.h"

#define BASE UINT64_C(0x80000000)
#define UART UINT64_C(0x10000000)
#define PLIC UINT64_C(0xc000000)
#define CLINT UINT64_C(0x2000000)
typedef struct { bool level; } LineState;
static void line_set(void *opaque, bool level) { ((LineState *)opaque)->level = level; }
#if defined(CONFIG_HAS_UART16550) || defined(CONFIG_HAS_CLINT) || defined(CONFIG_HAS_PLIC) || defined(CONFIG_HAS_TEST_FINISHER)
static word_t read_reg(paddr_t addr, int len) {
  word_t v=0; CHECK(mmio_dispatch_read(addr,len,&v)); return v;
}
static void write_reg(paddr_t addr, int len, word_t v) { CHECK(mmio_dispatch_write(addr,len,v)); }
static void reset_bus(void) { bus_init(BASE,4096); }
#endif

#ifdef CONFIG_HAS_UART16550
static uint8_t tx[32]; static unsigned tx_count;
static void tx_put(void *opaque, uint8_t c) { (void)opaque; if (tx_count < sizeof(tx)) tx[tx_count++]=c; }
static void test_uart(void) {
  reset_bus(); Uart16550State u; LineState irq={0};
  IrqLine line={.set=line_set,.ctx=&irq};
  uart16550_init(&u);u.irq=&line;u.transmit=tx_put;CHECK(mmio_map(&u.mmio,UART));
  CHECK_EQ(read_reg(UART+5,1),0x60);CHECK_EQ(read_reg(UART+2,1),1);
  write_reg(UART+3,1,0x80);write_reg(UART,1,2);write_reg(UART+1,1,0x12);
  CHECK_EQ(read_reg(UART,1),2);CHECK_EQ(read_reg(UART+1,1),0x12);CHECK_EQ(tx_count,0);
  write_reg(UART+3,1,3);CHECK_EQ(read_reg(UART+1,1),0);
  write_reg(UART+1,1,0xff);CHECK_EQ(read_reg(UART+1,1),15);CHECK(irq.level);
  CHECK_EQ(read_reg(UART+2,1),2);CHECK(!irq.level);CHECK_EQ(read_reg(UART+2,1),1);
  write_reg(UART,1,'A');CHECK_EQ(tx_count,1);CHECK_EQ(tx[0],'A');CHECK(irq.level);
  CHECK_EQ(read_reg(UART+2,1),2);CHECK(!irq.level);
  CHECK(uart16550_receive(&u,'x'));CHECK(!uart16550_receive(&u,'y'));
  CHECK_EQ(read_reg(UART+2,1),6);CHECK_EQ(read_reg(UART+5,1),0x63);
  CHECK_EQ(read_reg(UART+2,1),4);CHECK_EQ(read_reg(UART,1),'y');CHECK(!irq.level);
  const uint8_t fcr[]={1,0x41,0x81,0xc1};const unsigned thresholds[]={1,4,8,14};
  for(unsigned k=0;k<4;k++) {
    write_reg(UART+2,1,fcr[k]|6);write_reg(UART+1,1,1);
    for(unsigned i=0;i<thresholds[k];i++) {
      CHECK(uart16550_receive(&u,(uint8_t)i));CHECK_EQ(irq.level,i+1>=thresholds[k]);
    }
    CHECK_EQ(read_reg(UART+2,1),0xc4);
    for(unsigned i=0;i<thresholds[k];i++) CHECK_EQ(read_reg(UART,1),i);
    CHECK(!irq.level);CHECK_EQ(read_reg(UART+2,1),0xc1);
  }
  write_reg(UART+2,1,0x47);
  for(unsigned i=0;i<16;i++) CHECK(uart16550_receive(&u,(uint8_t)i));
  CHECK(!uart16550_receive(&u,99));CHECK_EQ(u.count,16);
  write_reg(UART+1,1,5);CHECK_EQ(read_reg(UART+2,1),0xc6);
  CHECK_EQ(read_reg(UART+5,1),0x63);
  for(unsigned i=0;i<16;i++) CHECK_EQ(read_reg(UART,1),i);
  /* 8N1 at 115200 baud: ceil(4 * 10 / 115200 * 10MHz *128)=444445 ticks. */
  write_reg(UART+3,1,0x83);write_reg(UART,1,2);write_reg(UART+1,1,0);write_reg(UART+3,1,3);
  write_reg(UART+1,1,1);write_reg(UART+2,1,0x47);
  CHECK(uart16550_receive(&u,'T'));CHECK(!irq.level);CHECK_EQ(devices_next_deadline(),444445);
  devices_tick(444444);CHECK(!irq.level);devices_tick(444445);CHECK(irq.level);
  CHECK_EQ(read_reg(UART+2,1),0xcc);CHECK_EQ(read_reg(UART,1),'T');CHECK(!irq.level);
  CHECK_EQ(devices_next_deadline(),UINT64_MAX);
  CHECK(uart16550_receive(&u,'a'));devices_tick(444500);CHECK_EQ(read_reg(UART,1),'a');
  CHECK(uart16550_receive(&u,'b'));devices_tick(888944);CHECK(!irq.level);
  devices_tick(888945);CHECK(irq.level);CHECK_EQ(read_reg(UART,1),'b');
  write_reg(UART+2,1,0xc0);CHECK_EQ(u.fcr,0);CHECK_EQ(read_reg(UART+2,1),1);
  write_reg(UART+2,1,7);CHECK(uart16550_receive(&u,'q'));
  unsigned before=u.count;word_t ignored=123;
  CHECK(!mmio_dispatch_read(UART,4,&ignored));CHECK_EQ(ignored,123);CHECK_EQ(u.count,before);
  CHECK(!mmio_dispatch_write(UART,4,'!'));CHECK_EQ(tx_count,1);
  CHECK(!mmio_dispatch_read(UART+8,1,&ignored));
  write_reg(UART+2,1,7);write_reg(UART+1,1,8);write_reg(UART+4,1,0x1f);
  CHECK_EQ(read_reg(UART+6,1)&0xf0,0xf0);CHECK(!irq.level);
  write_reg(UART+4,1,0x10);CHECK(irq.level);CHECK_EQ(read_reg(UART+2,1),0xc0);
  CHECK_EQ(read_reg(UART+6,1)&0xf0,0);CHECK(!irq.level);
  write_reg(UART,1,'L');CHECK_EQ(read_reg(UART,1),'L');CHECK_EQ(tx_count,1);
  write_reg(UART+7,1,0xa5);CHECK_EQ(read_reg(UART+7,1),0xa5);
  uart16550_reset(&u);CHECK(!irq.level);CHECK_EQ(read_reg(UART+5,1),0x60);CHECK_EQ(u.count,0);
}
#endif

#ifdef CONFIG_HAS_PLIC
static void test_plic(void) {
  reset_bus();PlicState p;LineState m={0},s={0};
  IrqLine ml={.set=line_set,.ctx=&m},sl={.set=line_set,.ctx=&s};
  plic_init(&p);p.meip_irq=&ml;p.seip_irq=&sl;plic_reset(&p);CHECK(mmio_map(&p.mmio,PLIC));
  write_reg(PLIC,4,7);CHECK_EQ(read_reg(PLIC,4),0);
  write_reg(PLIC+40,4,0xff);CHECK_EQ(read_reg(PLIC+40,4),7);write_reg(PLIC+40,4,3);
  write_reg(PLIC+44,4,3);write_reg(PLIC+48,4,5);
  write_reg(PLIC+0x2000,4,0xffffffff);CHECK_EQ(read_reg(PLIC+0x2000,4),0xfffffffe);
  write_reg(PLIC+0x2080,4,1<<10);write_reg(PLIC+0x200000,4,7);
  plic_set_source(&p,10,true);plic_set_source(&p,11,true);plic_set_source(&p,12,true);
  CHECK(!m.level);CHECK(s.level);CHECK_EQ(read_reg(PLIC+0x200004,4),12);
  CHECK_EQ(read_reg(PLIC+0x200004,4),10);CHECK(!s.level);
  CHECK_EQ(read_reg(PLIC+0x201004,4),0);CHECK_EQ(read_reg(PLIC+0x200004,4),11);
  CHECK_EQ(read_reg(PLIC+0x1000,4),0);CHECK_EQ(read_reg(PLIC+0x200004,4),0);
  for(unsigned i=10;i<=12;i++) plic_set_source(&p,i,false);
  /* No owner/last-claim restriction: another enabled context may complete. */
  write_reg(PLIC+0x201004,4,10);plic_set_source(&p,10,true);CHECK(s.level);
  CHECK_EQ(read_reg(PLIC+0x201004,4),10);CHECK(!s.level);
  write_reg(PLIC+0x2080,4,0);write_reg(PLIC+0x201004,4,10);
  plic_set_source(&p,10,false);plic_set_source(&p,10,true);CHECK_EQ(p.pending,0);
  write_reg(PLIC+0x2080,4,1<<10);write_reg(PLIC+0x201004,4,10);CHECK(s.level);
  /* Falling level cannot retract a request, and claiming cannot reopen gateway. */
  plic_set_source(&p,10,false);CHECK(s.level);CHECK_EQ(read_reg(PLIC+0x201004,4),10);
  plic_set_source(&p,10,true);CHECK(!s.level);write_reg(PLIC+0x201004,4,10);CHECK(s.level);
  plic_set_source(&p,10,false);CHECK_EQ(read_reg(PLIC+0x201004,4),10);
  write_reg(PLIC+0x201004,4,10);CHECK(!s.level);
  write_reg(PLIC+0x200000,4,2);plic_set_source(&p,11,true);CHECK_EQ(p.pending,0);
  write_reg(PLIC+0x200004,4,11);CHECK(m.level);
  word_t ignored=9;CHECK(!mmio_dispatch_read(PLIC+0x200004,8,&ignored));CHECK_EQ(ignored,9);
  CHECK(m.level);CHECK_EQ(read_reg(PLIC+0x200004,4),11);
  plic_set_source(&p,0,true);plic_set_source(&p,32,true);CHECK(!(p.pending&1));
  plic_reset(&p);CHECK(!m.level&&!s.level);CHECK_EQ(p.pending,0);
}
#endif

#ifdef CONFIG_HAS_CLINT
static void test_clint(void) {
  reset_bus();ClintState c;LineState ti={0},si={0};
  IrqLine tl={.set=line_set,.ctx=&ti},sl={.set=line_set,.ctx=&si};
  clint_init(&c);c.mtimer_irq=&tl;c.msip_irq=&sl;clint_reset(&c);CHECK(mmio_map(&c.mmio,CLINT));
  write_reg(CLINT+0x4000,8,UINT64_C(0x1122334455667788));
  CHECK_EQ(read_reg(CLINT+0x4000,4),0x55667788);CHECK_EQ(read_reg(CLINT+0x4004,4),0x11223344);
  write_reg(CLINT+0x4004,4,0xaabbccdd);CHECK_EQ(read_reg(CLINT+0x4000,8),UINT64_C(0xaabbccdd55667788));
  write_reg(CLINT+0x4000,4,7);CHECK_EQ(read_reg(CLINT+0x4000,8),UINT64_C(0xaabbccdd00000007));
  write_reg(CLINT+0xbff8,8,UINT64_C(0x5566778811223344));
  CHECK_EQ(read_reg(CLINT+0xbffc,4),0x55667788);write_reg(CLINT+0xbff8,4,0x99);
  CHECK_EQ(read_reg(CLINT+0xbff8,8),UINT64_C(0x5566778800000099));
  write_reg(CLINT,4,0xffffffff);CHECK(si.level);CHECK_EQ(read_reg(CLINT,4),1);
  write_reg(CLINT,4,2);CHECK(!si.level);clint_reset(&c);
  write_reg(CLINT+0x4000,8,3);devices_tick(127);CHECK_EQ(c.mtime,0);CHECK_EQ(devices_next_deadline(),384);
  devices_tick(128);CHECK_EQ(c.mtime,1);devices_tick(383);CHECK_EQ(c.mtime,2);CHECK(!ti.level);
  devices_tick(384);CHECK_EQ(c.mtime,3);CHECK(ti.level);devices_tick(384);CHECK_EQ(c.mtime,3);
  write_reg(CLINT+0x4000,8,4);CHECK(!ti.level);devices_tick(512);CHECK(ti.level);
  word_t ignored=123;CHECK(!mmio_dispatch_read(CLINT+0x4004,8,&ignored));CHECK_EQ(ignored,123);
  CHECK(!mmio_dispatch_write(CLINT,8,0));CHECK_EQ(c.msip,0);
  CHECK(!mmio_dispatch_write(CLINT+0x4002,2,0));CHECK_EQ(c.mtimecmp,4);
  clint_reset(&c);CHECK(!ti.level&&!si.level);CHECK_EQ(c.mtimecmp,UINT64_MAX);
}
static void test_wfi(void) {
  reset_bus();cpu_reset(BASE);riscv_pmp_set_regions(0);riscv_mmu_set_sv39(false);
  ClintState c;clint_init(&c);c.mtimer_irq=cpu_irq_input(IRQ_MTIP);CHECK(mmio_map(&c.mmio,CLINT));
  cpu.mie=MIE_MTIE;cpu.mtvec=BASE+256;cpu.mstatus|=MSTATUS_MIE;
  sim_reset();CHECK(paddr_write(BASE,4,0x10500073));engine_init();
  write_reg(CLINT+0x4000,8,3);StepResult r;CHECK(engine_step(&r));
  CHECK(r.retired&&r.wait_request);CHECK_EQ(sim.instret,1);CHECK_EQ(sim.attempts,1);
  CHECK_EQ(sim.logical_ticks,384);CHECK_EQ(sim.idle_ticks,383);CHECK_EQ(sim.idle_advances,1);
  CHECK_EQ(cpu.cycle,1);CHECK_EQ(cpu.instret,1);CHECK(engine_step(&r));
  CHECK_EQ(r.outcome,STEP_INTERRUPT);CHECK_EQ(r.trap.cause,IRQ_BIT|IRQ_MTIP);
  CHECK_EQ(sim.attempts,1);CHECK_EQ(cpu.cycle,2);
  /* Individually enabled IRQ resumes WFI even while global MIE is clear. */
  cpu.pc=BASE;cpu.mstatus&=~MSTATUS_MIE;CHECK(engine_step(&r));CHECK(r.retired);
  CHECK_EQ(sim.logical_ticks,386);CHECK_EQ(sim.idle_advances,1);
  write_reg(CLINT+0x4000,8,UINT64_MAX);
  cpu.mstatus|=MSTATUS_TW;cpu.priv=PRV_S;cpu.pc=BASE;cpu.medeleg=0;
  CHECK(engine_step(&r));CHECK_EQ(r.outcome,STEP_SYNCHRONOUS_TRAP);CHECK(!r.wait_request);
}
#endif

#ifdef CONFIG_HAS_TEST_FINISHER
static void finish(void *opaque,int code) { *(int *)opaque=code; }
static void test_finisher(void) {
  reset_bus();TestFinisherState f;int result=-1;test_finisher_init(&f);f.finish=finish;f.finish_opaque=&result;
  CHECK(mmio_map(&f.mmio,0x100000));write_reg(0x100000,2,0x5555);CHECK_EQ(result,0);
  write_reg(0x100000,4,(7u<<16)|0x3333);CHECK_EQ(result,7);
  write_reg(0x100000,4,0x3333);CHECK_EQ(result,1);write_reg(0x100000,4,0xabcd);CHECK_EQ(result,1);
  CHECK(!mmio_dispatch_write(0x100000,8,0x5555));CHECK_EQ(result,1);
}
#endif

#if defined(CONFIG_HAS_UART16550) && defined(CONFIG_HAS_PLIC)
static void uart_to_plic(void *opaque,bool level) { plic_set_source(opaque,10,level); }
static void test_uart_cpu(void) {
  reset_bus();cpu_reset(BASE);riscv_pmp_set_regions(0);riscv_mmu_set_sv39(false);sim_reset();
  Uart16550State u;PlicState p;plic_init(&p);uart16550_init(&u);
  IrqLine uart_line={.set=uart_to_plic,.ctx=&p};u.irq=&uart_line;
  p.seip_irq=cpu_irq_input(IRQ_SEIP);CHECK(mmio_map(&u.mmio,UART));CHECK(mmio_map(&p.mmio,PLIC));
  write_reg(PLIC+40,4,1);write_reg(PLIC+0x2080,4,1<<10);write_reg(UART+1,1,1);
  CHECK(paddr_write(BASE,4,0x00034283)); /* lbu x5,0(x6) */
  cpu.gpr[6]=UART;cpu.gpr[5]=0x1234;cpu.priv=PRV_S;
  cpu.mideleg=MIE_SEIE;cpu.mie=MIE_SEIE;cpu.mstatus|=MSTATUS_SIE;cpu.stvec=BASE;
  CHECK(uart16550_receive(&u,'R'));engine_init();StepResult r;CHECK(engine_step(&r));
  CHECK_EQ(r.outcome,STEP_INTERRUPT);CHECK_EQ(r.trap.cause,IRQ_BIT|IRQ_SEIP);
  CHECK(!r.instruction_attempted&&!r.instruction_valid);CHECK_EQ(cpu.gpr[5],0x1234);CHECK_EQ(u.count,1);
  CHECK_EQ(read_reg(PLIC+0x201004,4),10);CHECK(engine_step(&r));
  CHECK(r.retired&&r.memory.mmio);CHECK_EQ(cpu.gpr[5],'R');CHECK_EQ(u.count,0);
  write_reg(PLIC+0x201004,4,10);cpu_sync_irq_inputs();CHECK(!(cpu.mip&MIE_SEIE));
  /* Failed wide RBR load cannot consume a byte or write destination. */
  CHECK(uart16550_receive(&u,'Q'));cpu.mie=0;cpu.pc=BASE;cpu.gpr[5]=0x1234;
  CHECK(paddr_write(BASE,4,0x00032283));CHECK(engine_step(&r));
  CHECK_EQ(r.outcome,STEP_SYNCHRONOUS_TRAP);CHECK_EQ(r.trap.cause,EX_LAF);
  CHECK_EQ(r.trap.tval,UART);CHECK_EQ(u.count,1);CHECK_EQ(cpu.gpr[5],0x1234);
}
#endif
#ifdef CONFIG_HAS_TEST_FINISHER
static void finish_cpu(void *opaque,int code) { (void)opaque;sim_halt(cpu.pc,code); }
static void test_finisher_cpu(void) {
  reset_bus();cpu_reset(BASE);riscv_pmp_set_regions(0);riscv_mmu_set_sv39(false);sim_reset();
  TestFinisherState f;test_finisher_init(&f);f.finish=finish_cpu;CHECK(mmio_map(&f.mmio,0x100000));
  cpu.gpr[5]=0x5555;cpu.gpr[6]=0x100000;CHECK(paddr_write(BASE,4,0x00532023));engine_init();
  StepResult r;CHECK(engine_step(&r));CHECK(r.retired&&r.memory.write_performed);
  CHECK_EQ(r.outcome,STEP_HALTED);CHECK_EQ(sim.halt_pc,BASE);CHECK_EQ(sim.halt_code,0);
}
#endif
int main(void) {
  CHECK(mem_init(BASE,4096));
#ifdef CONFIG_HAS_UART16550
  test_uart();
#endif
#ifdef CONFIG_HAS_PLIC
  test_plic();
#endif
#ifdef CONFIG_HAS_CLINT
  test_clint();test_wfi();
#endif
#ifdef CONFIG_HAS_TEST_FINISHER
  test_finisher();test_finisher_cpu();
#endif
#if defined(CONFIG_HAS_UART16550) && defined(CONFIG_HAS_PLIC)
  test_uart_cpu();
#endif
  mem_shutdown();TEST_MAIN_END();
}
