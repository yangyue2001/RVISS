#include <cpu/cpu.h>
#include <memory/paddr.h>
#include <device/device.h>
#include <util/state.h>
#include "../../src/engine/engine.h"
#include "csr.h"
#include "pmp.h"
#include "mmu.h"
#include "case.h"

static word_t input_time(void *opaque) { return *(uint64_t *)opaque; }

static void snapshot(DiffSnapshot *s) {
  s->pc = cpu.pc;
  s->privilege = cpu.priv;
  s->retired_total = sim.instret;
  for (unsigned i = 0; i < 32; i++) s->gpr[i] = cpu.gpr[i];
  /* 体系结构可见状态；只读 peek 不通过客户机权限，不改变运行中特权。 */
  for (unsigned i = 0; i < DIFF_CSR_COUNT; i++) {
    if (!csr_peek(diff_csrs[i], &s->csr[i])) case_error("snapshot CSR");
  }
}

int main(int argc, char **argv) {
  if (argc != 2 && argc != 3) return 2;
  const char *inject = argc == 3 ? argv[2] : "";
  bool injected = false;
  DiffCase c = load_case(argv[1]);
  if (!mem_init(c.base, c.size)) return 2;
  bus_init(c.base, c.size);
  uint64_t clock=c.time_value;
  device_set_time_reader(input_time,&clock);
  cpu_reset(c.pc);
  riscv_pmp_set_regions(c.pmp_regions);
  riscv_mmu_set_sv39(c.sv39);
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
  for(unsigned phase=0;phase<2;phase++) for (unsigned i = 0; i < 4096; i++) {
    if (case_pmp_cfg(i)!=(phase==1)) continue;
    if (c.csr_set[i] && !csr_write(i, c.csr[i])) case_error("DUT initial CSR");
  }
  cpu.priv = c.priv;
  DiffSnapshot initial = {.outcome = "initial", .pc_before = cpu.pc,
                           .privilege_before = cpu.priv};
  snapshot(&initial);
  emit_snapshot(&initial, mem_host_ptr(c.base), c.size);
  for (uint64_t i = 0; i < c.steps; i++) {
    for (unsigned j=0;j<c.clock_count;j++)
      if(c.clock[j].event==i+1) clock=c.clock[j].value;
    for (unsigned j=0;j<c.irq_count;j++) if(c.irq[j].event==i+1) {
      IrqLine *line=cpu_irq_input(c.irq[j].bit);
      if(c.irq[j].level) irq_raise(line); else irq_lower(line);
    }
    StepResult r;
    if (!engine_step(&r)) return 3;
    if (r.outcome != STEP_RETIRED && r.outcome != STEP_SYNCHRONOUS_TRAP &&
        r.outcome != STEP_INTERRUPT) return 3;
    DiffSnapshot s = {.seq = i + 1, .pc_before = r.pc_before,
        .privilege_before = r.privilege_before, .raw = r.raw_instruction,
        .length = r.instruction_length, .raw_bytes = r.raw_bytes,
        .valid = r.instruction_valid, .retired = r.retired,
        .outcome = r.retired ? "retired" : r.outcome == STEP_INTERRUPT ?
                    "interrupt" : "synchronous_trap"};
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
