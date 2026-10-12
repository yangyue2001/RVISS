#include <common.h>
#include <cpu/cpu.h>
#include <device/device.h>
#include <memory/paddr.h>
#include <util/state.h>
#include "../../src/engine/engine.h"
#include "test.h"

#define BASE UINT64_C(0x80000000)
#define NEG1 UINT64_MAX
#define MIN64 UINT64_C(0x8000000000000000)
#define MIN32 UINT64_C(0xffffffff80000000)

/* 独立常量期望；不在测试中重算实现的乘除公式。funct3 + opcode 位。 */
static const struct { uint32_t op; word_t a, b, expected; } vectors[] = {
  {0x0033, NEG1, 2, UINT64_C(0xfffffffffffffffe)},
  {0x1033, NEG1 - 1, 3, NEG1},
  {0x1033, MIN64, MIN64, UINT64_C(0x4000000000000000)},
  {0x2033, NEG1, NEG1, NEG1},
  {0x2033, MIN64, NEG1, MIN64},
  {0x3033, NEG1, NEG1, NEG1 - 1},
  {0x3033, MIN64, 2, 1},
  {0x4033, (word_t)-7, 3, (word_t)-2},
  {0x4033, 7, (word_t)-3, (word_t)-2},
  {0x4033, MIN64, NEG1, MIN64},
  {0x4033, MIN64, 0, NEG1},
  {0x5033, NEG1, 2, UINT64_C(0x7fffffffffffffff)},
  {0x5033, 123, 0, NEG1},
  {0x6033, (word_t)-7, 3, NEG1},
  {0x6033, 7, (word_t)-3, 1},
  {0x6033, MIN64, NEG1, 0},
  {0x6033, MIN64, 0, MIN64},
  {0x7033, NEG1, 2, 1},
  {0x7033, NEG1, 0, NEG1},
  {0x003b, UINT64_C(0xaaaaaaaa7fffffff), 2, NEG1 - 1},
  {0x403b, UINT64_C(0x1234567880000000), NEG1, MIN32},
  {0x403b, UINT64_C(0x1234567880000000), UINT64_C(0x100000000), NEG1},
  {0x403b, (word_t)-7, 3, (word_t)-2},
  {0x503b, UINT64_C(0x12345678ffffffff), 1, NEG1},
  {0x503b, UINT64_C(0x1234567880000000), 1, MIN32},
  {0x503b, 123, UINT64_C(0x100000000), NEG1},
  {0x603b, UINT64_C(0x1234567880000000), NEG1, 0},
  {0x603b, (word_t)-7, 3, NEG1},
  {0x603b, UINT64_C(0x1234567880000000), 0, MIN32},
  {0x703b, UINT64_C(0x12345678ffffffff), UINT64_C(0x100000000), NEG1},
  {0x703b, UINT64_C(0x12345678ffffffff), 2, 1},
  {0x703b, UINT64_C(0x1234567880000000), 0, MIN32},
};

int main(void) {
  CHECK(mem_init(BASE, 4096));
  bus_init(BASE, 4096);
  engine_init();
  const uint8_t destinations[] = {5, 6, 7, 0};
  for (size_t v = 0; v < ARRLEN(vectors); v++) {
    for (size_t d = 0; d < ARRLEN(destinations); d++) {
      uint8_t rd = destinations[d];
      uint32_t instruction = 0x02000000 | (7u << 20) | (6u << 15) |
                             (rd << 7) | vectors[v].op;
      cpu_reset(BASE);
      cpu.mtvec = BASE + 128;
      cpu.gpr[5] = 0xdeadbeef;
      cpu.gpr[6] = vectors[v].a;
      cpu.gpr[7] = vectors[v].b;
      sim_reset();
      CHECK(paddr_write(BASE, 4, instruction));
      StepResult r;
      CHECK(engine_step(&r));
      CHECK_EQ(r.raw_instruction, instruction);
      CHECK_EQ(r.instruction_length, 4);
#ifdef CONFIG_RV_M
      CHECK(r.retired && r.outcome == STEP_RETIRED && !r.trap.valid);
      CHECK_EQ(cpu.pc, BASE + 4);
      CHECK_EQ(cpu.gpr[rd], rd ? vectors[v].expected : 0);
      CHECK_EQ(r.register_write, rd != 0);
      CHECK_EQ(sim.instret, 1);
      if (rd != 6) CHECK_EQ(cpu.gpr[6], vectors[v].a);
      if (rd != 7) CHECK_EQ(cpu.gpr[7], vectors[v].b);
#else
      CHECK(!r.retired && r.outcome == STEP_SYNCHRONOUS_TRAP);
      CHECK_EQ(cpu.mcause, 2);
      CHECK_EQ(cpu.mtval, instruction);
      CHECK_EQ(cpu.gpr[5], 0xdeadbeef);
      CHECK_EQ(cpu.gpr[6], vectors[v].a);
      CHECK_EQ(cpu.gpr[7], vectors[v].b);
      CHECK_EQ(sim.instret, 0);
      CHECK(!r.register_write);
#endif
      CHECK_EQ(cpu.gpr[0], 0);
    }
  }
  mem_shutdown();
  TEST_MAIN_END();
}
