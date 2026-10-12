#include <cpu/cpu.h>
#include <memory/paddr.h>
#include <device/device.h>
#include <util/state.h>
#include "../../src/engine/engine.h"
#include "csr.h"
#include "case.h"

static void snapshot(DiffSnapshot *s) {
  s->pc = cpu.pc;
  s->privilege = cpu.priv;
  s->retired_total = sim.instret;
  for (unsigned i = 0; i < 32; i++) s->gpr[i] = cpu.gpr[i];
  /* 原始状态读取，不通过客户机权限，不改变运行中特权。 */
  for (unsigned i = 0; i < DIFF_CSR_COUNT; i++) {
    switch (diff_csrs[i]) {
#define RAW_CSR(n,a,m) case a: s->csr[i] = cpu.n; break;
      RVISS_CSRS_OWNED(RAW_CSR)
#undef RAW_CSR
      default: case_error("snapshot CSR");
    }
  }
}

int main(int argc, char **argv) {
  if (argc != 2 && argc != 3) return 2;
  const char *inject = argc == 3 ? argv[2] : "";
  bool injected = false;
  DiffCase c = load_case(argv[1]);
  if (!mem_init(c.base, c.size)) return 2;
  bus_init(c.base, c.size);
  cpu_reset(c.pc);
  const char *isa_end = strchr(c.isa, '_');
  if (!isa_end) isa_end = c.isa + strlen(c.isa);
  bool requested_c = memchr(c.isa, 'c', isa_end - c.isa) != NULL;
#ifndef CONFIG_RV_C
  if (requested_c) case_error("DUT C disabled in build");
#endif
  if (!requested_c) cpu.misa &= ~MISA_C; /* 仅共同输入初始化，不在执行中同步 */
  engine_init();
  sim_reset();
  memcpy(mem_host_ptr(c.base), c.ram, c.size);
  memcpy(cpu.gpr, c.gpr, sizeof(c.gpr));
  for (unsigned i = 0; i < 4096; i++) {
    if (c.csr_set[i] && !csr_write(i, c.csr[i])) case_error("DUT initial CSR");
  }
  cpu.priv = c.priv;
  DiffSnapshot initial = {.outcome = "initial", .pc_before = cpu.pc,
                           .privilege_before = cpu.priv};
  snapshot(&initial);
  emit_snapshot(&initial, mem_host_ptr(c.base), c.size);
  for (uint64_t i = 0; i < c.steps; i++) {
    StepResult r;
    if (!engine_step(&r)) return 3;
    if (r.outcome != STEP_RETIRED && r.outcome != STEP_SYNCHRONOUS_TRAP) return 3;
    DiffSnapshot s = {.seq = i + 1, .pc_before = r.pc_before,
        .privilege_before = r.privilege_before, .raw = r.raw_instruction,
        .length = r.instruction_length, .raw_bytes = r.raw_bytes,
        .valid = r.instruction_valid, .retired = r.retired,
        .outcome = r.retired ? "retired" : "synchronous_trap"};
    if (r.memory.write_performed) {
      s.store = true; s.store_addr = r.memory.paddr; s.store_length = r.memory.length;
      s.store_data = r.memory.data;
    }
    /* 测试工具的故意差异注入；不改模拟器核心或参考。 */
    if (!injected && !strcmp(inject, "gpr")) {
      cpu.gpr[5] ^= 1; injected = true;
    } else if (!injected && !strcmp(inject, "store") && s.store) {
      *mem_host_ptr(s.store_addr) ^= 1; injected = true;
    } else if (!injected && !strcmp(inject, "trap") && r.trap.valid) {
      cpu.mtval ^= 1; injected = true;
    }
    snapshot(&s);
    emit_snapshot(&s, mem_host_ptr(c.base), c.size);
  }
  mem_shutdown();
  free(c.ram);
  return 0;
}
