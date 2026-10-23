#include <common.h>
#include <cpu/cpu.h>
#include <util/log.h>

#include "csr.h"

/* 体系结构状态实例 */
CpuState cpu;

void cpu_reset(vaddr_t reset_pc) {
  memset(&cpu, 0, sizeof(cpu));
  cpu.pc = reset_pc;
  cpu.priv = PRV_M; /* RISC-V 复位后进入 M 模式 */
  csr_init(&cpu);
#ifdef CONFIG_RV_PMP
  cpu.pmp_count = CPU_PMP_ENTRIES;
#endif
  Log("cpu reset: pc = " FMT_WORD ", priv = M", reset_pc);
}

const char* cpu_priv_name(Priv priv) {
  switch (priv) {
    case PRV_U:
      return "U";
    case PRV_S:
      return "S";
    case PRV_M:
      return "M";
    default:
      return "?";
  }
}
