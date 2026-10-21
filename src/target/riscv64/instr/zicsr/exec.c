#include <cpu/exec.h>

#include "csr.h"

/*
 * CSRRW/CSRRWI：rd≠x0 才读，总是写。
 * CSRRS/CSRRC：总是读，rs1 寄存器号不是 x0 才写。
 * CSRRSI/CSRRCI：总是读，uimm≠0 才写。
 * 旧值只在 rd≠x0 时写入。读副作用只在确实读、且读写都合法时发生。
 */
static ExecResult csr_access(ExecContext* ctx, bool always_write, bool set,
                             bool clear, bool imm) {
  const word_t addr = BITS(INT_IMM(ctx), 11, 0);
  const unsigned rd = INT_RD(ctx);
  const unsigned rs1 = INT_RS1(ctx);
  const word_t src = imm ? rs1 : INT_SRC1(ctx);
  const bool do_read = !always_write || rd != 0;
  const bool do_write = always_write || rs1 != 0;

  if ((do_read && !csr_accessible(ctx->hart->priv, addr, false)) ||
      (do_write && !csr_accessible(ctx->hart->priv, addr, true))) {
    return exec_trap(EXCP_ILLEGAL_INST, ctx->raw_instr);
  }

  word_t old = 0;
  if (do_read && !csr_read(ctx->hart, addr, &old)) {
    return exec_trap(EXCP_ILLEGAL_INST, ctx->raw_instr);
  }

  if (do_write) {
    const word_t value = set ? (old | src) : clear ? (old & ~src) : src;
    if (!csr_write(ctx->hart, addr, value)) {
      return exec_trap(EXCP_ILLEGAL_INST, ctx->raw_instr);
    }
  }

  if (do_read) gpr_write(ctx->hart, (uint8_t)rd, old);

  return exec_ok();
}

ExecResult exec_csrrw(ExecContext* ctx) {
  return csr_access(ctx, true, false, false, false);
}
ExecResult exec_csrrs(ExecContext* ctx) {
  return csr_access(ctx, false, true, false, false);
}
ExecResult exec_csrrc(ExecContext* ctx) {
  return csr_access(ctx, false, false, true, false);
}
ExecResult exec_csrrwi(ExecContext* ctx) {
  return csr_access(ctx, true, false, false, true);
}
ExecResult exec_csrrsi(ExecContext* ctx) {
  return csr_access(ctx, false, true, false, true);
}
ExecResult exec_csrrci(ExecContext* ctx) {
  return csr_access(ctx, false, false, true, true);
}
