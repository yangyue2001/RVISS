#include <cpu/exec.h>

static inline ExecResult exec_ok(void) {
  return (ExecResult){
      .trap = false,
  };
}

static inline ExecResult exec_trap(Exception cause, word_t tval) {
  return (ExecResult){
      .trap = true,
      .tval = tval,
      .cause = cause,
  };
}

static inline word_t sign_extend_word32(uint32_t value) {
  return (word_t)(sword_t)(int32_t)value;
}

/* Keep arithmetic right shifts independent of the host signed-shift rule. */
static inline word_t arithmetic_shift_right64(word_t value, unsigned shamt) {
  if (shamt == 0) return value;

  word_t result = value >> shamt;
  if ((value >> 63) != 0) result |= UINT64_MAX << (64 - shamt);
  return result;
}

static inline uint32_t arithmetic_shift_right32(uint32_t value,
                                                unsigned shamt) {
  if (shamt == 0) return value;

  uint32_t result = value >> shamt;
  if ((value >> 31) != 0) result |= UINT32_MAX << (32 - shamt);
  return result;
}

static inline ExecResult exec_branch(ExecContext* ctx, bool taken) {
  if (taken) ctx->npc = PC(ctx) + INT_IMM(ctx);
  return exec_ok();
}

static inline Exception ecall_exception(const CpuState* hart) {
  switch (hart->priv) {
    case PRV_U:
      return EXCP_U_ECALL;
    case PRV_S:
      return EXCP_S_ECALL;
    case PRV_M:
    default:
      return EXCP_M_ECALL;
  }
}

static ExecResult exec_add(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) + INT_SRC2(ctx));
  return exec_ok();
}

static ExecResult exec_addi(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) + INT_IMM(ctx));
  return exec_ok();
}

static ExecResult exec_addiw(ExecContext* ctx) {
  uint32_t result = (uint32_t)INT_SRC1(ctx) + (uint32_t)INT_IMM(ctx);
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

static ExecResult exec_addw(ExecContext* ctx) {
  uint32_t result = (uint32_t)INT_SRC1(ctx) + (uint32_t)INT_SRC2(ctx);
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

static ExecResult exec_and(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) & INT_SRC2(ctx));
  return exec_ok();
}

static ExecResult exec_andi(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) & INT_IMM(ctx));
  return exec_ok();
}

static ExecResult exec_auipc(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), PC(ctx) + INT_IMM(ctx));
  return exec_ok();
}

static ExecResult exec_beq(ExecContext* ctx) {
  return exec_branch(ctx, INT_SRC1(ctx) == INT_SRC2(ctx));
}

static ExecResult exec_bge(ExecContext* ctx) {
  return exec_branch(ctx, (sword_t)INT_SRC1(ctx) >= (sword_t)INT_SRC2(ctx));
}

static ExecResult exec_bgeu(ExecContext* ctx) {
  return exec_branch(ctx, INT_SRC1(ctx) >= INT_SRC2(ctx));
}

static ExecResult exec_blt(ExecContext* ctx) {
  return exec_branch(ctx, (sword_t)INT_SRC1(ctx) < (sword_t)INT_SRC2(ctx));
}

static ExecResult exec_bltu(ExecContext* ctx) {
  return exec_branch(ctx, INT_SRC1(ctx) < INT_SRC2(ctx));
}

static ExecResult exec_bne(ExecContext* ctx) {
  return exec_branch(ctx, INT_SRC1(ctx) != INT_SRC2(ctx));
}

static ExecResult exec_ebreak(ExecContext* ctx) {
  return exec_trap(EXCP_BREAKPOINT, PC(ctx));
}

static ExecResult exec_ecall(ExecContext* ctx) {
  return exec_trap(ecall_exception(ctx->hart), 0);
}

static ExecResult exec_fence(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}

static ExecResult exec_fence_tso(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}

static ExecResult exec_jal(ExecContext* ctx) {
  const vaddr_t target = PC(ctx) + INT_IMM(ctx);
  const word_t link = ctx->npc;
  gpr_write(ctx->hart, INT_RD(ctx), link);
  ctx->npc = target;
  return exec_ok();
}

static ExecResult exec_jalr(ExecContext* ctx) {
  /* Compute the target before writing rd: jalr permits rd == rs1. */
  const vaddr_t target = (INT_SRC1(ctx) + INT_IMM(ctx)) & ~UINT64_C(1);
  const word_t link = ctx->npc;
  gpr_write(ctx->hart, INT_RD(ctx), link);
  ctx->npc = target;
  return exec_ok();
}

static ExecResult exec_lui(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_IMM(ctx));
  return exec_ok();
}

static ExecResult exec_nop(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}

static ExecResult exec_or(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) | INT_SRC2(ctx));
  return exec_ok();
}

static ExecResult exec_ori(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) | INT_IMM(ctx));
  return exec_ok();
}

static ExecResult exec_sll(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_SRC2(ctx) & 0x3fU;
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) << shamt);
  return exec_ok();
}

static ExecResult exec_slli(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_IMM(ctx) & 0x3fU;
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) << shamt);
  return exec_ok();
}

static ExecResult exec_slliw(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_IMM(ctx) & 0x1fU;
  const uint32_t result = (uint32_t)INT_SRC1(ctx) << shamt;
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

static ExecResult exec_sllw(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_SRC2(ctx) & 0x1fU;
  const uint32_t result = (uint32_t)INT_SRC1(ctx) << shamt;
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

static ExecResult exec_slt(ExecContext* ctx) {
  const word_t result = (sword_t)INT_SRC1(ctx) < (sword_t)INT_SRC2(ctx);
  gpr_write(ctx->hart, INT_RD(ctx), result);
  return exec_ok();
}

static ExecResult exec_slti(ExecContext* ctx) {
  const word_t result = (sword_t)INT_SRC1(ctx) < (sword_t)INT_IMM(ctx);
  gpr_write(ctx->hart, INT_RD(ctx), result);
  return exec_ok();
}

static ExecResult exec_sltiu(ExecContext* ctx) {
  const word_t result = INT_SRC1(ctx) < INT_IMM(ctx);
  gpr_write(ctx->hart, INT_RD(ctx), result);
  return exec_ok();
}

static ExecResult exec_sltu(ExecContext* ctx) {
  const word_t result = INT_SRC1(ctx) < INT_SRC2(ctx);
  gpr_write(ctx->hart, INT_RD(ctx), result);
  return exec_ok();
}

static ExecResult exec_sra(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_SRC2(ctx) & 0x3fU;
  gpr_write(ctx->hart, INT_RD(ctx),
            arithmetic_shift_right64(INT_SRC1(ctx), shamt));
  return exec_ok();
}

static ExecResult exec_srai(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_IMM(ctx) & 0x3fU;
  gpr_write(ctx->hart, INT_RD(ctx),
            arithmetic_shift_right64(INT_SRC1(ctx), shamt));
  return exec_ok();
}

static ExecResult exec_sraiw(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_IMM(ctx) & 0x1fU;
  const uint32_t result = arithmetic_shift_right32((uint32_t)INT_SRC1(ctx),
                                                   shamt);
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

static ExecResult exec_sraw(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_SRC2(ctx) & 0x1fU;
  const uint32_t result = arithmetic_shift_right32((uint32_t)INT_SRC1(ctx),
                                                   shamt);
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

static ExecResult exec_srl(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_SRC2(ctx) & 0x3fU;
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) >> shamt);
  return exec_ok();
}

static ExecResult exec_srli(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_IMM(ctx) & 0x3fU;
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) >> shamt);
  return exec_ok();
}

static ExecResult exec_srliw(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_IMM(ctx) & 0x1fU;
  const uint32_t result = (uint32_t)INT_SRC1(ctx) >> shamt;
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

static ExecResult exec_srlw(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_SRC2(ctx) & 0x1fU;
  const uint32_t result = (uint32_t)INT_SRC1(ctx) >> shamt;
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

static ExecResult exec_sub(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) - INT_SRC2(ctx));
  return exec_ok();
}

static ExecResult exec_subw(ExecContext* ctx) {
  const uint32_t result = (uint32_t)INT_SRC1(ctx) - (uint32_t)INT_SRC2(ctx);
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

static ExecResult exec_xor(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) ^ INT_SRC2(ctx));
  return exec_ok();
}

static ExecResult exec_xori(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) ^ INT_IMM(ctx));
  return exec_ok();
}
