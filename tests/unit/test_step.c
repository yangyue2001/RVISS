#include <common.h>
#include <cpu/cpu.h>
#include <cpu/interrupt.h>
#include <device/device.h>
#include <device/timer/clint.h>
#include <memory/paddr.h>
#include <rviss.h>
#include <util/state.h>

#include "../../src/engine/engine.h"
#include "test.h"
#include "pmp.h"
#include "mmu.h"

#define BASE UINT64_C(0x80000000)
#define HANDLER (BASE + 64)

static void setup(uint32_t instruction) {
  cpu_reset(BASE);
  riscv_pmp_set_regions(0); riscv_mmu_set_sv39(false); /* 本 fixture 固定 Bare、0 PMP entries */
  cpu.misa &= ~MISA_C; /* T003 的固定 I profile；C 行为另由 test_c 验证 */
  cpu.mtvec = HANDLER;
  cpu.gpr[5] = 0xdeadbeef;
  cpu.gpr[6] = 0x40000000;
  sim = (SimState){.state = SIM_RUNNING, .max_instr = 1};
  CHECK(paddr_write(BASE, 4, instruction));
}

static StepResult step(void) {
  StepResult r;
  CHECK(engine_step(&r));
  return r;
}

static void check_trap(StepResult r, word_t cause, vaddr_t epc, vaddr_t tval) {
  CHECK_EQ(r.outcome, STEP_SYNCHRONOUS_TRAP);
  CHECK(r.instruction_attempted && !r.retired && r.trap.valid);
  CHECK(!r.register_write);
  CHECK_EQ(r.trap.cause, cause);
  CHECK_EQ(r.trap.epc, epc);
  CHECK_EQ(r.trap.tval, tval);
  CHECK_EQ(cpu.mcause, cause);
  /* 原始事件 epc 保留故障 PC；CSR 存储遵守实现支持的对齐。 */
#ifdef CONFIG_RV_C
  CHECK_EQ(cpu.mepc, epc & ~1ull);
#else
  CHECK_EQ(cpu.mepc, epc & ~3ull);
#endif
  CHECK_EQ(cpu.mtval, tval);
  CHECK_EQ(r.pc_after, HANDLER);
  CHECK_EQ(r.retired_total, 0);
}

static void test_result_and_memory(void) {
  setup(0x00128293); /* addi x5,x5,1 */
  StepResult r = step();
  CHECK_EQ(r.outcome, STEP_RETIRED);
  CHECK(r.retired && r.instruction_attempted && r.instruction_valid);
  CHECK_EQ(r.raw_instruction, 0x00128293);
  CHECK_EQ(r.raw_bytes, 4);
  CHECK_EQ(r.instruction_length, 4);
  CHECK_EQ(r.pc_before, BASE);
  CHECK_EQ(r.pc_after, BASE + 4);
  CHECK_EQ(r.privilege_before, PRV_M);
  CHECK_EQ(r.privilege_after, PRV_M);
  CHECK(r.register_write);
  CHECK_EQ(r.rd, 5);
  CHECK_EQ(r.register_value, 0xdeadbef0);
  CHECK_EQ(r.event_seq, 1);
  CHECK_EQ(r.retired_total, 1);
  CHECK_EQ(sim.attempts, 1);
  CHECK_EQ(sim.logical_ticks, 1);
  CHECK_EQ(cpu.cycle, 1);

  /* 预期值是固定字节序列的已知符号/零扩展，不复用实现公式。 */
  const uint32_t loads[] = {0x00030283, 0x00031283, 0x00032283, 0x00033283,
                            0x00034283, 0x00035283, 0x00036283};
  const word_t values[] = {UINT64_C(0xffffffffffffff80), UINT64_C(0xffffffffffff8080),
                           UINT64_C(0xffffffff80008080), UINT64_C(0x8877665580008080),
                           0x80, 0x8080, 0x80008080};
  const int lengths[] = {1, 2, 4, 8, 1, 2, 4};
  for (size_t i = 0; i < ARRLEN(loads); i++) {
    setup(loads[i]);
    cpu.gpr[6] = BASE + 96;
    CHECK(paddr_write(BASE + 96, 8, UINT64_C(0x8877665580008080)));
    r = step();
    CHECK(r.retired && r.memory.valid && !r.memory.mmio);
    CHECK_EQ(cpu.gpr[5], values[i]);
    CHECK_EQ(r.memory.type, MEM_TYPE_READ);
    CHECK_EQ(r.memory.length, lengths[i]);
    CHECK_EQ(r.memory.paddr, BASE + 96);
    CHECK_EQ(r.memory.status, MEM_OK);
  }

  const uint32_t stores[] = {0x00530023, 0x00531023, 0x00532023, 0x00533023};
  const int sizes[] = {1, 2, 4, 8};
  const word_t stored[] = {0xef, 0xcdef, 0x89abcdef, UINT64_C(0x0123456789abcdef)};
  for (size_t i = 0; i < ARRLEN(stores); i++) {
    setup(stores[i]);
    cpu.gpr[6] = BASE + 96;
    cpu.gpr[5] = UINT64_C(0x0123456789abcdef);
    CHECK(paddr_write(BASE + 96, 8, 0));
    r = step();
    CHECK(r.retired && !r.register_write && r.memory.valid);
    CHECK_EQ(r.memory.type, MEM_TYPE_WRITE);
    CHECK_EQ(r.memory.length, sizes[i]);
    CHECK_EQ(r.memory.data, stored[i]);
    CHECK_EQ(paddr_read(BASE + 96, 8), stored[i]);
  }

  setup(0x00033303); /* ld x6,0(x6), rd == rs1 */
  cpu.gpr[6] = BASE + 96;
  CHECK(paddr_write(BASE + 96, 8, 0x1234));
  r = step();
  CHECK_EQ(cpu.gpr[6], 0x1234);
  CHECK_EQ(r.memory.vaddr, BASE + 96);
  setup(0x00100013); /* addi x0,x0,1 */
  r = step();
  CHECK(r.retired && !r.register_write);
  CHECK_EQ(cpu.gpr[0], 0);
}

static void test_sync_faults(void) {
  setup(0xffffffff);
  StepResult r = step();
  check_trap(r, 2, BASE, 0xffffffff);
  CHECK(r.instruction_valid);
  for (uint8_t priv = PRV_U; priv <= PRV_M; priv++) {
    if (priv == 2) continue;
    setup(0x00000073);
    cpu.priv = priv;
    r = step();
    check_trap(r, 8 + priv, BASE, 0);
    CHECK_EQ(r.privilege_before, priv);
    CHECK_EQ(r.privilege_after, PRV_M);
  }
  setup(0x00100073); /* ebreak */
  check_trap(step(), 3, BASE, BASE);
  const uint32_t bad_csrs[] = {0xfff312f3, /* missing CSR, csrrw */
                               0xf14312f3, /* readonly mhartid, csrrw */
                               0x300322f3}; /* S access mstatus */
  for (size_t i = 0; i < ARRLEN(bad_csrs); i++) {
    setup(bad_csrs[i]);
    if (i == 2) cpu.priv = PRV_S;
    check_trap(step(), 2, BASE, bad_csrs[i]);
    CHECK_EQ(cpu.gpr[5], 0xdeadbeef);
  }
  setup(0x00533023); /* unmapped sd */
  uint8_t before[128];
  memcpy(before, mem_host_ptr(BASE), sizeof(before));
  r = step();
  check_trap(r, 7, BASE, 0x40000000);
  CHECK_EQ(r.memory.status, MEM_FAULT);
  CHECK(memcmp(before, mem_host_ptr(BASE), sizeof(before)) == 0);

  setup(0x00033283);
  cpu.gpr[6] = BASE + 97;
  check_trap(step(), 4, BASE, BASE + 97);
  CHECK_EQ(cpu.gpr[5], 0xdeadbeef);
  setup(0x00533023);
  cpu.gpr[6] = BASE + 97;
  memcpy(before, mem_host_ptr(BASE), sizeof(before));
  check_trap(step(), 6, BASE, BASE + 97);
  CHECK(memcmp(before, mem_host_ptr(BASE), sizeof(before)) == 0);
  /* 对齐检查在 unmapped/越界检查之前。 */
  setup(0x00033283);
  cpu.gpr[6] = 0x40000001;
  check_trap(step(), 4, BASE, 0x40000001);
  setup(0x00033283);
  cpu.gpr[6] = UINT64_C(0x0100000000000000); /* PA 超出 56 bits */
  check_trap(step(), 5, BASE, UINT64_C(0x0100000000000000));
  CHECK_EQ(cpu.gpr[5], 0xdeadbeef);

  word_t data = 0x9876;
  bool mmio = false;
  CHECK(!paddr_try_read(UINT64_MAX - 1, 4, &data, &mmio));
  CHECK_EQ(data, 0x9876);
  CHECK(!paddr_try_write(BASE + 126, 4, 0x1234, &mmio));
  CHECK(!mem_in_ram(UINT64_MAX - 1, 4));
  CHECK(!paddr_try_read(BASE, 0, &data, &mmio));
  CHECK(!paddr_try_read(BASE, 3, &data, &mmio));
}

static void test_control_flow(void) {
  setup(0x002002ef); /* jal x5,+2 */
  check_trap(step(), 0, BASE, BASE + 2);
  CHECK_EQ(cpu.gpr[5], 0xdeadbeef);
  setup(0x000282e7); /* jalr x5,0(x5) */
  cpu.gpr[5] = BASE + 3; /* bit 0 清零后仍不符合 IALIGN=32 */
  check_trap(step(), 0, BASE, BASE + 2);
  CHECK_EQ(cpu.gpr[5], BASE + 3);
  setup(0x000282e7);
  cpu.gpr[5] = BASE + 9;
  StepResult r = step();
  CHECK(r.retired);
  CHECK_EQ(r.pc_after, BASE + 8);
  CHECK_EQ(cpu.gpr[5], BASE + 4);
  setup(0x00000163); /* beq x0,x0,+2 */
  check_trap(step(), 0, BASE, BASE + 2);
  setup(0x00001163); /* bne x0,x0,+2, not taken */
  r = step();
  CHECK(r.retired);
  CHECK_EQ(cpu.pc, BASE + 4);
  setup(0x00000013);
  cpu.pc = BASE + 2;
  r = step();
  check_trap(r, 0, BASE + 2, BASE + 2);
  CHECK_EQ(r.raw_bytes, 0);
  CHECK(!r.instruction_valid);
}

static int fetch_reads, fetch_length;
static word_t fetch_read(void *opaque, paddr_t off, int len) {
  (void)opaque;
  CHECK_EQ(off, 0);
  fetch_reads++;
  fetch_length = len;
  return 0x0001; /* 未实现的 16-bit 编码 */
}

static void test_fetch_boundary(void) {
  mem_shutdown();
  CHECK(mem_init(BASE, 126));
  bus_init(BASE, 126);
  setup(0x00000013);
  cpu.pc = BASE + 124;
  CHECK(paddr_write(cpu.pc, 2, 0x0013));
  StepResult r = step();
  check_trap(r, 1, BASE + 124, BASE + 126);
  CHECK_EQ(r.raw_instruction, 0x0013);
  CHECK_EQ(r.raw_bytes, 2);
  CHECK_EQ(r.instruction_length, 4);
  CHECK(!r.instruction_valid);
  CHECK_EQ(r.memory.type, MEM_TYPE_IFETCH);

  setup(0x00000013);
  cpu.pc = BASE + 124;
  CHECK(paddr_write(cpu.pc, 2, 0x0001));
  r = step();
  check_trap(r, 2, BASE + 124, 1);
  CHECK(r.instruction_valid);
  CHECK_EQ(r.instruction_length, 2);
  CHECK_EQ(r.raw_bytes, 2);

  setup(0x00533023); /* 对齐 sd 跨过 RAM 末尾：不能部分写入 */
  cpu.gpr[6] = BASE + 120;
  CHECK(paddr_write(BASE + 120, 4, 0x12345678));
  CHECK(paddr_write(BASE + 124, 2, 0x9abc));
  check_trap(step(), 7, BASE, BASE + 120);
  CHECK_EQ(paddr_read(BASE + 120, 4), 0x12345678);
  CHECK_EQ(paddr_read(BASE + 124, 2), 0x9abc);
  setup(0x00033283);
  cpu.gpr[6] = BASE + 120;
  check_trap(step(), 5, BASE, BASE + 120);
  CHECK_EQ(cpu.gpr[5], 0xdeadbeef);

  setup(0x00000013);
  MmioRegion fetch = {.name = "fetch-observer", .size = 2, .read = fetch_read};
  CHECK(mmio_map(&fetch, 0x20000000));
  cpu.pc = 0x20000000;
  fetch_reads = fetch_length = 0;
  r = step();
  check_trap(r, 2, 0x20000000, 1);
  CHECK_EQ(fetch_reads, 1);
  CHECK_EQ(fetch_length, 2);
  mem_shutdown();
  CHECK(mem_init(BASE, 128));
  bus_init(BASE, 128); /* 不保留指向栈内 MmioRegion 的映射 */
}

static void test_budgets_and_callers(void) {
  setup(0xffffffff);
  CHECK(paddr_write(HANDLER, 4, 0xffffffff));
  sim.max_instr = 19;
  CHECK_EQ(engine_exec(100), 19);
  CHECK_EQ(sim.instret, 0);
  CHECK_EQ(sim.attempts, 19);
  CHECK_EQ(sim.event_seq, 19);
  CHECK_EQ(sim.logical_ticks, 19);
  CHECK_EQ(sim.state, SIM_STOPPED);
  StepResult recent[16];
  CHECK_EQ(engine_recent(recent, 16), 16);
  CHECK_EQ(recent[0].event_seq, 4);
  CHECK_EQ(recent[15].event_seq, 19);
  recent[15].pc_after = 0; /* 修改副本不能影响记录/CPU */
  CHECK_EQ(engine_recent(recent, 1), 1);
  CHECK_EQ(recent[0].pc_after, HANDLER);

  setup(0x0005006b); /* successful halt is a retired instruction */
  CHECK_EQ(engine_exec(100), 1);
  CHECK_EQ(sim.state, SIM_HALTED);
  CHECK_EQ(sim.instret, 1);
  CHECK_EQ(sim.event_seq, 1);
  StepResult r;
  CHECK(!engine_step(&r));
  CHECK_EQ(r.outcome, STEP_STOPPED);
  CHECK_EQ(engine_exec(1), 0);
  CHECK_EQ(sim.event_seq, 1);

  setup(0x00000013);
  CHECK_EQ(engine_exec(0), 0);
  CHECK_EQ(sim.event_seq, 0);
  sim.state = SIM_STOPPED;
  CHECK_EQ(engine_exec(1), 0);
  CHECK_EQ(cpu.pc, BASE);
  CHECK_EQ(sim.event_seq, 0);

  setup(0xffffffff);
  CHECK(paddr_write(HANDLER, 4, 0xffffffff));
  sim.max_instr = 0;
  difftest_exec(2);
  CHECK_EQ(sim.attempts, 2);
  CHECK_EQ(sim.instret, 0);
  CHECK_EQ(sim.event_seq, 2);
  CHECK_EQ(sim.state, SIM_RUNNING);
  difftest_raise_intr(IRQ_BIT | 7);
  CHECK_EQ(sim.event_seq, 3);
  CHECK_EQ(sim.attempts, 2);
  CHECK_EQ(sim.instret, 0);
  CHECK_EQ(engine_recent(recent, 1), 1);
  CHECK_EQ(recent[0].outcome, STEP_INTERRUPT);
  CHECK(!recent[0].instruction_attempted);
  CHECK_EQ(recent[0].trap.epc, HANDLER);
}

static void test_interrupt_and_time(void) {
  ClintState clint;
  bus_init(BASE, 128);
  clint_init(&clint);
  clint.mtimer_irq = cpu_irq_input(7);
  clint.msip_irq = cpu_irq_input(3);
  CHECK(mmio_map(&clint.mmio, 0x02000000));
  setup(0x0000006f); /* jal x0,0 */
  sim.max_instr = 0;
  cpu.mie = 0x80;
  cpu.mstatus |= MSTATUS_MIE;
  clint.mtimecmp = 1;
  CHECK_EQ(engine_exec(128), 128);
  CHECK_EQ(clint.mtime, 1);
  CHECK_EQ(sim.event_seq, 128);
  CHECK_EQ(cpu.pc, BASE); /* 第 128 条退休后只是拉高中断线 */
  CHECK(cpu.mip & 0x80);
  cpu.mtvec = HANDLER | 1; /* vectored interrupt */
  StepResult r = step();
  CHECK_EQ(r.outcome, STEP_INTERRUPT);
  CHECK(!r.instruction_attempted && !r.retired && !r.instruction_valid);
  CHECK_EQ(r.trap.cause, IRQ_BIT | 7);
  CHECK_EQ(r.trap.epc, BASE);
  CHECK_EQ(r.trap.tval, 0);
  CHECK_EQ(r.pc_after, HANDLER + 28);
  CHECK_EQ(r.retired_total, 128);
  CHECK_EQ(sim.attempts, 128);
  CHECK_EQ(sim.event_seq, 129);
  CHECK_EQ(sim.logical_ticks, 129);
  CHECK_EQ(cpu.cycle, 129);
  CHECK(paddr_write(HANDLER + 28, 4, 0x00000013));
  CHECK_EQ(engine_exec(1), 1);
  CHECK_EQ(sim.event_seq, 130);
  CHECK_EQ(sim.instret, 129);
  /* 软件写入 guest counters 不影响平台时间或累计退休。 */
  cpu.instret = 10000;
  cpu.cycle = 50000;
  sim.max_instr = 0;
  CHECK(paddr_write(cpu.pc, 4, 0x0000006f));
  CHECK_EQ(engine_exec(126), 126);
  CHECK_EQ(clint.mtime, 2);
  CHECK_EQ(sim.logical_ticks, 256);
  CHECK_EQ(sim.instret, 255);
  CHECK_EQ(cpu.instret, 10126);

  clint_reset(&clint);
  setup(0xffffffff); /* 异常循环也推进 mtime，不退休 */
  cpu.mtvec = BASE;
  sim.max_instr = 128;
  CHECK_EQ(engine_exec(200), 128);
  CHECK_EQ(sim.instret, 0);
  CHECK_EQ(clint.mtime, 1);

  clint_reset(&clint);
  setup(0x00000013);
  sim.max_events = 1;
  cpu.mie = 0x80;
  cpu.mstatus |= MSTATUS_MIE;
  irq_raise(clint.mtimer_irq);
  CHECK_EQ(engine_exec(1), 0); /* 事件预算被纯中断消耗 */
  CHECK_EQ(sim.state, SIM_STOPPED);
  CHECK_EQ(sim.event_seq, 1);
  CHECK_EQ(sim.attempts, 0);
  CHECK_EQ(sim.instret, 0);
  CHECK_EQ(cpu.mepc, BASE);

  clint_reset(&clint);
  setup(0x00000013);
  CHECK(paddr_write(HANDLER, 4, 0x00000013));
  cpu.mie = 0x80;
  cpu.mstatus |= MSTATUS_MIE;
  irq_raise(clint.mtimer_irq);
  CHECK_EQ(engine_exec(1), 1); /* 尝试预算允许先中断再执行 handler */
  CHECK_EQ(sim.event_seq, 2);
  CHECK_EQ(sim.instret, 1);
  CHECK_EQ(cpu.pc, HANDLER + 4);
  clint_reset(&clint);
  bus_init(BASE, 128); /* 清除指向局部 clint 的 tick handler */
}

typedef struct { uint8_t bytes[16]; size_t count; } Output;

static void capture_output(void *opaque, paddr_t off, int len, word_t data) {
  Output *out = opaque;
  CHECK_EQ(off, 0);
  CHECK_EQ(len, 1);
  CHECK(out->count < sizeof(out->bytes));
  out->bytes[out->count++] = (uint8_t)data;
}

static void test_access_mode_and_observer(void) {
  setup(0x00033283);
  cpu.gpr[6] = BASE + 96;
  CHECK(paddr_write(BASE + 96, 8, 0x1234));
  cpu.satp = UINT64_C(0x8000000000000000);
  StepResult r = step(); /* M 的取指/数据不受 satp 影响 */
  CHECK(r.retired);
  CHECK_EQ(cpu.gpr[5], 0x1234);
  setup(0x00033283);
  cpu.gpr[6] = BASE + 96;
  cpu.satp = UINT64_C(0x8000000000000000);
  cpu.mstatus = MSTATUS_MPRV | ((word_t)PRV_S << 11);
  r = step();
  CHECK_EQ(r.outcome, STEP_HOST_ERROR);
  CHECK_EQ(sim.state, SIM_ABORTED);
  CHECK_EQ(cpu.gpr[5], 0xdeadbeef);
  CHECK_EQ(cpu.pc, BASE);
  CHECK_EQ(sim.instret, 0);
  CHECK(!r.trap.valid);
  CHECK_EQ(r.memory.status, MEM_HOST_ERROR);

  CPU_state expected_cpu;
  SimState expected_sim;
  uint8_t expected_ram[128];
  Output outputs[2] = {0};
  for (int observer = 0; observer < 2; observer++) {
    setup(0x00533023); /* RAM store/load -> MMIO output -> ecall -> addi -> halt */
    sim.max_instr = 0;
    cpu.gpr[6] = BASE + 96;
    cpu.gpr[7] = 0x10000000;
    CHECK(paddr_write(BASE + 4, 4, 0x00033283));
    CHECK(paddr_write(BASE + 8, 4, 0x00538023)); /* sb x5,0(x7) */
    CHECK(paddr_write(BASE + 12, 4, 0x00000073));
    CHECK(paddr_write(HANDLER, 4, 0x00128293));
    CHECK(paddr_write(HANDLER + 4, 4, 0x0005006b));
    bus_init(BASE, 128);
    MmioRegion output = {.name = "output", .size = 1, .write = capture_output,
                          .opaque = &outputs[observer]};
    CHECK(mmio_map(&output, 0x10000000));
    engine_set_history(observer != 0);
    CHECK_EQ(engine_exec(10), 6);
    CHECK_EQ(sim.instret, 5);
    CHECK_EQ(sim.event_seq, 6);
    CHECK_EQ(outputs[observer].count, 1);
    CHECK_EQ(outputs[observer].bytes[0], 0xef);
    if (observer == 0) {
      expected_cpu = cpu;
      expected_sim = sim;
      memcpy(expected_ram, mem_host_ptr(BASE), sizeof(expected_ram));
    } else {
      CHECK(memcmp(&expected_cpu, &cpu, sizeof(cpu)) == 0);
      CHECK(memcmp(&expected_sim, &sim, sizeof(sim)) == 0);
      CHECK(memcmp(expected_ram, mem_host_ptr(BASE), sizeof(expected_ram)) == 0);
      CHECK(memcmp(&outputs[0], &outputs[1], sizeof(Output)) == 0);
    }
    StepResult events[16];
    CHECK_EQ(engine_recent(events, 16), observer ? 6 : 0);
    if (observer) {
      CHECK(events[2].memory.mmio);
      CHECK_EQ(events[2].memory.data, 0xef);
    }
    bus_init(BASE, 128);
  }
}

int main(void) {
  CHECK(mem_init(BASE, 128));
  bus_init(BASE, 128);
  engine_init();

  setup(0xffffffff); /* illegal */
  CHECK_EQ(engine_exec(1), 1);
  CHECK_EQ(cpu.instret, 0);
  CHECK_EQ(sim.instret, 0);
  CHECK_EQ(cpu.mcause, 2);

  setup(0x00000073); /* ecall from M */
  CHECK_EQ(engine_exec(1), 1);
  CHECK_EQ(cpu.instret, 0);
  CHECK_EQ(cpu.mcause, 11);

  setup(0x00033283); /* ld x5,0(x6) */
  CHECK_EQ(engine_exec(1), 1);
  CHECK_EQ(cpu.gpr[5], 0xdeadbeef);
  CHECK_EQ(cpu.mcause, 5);
  CHECK_EQ(cpu.mtval, 0x40000000);
  CHECK_EQ(cpu.instret, 0);

  setup(0x00033003); /* ld x0,0(x6) still faults */
  CHECK_EQ(engine_exec(1), 1);
  CHECK_EQ(cpu.gpr[0], 0);
  CHECK_EQ(cpu.mcause, 5);

  setup(0x00000013);
  cpu.pc = 0x40000000;
  CHECK_EQ(engine_exec(1), 1);
  CHECK_EQ(cpu.mcause, 1);
  CHECK_EQ(cpu.mtval, 0x40000000);
  CHECK_EQ(cpu.instret, 0);

  test_result_and_memory();
  test_sync_faults();
  test_control_flow();
  test_fetch_boundary();
  test_budgets_and_callers();
  test_interrupt_and_time();
  test_access_mode_and_observer();

  mem_shutdown();
  TEST_MAIN_END();
}
