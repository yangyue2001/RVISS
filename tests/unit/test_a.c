#include <common.h>
#include <cpu/cpu.h>
#include <device/device.h>
#include <memory/paddr.h>
#include <util/state.h>
#include "../../src/engine/engine.h"
#include "test.h"

#define BASE UINT64_C(0x80000000)
#define DATA (BASE + 512)

static uint32_t encoding(unsigned op, int len, unsigned rd, unsigned ordering) {
  return (op << 27) | (ordering << 25) | ((op == 2 ? 0u : 7u) << 20) |
         (6u << 15) | ((len == 4 ? 2u : 3u) << 12) | (rd << 7) | 0x2f;
}

static void setup(void) {
  cpu_reset(BASE);
  cpu.mtvec = BASE + 128;
  cpu.gpr[5] = 0xdeadbeef;
  cpu.gpr[6] = DATA;
  sim_reset();
}

static StepResult execute(uint32_t instruction) {
  CHECK(paddr_write(cpu.pc, 4, instruction));
  StepResult r;
  CHECK(engine_step(&r));
  return r;
}

static void test_operations(void) {
  /* Known constants for old=MIN+3, value=MAX-1 at each width. */
  const unsigned ops[] = {0,1,4,8,12,16,20,24,28,2,3};
#ifdef CONFIG_RV_A
  const word_t w[] = {1,0x7ffffffe,0xfffffffd,0xffffffff,2,
                      0x80000003,0x7ffffffe,0x7ffffffe,0x80000003};
  const word_t d[] = {1,UINT64_C(0x7ffffffffffffffe),UINT64_C(0xfffffffffffffffd),
                      UINT64_MAX,2,UINT64_C(0x8000000000000003),
                      UINT64_C(0x7ffffffffffffffe),UINT64_C(0x7ffffffffffffffe),
                      UINT64_C(0x8000000000000003)};
#endif
  const unsigned rd[] = {5,6,7,0};
  for (int len = 4; len <= 8; len += 4) {
    word_t old = len == 4 ? 0x80000003 : UINT64_C(0x8000000000000003);
    for (size_t op = 0; op < ARRLEN(ops); op++) {
      for (unsigned order = 0; order < 4; order++) {
        for (size_t dest = 0; dest < ARRLEN(rd); dest++) {
          setup();
          cpu.gpr[7] = len == 4 ? UINT64_C(0x123456787ffffffe) :
                                 UINT64_C(0x7ffffffffffffffe);
          CHECK(paddr_write(DATA, len, old));
          uint32_t instruction = encoding(ops[op], len, rd[dest], order);
          StepResult r = execute(instruction);
#ifdef CONFIG_RV_A
          CHECK(r.retired && !r.trap.valid);
          CHECK_EQ(r.register_write, rd[dest] != 0);
          CHECK_EQ(cpu.gpr[rd[dest]], rd[dest] == 0 ? 0 :
                   ops[op] == 3 ? 1 : len == 4 ? UINT64_C(0xffffffff80000003) : old);
          CHECK_EQ(r.memory.write_performed, ops[op] != 2 && ops[op] != 3);
          CHECK_EQ(paddr_read(DATA, len), op < 9 ? (len == 4 ? w[op] : d[op]) : old);
          CHECK_EQ(sim.instret, 1);
#else
          CHECK(!r.retired && r.trap.valid);
          CHECK_EQ(cpu.mcause, 2);
          CHECK_EQ(cpu.mtval, instruction);
          CHECK(!r.memory.write_performed && !r.register_write);
          CHECK_EQ(cpu.gpr[5], 0xdeadbeef);
          CHECK_EQ(cpu.gpr[6], DATA);
          CHECK_EQ(paddr_read(DATA, len), old);
          CHECK_EQ(sim.instret, 0);
#endif
          CHECK_EQ(cpu.gpr[0], 0);
        }
      }
    }
  }
}

#ifdef CONFIG_RV_A
static void test_reservation(void) {
  for (int len = 4; len <= 8; len += 4) {
    setup();
    CHECK(paddr_write(DATA, len, 123));
    cpu.gpr[7] = 456;
    CHECK(execute(encoding(2,len,0,3)).retired); /* x0 LR still reserves */
    CHECK(cpu.reservation_valid);
    StepResult r = execute(encoding(3,len,7,3)); /* rd==rs2 */
    CHECK(r.retired && r.memory.write_performed);
    CHECK_EQ(cpu.gpr[7], 0);
    CHECK_EQ(paddr_read(DATA,len),456);
    CHECK(!cpu.reservation_valid);
    cpu.gpr[7] = 789;
    r = execute(encoding(3,len,5,0));
    CHECK(r.retired && !r.memory.write_performed);
    CHECK_EQ(cpu.gpr[5],1);
    CHECK_EQ(paddr_read(DATA,len),456);

    CHECK(execute(encoding(2,len,5,0)).retired);
    cpu.gpr[6] = DATA + 8;
    CHECK(execute(encoding(3,len,5,0)).retired);
    CHECK_EQ(cpu.gpr[5],1);
    cpu.gpr[6] = DATA;
    CHECK(execute(encoding(3,len,5,0)).retired);
    CHECK_EQ(cpu.gpr[5],1); /* failed SC consumed earlier LR */

    CHECK(execute(encoding(2,len,5,0)).retired);
    cpu.gpr[6] = DATA + 8;
    CHECK(execute(encoding(2,len,5,0)).retired);
    cpu.gpr[6] = DATA;
    CHECK(execute(encoding(3,len,5,0)).retired);
    CHECK_EQ(cpu.gpr[5],1); /* newest LR replaced old */

    CHECK(execute(encoding(2,len,5,0)).retired);
    setup();
    CHECK(!cpu.reservation_valid);
    CHECK(execute(encoding(3,len,5,0)).retired);
    CHECK_EQ(cpu.gpr[5],1);

    CHECK(execute(encoding(2,len,5,0)).retired);
    cpu.gpr[7] = 999;
    CHECK(execute(len == 4 ? 0x00732023 : 0x00733023).retired); /* same hart store */
    CHECK(execute(encoding(3,len,5,0)).retired);
    CHECK_EQ(cpu.gpr[5],0); /* profile retains own-store reservation */
  }
}

static int callbacks;
static word_t mmio_read(void *opaque, paddr_t off, int len) {
  (void)opaque; (void)off; (void)len; callbacks++; return 0;
}
static void mmio_write(void *opaque, paddr_t off, int len, word_t value) {
  (void)opaque; (void)off; (void)len; (void)value; callbacks++;
}

static void test_faults(void) {
  MmioRegion mmio = {.name="no-atomics",.size=64,.read=mmio_read,.write=mmio_write};
  CHECK(mmio_map(&mmio,0x10000000));
  const unsigned ops[] = {0,1,4,8,12,16,20,24,28,2,3};
  const vaddr_t addresses[] = {DATA+1,0x40000000,BASE+4096,0x10000000};
  for (int len=4;len<=8;len+=4) {
    for (size_t op=0;op<ARRLEN(ops);op++) {
      for (size_t address=0;address<ARRLEN(addresses);address++) {
        setup();
        cpu.gpr[6]=addresses[address];
        CHECK(paddr_write(DATA,8,0x12345678));
        StepResult r=execute(encoding(ops[op],len,5,0));
        CHECK(!r.retired && r.trap.valid && !r.register_write);
        CHECK_EQ(cpu.mcause,address==0 ? (ops[op]==2 ? 4 : 6) : (ops[op]==2 ? 5 : 7));
        CHECK_EQ(cpu.mtval,addresses[address]);
        CHECK_EQ(cpu.gpr[5],0xdeadbeef);
        CHECK_EQ(paddr_read(DATA,8),0x12345678);
        CHECK(!r.memory.write_performed);
      }
    }
  }
  CHECK_EQ(callbacks,0);
  /* 非法 LR rs2、未知 funct5、非法宽度。 */
  uint32_t invalid[]={encoding(2,4,5,0)|(7u<<20),encoding(5,8,5,0),
                     (encoding(0,4,5,0)&~0x7000u)|0x1000u};
  for(size_t i=0;i<ARRLEN(invalid);i++) {
    setup();
    StepResult r=execute(invalid[i]);
    CHECK(!r.retired && !r.register_write);
    CHECK_EQ(cpu.mcause,2);
    CHECK_EQ(cpu.gpr[5],0xdeadbeef);
  }
  bus_init(BASE,4096);
}

static void test_partial_ram_boundary(void) {
  mem_shutdown();
  CHECK(mem_init(BASE,1030));
  bus_init(BASE,1030);
  const unsigned ops[]={0,1,4,8,12,16,20,24,28,2,3};
  for(size_t i=0;i<ARRLEN(ops);i++) {
    setup();
    cpu.gpr[6]=BASE+1024;
    CHECK(paddr_write(BASE+1024,4,0x12345678));
    CHECK(paddr_write(BASE+1028,2,0x9abc));
    StepResult r=execute(encoding(ops[i],8,5,0));
    CHECK(!r.retired && !r.register_write && !r.memory.write_performed);
    CHECK_EQ(cpu.mcause,ops[i]==2 ? 5 : 7);
    CHECK_EQ(cpu.gpr[5],0xdeadbeef);
    CHECK_EQ(paddr_read(BASE+1024,4),0x12345678);
    CHECK_EQ(paddr_read(BASE+1028,2),0x9abc);
  }
}
#endif

int main(void) {
  CHECK(mem_init(BASE,4096));
  bus_init(BASE,4096);
  engine_init();
  test_operations();
#ifdef CONFIG_RV_A
  test_reservation();
  test_faults();
  test_partial_ram_boundary();
#endif
  mem_shutdown();
  TEST_MAIN_END();
}
