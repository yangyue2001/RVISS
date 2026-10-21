#include <cpu/exec.h>

#include "csr.h"
#include "encoding.h"

enum { CSR_FUNCT3_IMMEDIATE = 4 };

static ExecResult csr_access(ExecContext* ctx, bool write_src, bool set,
                             bool clear) {
  const uint32_t raw = ctx->raw_instr;
  const word_t addr = BITS(raw, 31, 20);
  const bool imm = (FUNCT3(raw) & CSR_FUNCT3_IMMEDIATE) != 0;
  const word_t src = imm ? (word_t)RS1(raw) : INT_SRC1(ctx);
  const bool do_write =
      write_src || (imm ? src != 0 : INT_RS1(ctx) != 0);
  word_t old = 0;
  if (!csr_read(addr, &old)) return exec_trap(EXCP_ILLEGAL_INST, raw);
  if (do_write) {
    const word_t value = set ? (old | src) : clear ? (old & ~src) : src;
    if (!csr_write(addr, value)) return exec_trap(EXCP_ILLEGAL_INST, raw);
  }
  gpr_write(ctx->hart, INT_RD(ctx), old);
  return exec_ok();
}

ExecResult exec_csrrw(ExecContext* ctx) {
  return csr_access(ctx, true, false, false);
}
ExecResult exec_csrrs(ExecContext* ctx) {
  return csr_access(ctx, false, true, false);
}
ExecResult exec_csrrc(ExecContext* ctx) {
  return csr_access(ctx, false, false, true);
}
ExecResult exec_csrrwi(ExecContext* ctx) {
  return csr_access(ctx, true, false, false);
}
ExecResult exec_csrrsi(ExecContext* ctx) {
  return csr_access(ctx, false, true, false);
}
ExecResult exec_csrrci(ExecContext* ctx) {
  return csr_access(ctx, false, false, true);
}
