#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <cpu/step.h>
#include <memory/vaddr.h>

#include "trap.h"

static void inline take_trap(word_t cause, vaddr_t epc, word_t tval) {
  cpu.pc = riscv_raise_trap(cause, epc, tval);
}

static void retire_counters(void) {
  if ((cpu.mcountinhibit & 1) == 0) cpu.mcycle++;
  if ((cpu.mcountinhibit & 4) == 0) cpu.minstret++;
}

void riscv_execute(void) {
  const vaddr_t pc = cpu.pc;
  uint32_t instr = 0;
  uint8_t ilen = 4;
  word_t illegal_tval = 0;

  const MemResult lo = vaddr_ifetch(pc, 2);
  if (lo.exception) {
    take_trap(lo.cause, pc, lo.tval);
    return;
  }

  const uint16_t parcel = (uint16_t)lo.data;

  if ((parcel & 0x3) != 0x3) {
    if (!riscv_expand_compressed(parcel, &instr)) {
      take_trap(EXCP_ILLEGAL_INST, pc, parcel);
      return;
    }
    ilen = 2;
    illegal_tval = parcel;
  } else if ((parcel & 0x1f) == 0x1f) {
    take_trap(EXCP_ILLEGAL_INST, pc, parcel);
    return;
  } else {
    const MemResult hi = vaddr_ifetch(pc + 2, 2);
    if (hi.exception) {
      take_trap(hi.cause, pc, hi.tval);
      return;
    }
    instr = (uint32_t)parcel | ((uint32_t)hi.data << 16);
    illegal_tval = instr;
  }

  Decode decoded = {.ilen = ilen};
  if (!riscv_decode(instr, &decoded) || decoded.illegal ||
      decoded.exec == NULL) {
    take_trap(EXCP_ILLEGAL_INST, pc, illegal_tval);
    return;
  }

  ExecContext ctx = {
      .hart = &cpu,
      .npc = pc + ilen,
      .raw_instr = instr,
      .operands = decoded.operands,
  };
  const ExecResult result = decoded.exec(&ctx);
  if (result.exception) {
    take_trap(result.cause, pc, result.tval);
    return;
  }

  cpu.pc = ctx.npc;
  retire_counters();
}

void riscv_step(StepResult* result) {
  (void)result;
  if (cpu.wfi_sleep) {
    if (!riscv_wait_resume()) return;
    cpu.wfi_sleep = false;
  }
  const word_t intr = riscv_query_intr();
  if (intr != 0) {
    take_trap(intr, cpu.pc, 0);
    return;
  }
  riscv_execute();
}
