#include <common.h>
#include <util/log.h>

#include "cpu_state.h"
#include "csr.h"
#include "intr.h"
#include "pmp.h"
#include "mmu.h"

/* 体系结构状态实例 */
CPU_state cpu;

void cpu_reset(vaddr_t reset_pc) {
  memset(&cpu, 0, sizeof(cpu));
  cpu.pc = reset_pc;
  cpu.priv = PRV_M; /* RISC-V 复位后进入 M 模式 */
  cpu.mhartid = 0;

  riscv_mmu_reset();
  cpu.misa = riscv_misa_supported();

  csr_reset();
  riscv_pmp_reset();
  riscv_intr_reset(); /* 清空设备中断线电平快照（否则复用 CPU 时会残留） */
  Log("cpu reset: pc = " FMT_WORD ", priv = M", reset_pc);
}

const char *cpu_priv_name(uint8_t priv) {
  switch (priv) {
    case PRV_U: return "U";
    case PRV_S: return "S";
    case PRV_M: return "M";
    default:    return "?";
  }
}
