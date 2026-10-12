#include <common.h>
#include <cpu/decode.h>
#include <memory/vaddr.h>
#include <util/log.h>
#include <util/state.h>

#include "cpu_state.h"
#include "csr.h"
#include "encoding.h"
#include "intr.h"
#include "mmu.h"

/*
 * 特权/系统指令语义。
 * 这些指令会直接和 CSR、陷阱、状态机打交道——这是它们与普通算术指令的区别。
 */

/* ecall：cause = 8 + 当前特权级（U=8, S=9, M=11） */
void exec_ecall(Decode* s) {
  riscv_exec_trap(s, EX_ECU + cpu.priv, 0);
}

void exec_ebreak(Decode* s) { riscv_exec_trap(s, EX_BP, s->pc); }

void exec_mret(Decode* s) {
  if (cpu.priv != PRV_M) {
    riscv_exec_trap(s, EX_II, s->instr);
    return;
  }
  cpu.pc = riscv_mret();
}

void exec_sret(Decode* s) {
  if (cpu.priv < PRV_S || (cpu.priv == PRV_S && (cpu.mstatus & MSTATUS_TSR))) {
    riscv_exec_trap(s, EX_II, s->instr);
    return;
  }
  cpu.pc = riscv_sret();
}

void exec_wfi(Decode* s) {
  /* TW 选择超时0；正常退休后引擎可等待下一个确定性设备事件。 */
  if (cpu.priv == PRV_U || (cpu.priv != PRV_M && (cpu.mstatus & MSTATUS_TW)))
    riscv_exec_trap(s, EX_II, s->instr);
  else
    s->wait_request = true;
}

void exec_sfence_vma(Decode* s) {
  if (!cpu.sv39_supported || cpu.priv < PRV_S ||
      (cpu.priv == PRV_S && (cpu.mstatus & MSTATUS_TVM))) {
    riscv_exec_trap(s, EX_II, s->instr);
    return;
  }
  isa_mmu_flush_tlb(); /* ASIDLEN=0，可保守失效所有地址；当前每次真实 walk */
}

/* ---------------- CSR 指令 ---------------- */
void exec_csrrw(Decode* s) { csr_execute(s, CSR_OP_RW, false); }
void exec_csrrs(Decode* s) { csr_execute(s, CSR_OP_RS, false); }
void exec_csrrc(Decode* s) { csr_execute(s, CSR_OP_RC, false); }
void exec_csrrwi(Decode* s) { csr_execute(s, CSR_OP_RW, true); }
void exec_csrrsi(Decode* s) { csr_execute(s, CSR_OP_RS, true); }
void exec_csrrci(Decode* s) { csr_execute(s, CSR_OP_RC, true); }

/* ---------------- NEMU 约定的退出指令 ---------------- */
void exec_nemu_trap(Decode* s) {
  sim.halt_pc = s->pc;
  sim.halt_code = (int)cpu.gpr[10]; /* a0 是退出码 */
  sim.state = SIM_HALTED;
  LogInstr("nemu_trap at pc = " FMT_WORD ", a0 = %d", s->pc, sim.halt_code);
}
