#include <cpu/cpu.h>
#include <memory/paddr.h>
#include <device/device.h>
#include <util/state.h>
#include "../../src/engine/engine.h"
#include "csr.h"
#include "pmp.h"
#include "mmu.h"
#include "case.h"

static bool input_double;
static void snapshot(DiffSnapshot *s) {
  s->pc = cpu.pc;
  s->privilege = cpu.priv;
  s->retired_total = sim.instret;
  for (unsigned i = 0; i < 32; i++) s->gpr[i] = cpu.gpr[i];
  for (unsigned i = 0; i < 32; i++) s->fpr[i] = cpu.fpr[i] & (input_double?UINT64_MAX:UINT32_MAX);
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
  if (!mem_install_ram(c.base, c.size)) return 2;
  cpu_reset(c.pc);
  riscv_pmp_set_regions(c.pmp_regions);
  riscv_mmu_set_sv39(c.sv39);
  input_double=case_extension(&c,'d');
  bool input_float=case_extension(&c,'f') || input_double;
#ifndef CONFIG_RV_F
  if(input_float) case_error("DUT F disabled in build");
#endif
#ifndef CONFIG_RV_D
  if(input_double) case_error("DUT D disabled in build");
#endif
  cpu.misa=(cpu.misa&~(MISA_F|MISA_D))|(input_float?MISA_F:0)|(input_double?MISA_D:0);
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
  memcpy(cpu.fpr, c.fpr, sizeof(c.fpr));
  for(unsigned phase=0;phase<2;phase++) for (unsigned i = 0; i < 4096; i++) {
    if (case_pmp_cfg(i)!=(phase==1)) continue;
    if(!c.csr_set[i]) continue;
    if(i>=CSR_FFLAGS && i<=CSR_FCSR) {
      if(!input_float) case_error("initial FP CSR without F");
      // Dedicated initial state, independent of guest FS access permissions.
      if(i==CSR_FFLAGS) cpu.fcsr=(cpu.fcsr&0xe0)|(c.csr[i]&31);
      if(i==CSR_FRM) cpu.fcsr=(cpu.fcsr&31)|((c.csr[i]&7)<<5);
      if(i==CSR_FCSR) cpu.fcsr=c.csr[i]&0xff;
    } else if (!csr_write(i, c.csr[i])) case_error("DUT initial CSR");
  }
  cpu.priv = c.priv;
  DiffSnapshot initial = {.outcome = "initial", .pc_before = cpu.pc,
                           .privilege_before = cpu.priv};
  snapshot(&initial);
  emit_snapshot(&initial, mem_host_ptr(c.base), c.size);
  for (uint64_t i = 0; i < c.steps; i++) {
    for (unsigned j=0;j<c.clock_count;j++)
      if(c.clock[j].event==i+1) sim.logical_ticks=c.clock[j].value;
    for (unsigned j=0;j<c.irq_count;j++) if(c.irq[j].event==i+1) {
      IrqLine *line=cpu_irq_input(c.irq[j].bit);
      if(c.irq[j].level) irq_raise(line); else irq_lower(line);
    }
    StepResult r;
    if (!engine_step(&r)) return 3;
    DiffSnapshot s = {.seq = i + 1, .pc_before = cpu.pc,
                      .privilege_before = cpu.priv, .outcome = "retired"};
    /* 测试工具的故意差异注入；不改模拟器核心或参考。 */
    if (!injected && !strcmp(inject, "gpr")) {
      cpu.gpr[5] ^= 1; injected = true;
    } else if (!injected && !strcmp(inject, "fpr")) {
      cpu.fpr[5]^=1; injected=true;
    }
    snapshot(&s);
    emit_snapshot(&s, mem_host_ptr(c.base), c.size);
  }
  mem_shutdown();
  free(c.ram);
  return 0;
}
