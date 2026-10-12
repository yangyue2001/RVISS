#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <cpu/interrupt.h>
#include <cpu/step.h>
#include <util/state.h>

#include "intr.h"

void riscv_exec_trap(Decode *s, word_t cause, vaddr_t tval) {
  if (s->trap_pending) return; /* 保留第一处故障 */
  s->trap_pending = true;
  s->trap_cause = cause;
  s->trap_tval = tval;
}

bool riscv_jump(Decode *s, vaddr_t target) {
  if (target & ((cpu.misa & MISA_C) ? 1 : 3)) {
    riscv_exec_trap(s, EX_IAM, target);
    return false;
  }
  cpu.pc = target;
  return true;
}

void riscv_step(StepResult *result) {
  *result = (StepResult){.pc_before = cpu.pc,
                         .privilege_before = cpu.priv};
  const word_t intr = riscv_query_intr();
  if (intr != 0) {
    result->outcome = STEP_INTERRUPT;
    result->trap.valid = true;
    result->trap.cause = intr;
    result->trap.epc = cpu.pc;
    cpu.pc = riscv_raise_trap(intr, cpu.pc, 0);
  } else {
    Decode s = {.pc = cpu.pc};
    result->instruction_attempted = true;
    if (riscv_decode(&s)) {
      cpu.pc = s.snpc;
      s.exec(&s);
    } else if (!s.trap_pending && !s.host_error) {
      riscv_exec_trap(&s, EX_II, s.instr);
    }
    result->raw_instruction = s.instr;
    result->instruction_length = s.ilen;
    result->raw_bytes = s.raw_bytes;
    result->instruction_valid = s.instruction_valid;
    result->operation = s.mnemonic;
    result->memory = s.memory;
    if (s.host_error) {
      cpu.pc = s.pc;
      result->outcome = STEP_HOST_ERROR;
    } else if (s.trap_pending) {
      result->outcome = STEP_SYNCHRONOUS_TRAP;
      result->trap.valid = true;
      result->trap.cause = s.trap_cause;
      result->trap.epc = s.pc;
      result->trap.tval = s.trap_tval;
      cpu.pc = riscv_raise_trap(s.trap_cause, s.pc, s.trap_tval);
    } else {
      result->retired = true;
      result->outcome = sim.state == SIM_HALTED ? STEP_HALTED : STEP_RETIRED;
      if (s.rd != 0 && s.dest.preg != NULL) {
        result->register_write = true;
        result->rd = s.rd;
        result->register_value = cpu.gpr[s.rd];
      }
    }
  }
  cpu.gpr[0] = 0;
  result->pc_after = cpu.pc;
  result->privilege_after = cpu.priv;
}
