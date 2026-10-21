#include <cpu/exec.h>
#include <memory/vaddr.h>
#include "encoding.h"

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

static unsigned ialign_bytes(const CpuState* hart) {
#ifdef CONFIG_RV_C
  if ((hart->misa & MISA_C) != 0) return 2;
#else
  (void)hart;
#endif
  return 4;
}

static ExecResult redirect(ExecContext* ctx, vaddr_t target) {
  if ((target & (ialign_bytes(ctx->hart) - 1)) != 0) {
    return exec_trap(EXCP_INST_ADDR_MIS, target);
  }
  ctx->npc = target;
  return exec_ok();
}

static inline ExecResult exec_branch(ExecContext* ctx, bool taken) {
  if (!taken) return exec_ok();
  return redirect(ctx, PC(ctx) + INT_IMM(ctx));
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
/* ================= U-type：LUI / AUIPC ================= */
ExecResult exec_lui(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_IMM(ctx));
  return exec_ok();
}

ExecResult exec_auipc(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), PC(ctx) + INT_IMM(ctx));
  return exec_ok();
}

/* ================= 无条件跳转：JAL / JALR ================= */
ExecResult exec_jal(ExecContext* ctx) {
  const vaddr_t target = PC(ctx) + INT_IMM(ctx);
  if ((target & (ialign_bytes(ctx->hart) - 1)) != 0) {
    return exec_trap(EXCP_INST_ADDR_MIS, target);
  }
  gpr_write(ctx->hart, INT_RD(ctx), ctx->npc);
  ctx->npc = target;
  return exec_ok();
}

ExecResult exec_jalr(ExecContext* ctx) {
  /* Compute the target before writing rd: jalr permits rd == rs1. */
  const vaddr_t target = (INT_SRC1(ctx) + INT_IMM(ctx)) & ~UINT64_C(1);
  if ((target & (ialign_bytes(ctx->hart) - 1)) != 0) {
    return exec_trap(EXCP_INST_ADDR_MIS, target);
  }
  gpr_write(ctx->hart, INT_RD(ctx), ctx->npc);
  ctx->npc = target;
  return exec_ok();
}

/* ================= 条件分支 ================= */
ExecResult exec_beq(ExecContext* ctx) {
  return exec_branch(ctx, INT_SRC1(ctx) == INT_SRC2(ctx));
}

ExecResult exec_bne(ExecContext* ctx) {
  return exec_branch(ctx, INT_SRC1(ctx) != INT_SRC2(ctx));
}

ExecResult exec_blt(ExecContext* ctx) {
  return exec_branch(ctx, (sword_t)INT_SRC1(ctx) < (sword_t)INT_SRC2(ctx));
}

ExecResult exec_bge(ExecContext* ctx) {
  return exec_branch(ctx, (sword_t)INT_SRC1(ctx) >= (sword_t)INT_SRC2(ctx));
}

ExecResult exec_bltu(ExecContext* ctx) {
  return exec_branch(ctx, INT_SRC1(ctx) < INT_SRC2(ctx));
}

ExecResult exec_bgeu(ExecContext* ctx) {
  return exec_branch(ctx, INT_SRC1(ctx) >= INT_SRC2(ctx));
}

static ExecResult exec_load(ExecContext* ctx, int len, word_t (*extend)(word_t)) {
  const vaddr_t addr = INT_SRC1(ctx) + INT_IMM(ctx);
  const MemResult mem = vaddr_read(addr, len);
  if (mem.exception) return exec_trap(mem.cause, mem.tval);
  gpr_write(ctx->hart, INT_RD(ctx), extend(mem.data));
  return exec_ok();
}

static ExecResult exec_store(ExecContext* ctx, int len) {
  const vaddr_t addr = INT_SRC1(ctx) + INT_IMM(ctx);
  ctx->hart->reservation_valid = false;
  const MemResult mem = vaddr_write(addr, len, INT_SRC2(ctx));
  if (mem.exception) return exec_trap(mem.cause, mem.tval);
  return exec_ok();
}

/* ================= load ================= */
ExecResult exec_lb(ExecContext* ctx) { return exec_load(ctx, 1, ext_s8); }
ExecResult exec_lh(ExecContext* ctx) { return exec_load(ctx, 2, ext_s16); }
ExecResult exec_lw(ExecContext* ctx) { return exec_load(ctx, 4, ext_s32); }
ExecResult exec_ld(ExecContext* ctx) { return exec_load(ctx, 8, ext_none); }
ExecResult exec_lbu(ExecContext* ctx) { return exec_load(ctx, 1, ext_u8); }
ExecResult exec_lhu(ExecContext* ctx) { return exec_load(ctx, 2, ext_u16); }
ExecResult exec_lwu(ExecContext* ctx) { return exec_load(ctx, 4, ext_u32); }

/* ================= store ================= */
ExecResult exec_sb(ExecContext* ctx) { return exec_store(ctx, 1); }
ExecResult exec_sh(ExecContext* ctx) { return exec_store(ctx, 2); }
ExecResult exec_sw(ExecContext* ctx) { return exec_store(ctx, 4); }
ExecResult exec_sd(ExecContext* ctx) { return exec_store(ctx, 8); }

/* ================= 寄存器-立即数运算 ================= */
ExecResult exec_addi(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) + INT_IMM(ctx));
  return exec_ok();
}

ExecResult exec_slti(ExecContext* ctx) {
  const word_t result = (sword_t)INT_SRC1(ctx) < (sword_t)INT_IMM(ctx);
  gpr_write(ctx->hart, INT_RD(ctx), result);
  return exec_ok();
}

ExecResult exec_sltiu(ExecContext* ctx) {
  const word_t result = INT_SRC1(ctx) < INT_IMM(ctx);
  gpr_write(ctx->hart, INT_RD(ctx), result);
  return exec_ok();
}

ExecResult exec_xori(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) ^ INT_IMM(ctx));
  return exec_ok();
}

ExecResult exec_ori(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) | INT_IMM(ctx));
  return exec_ok();
}

ExecResult exec_andi(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) & INT_IMM(ctx));
  return exec_ok();
}

/* RV64：ADDIW 紧跟 ADDI */
ExecResult exec_addiw(ExecContext* ctx) {
  uint32_t result = (uint32_t)INT_SRC1(ctx) + (uint32_t)INT_IMM(ctx);
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

/* 立即数移位：SLLI / SRLI / SRAI，RV64 版 shamt 6 位 */
ExecResult exec_slli(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_IMM(ctx) & 0x3fU;
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) << shamt);
  return exec_ok();
}

ExecResult exec_srli(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_IMM(ctx) & 0x3fU;
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) >> shamt);
  return exec_ok();
}

ExecResult exec_srai(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_IMM(ctx) & 0x3fU;
  gpr_write(ctx->hart, INT_RD(ctx),
            arithmetic_shift_right64(INT_SRC1(ctx), shamt));
  return exec_ok();
}

/* RV64：32 位立即数移位，shamt 5 位 */
ExecResult exec_slliw(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_IMM(ctx) & 0x1fU;
  const uint32_t result = (uint32_t)INT_SRC1(ctx) << shamt;
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

ExecResult exec_srliw(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_IMM(ctx) & 0x1fU;
  const uint32_t result = (uint32_t)INT_SRC1(ctx) >> shamt;
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

ExecResult exec_sraiw(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_IMM(ctx) & 0x1fU;
  const uint32_t result =
      arithmetic_shift_right32((uint32_t)INT_SRC1(ctx), shamt);
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

/* ================= 寄存器-寄存器运算 ================= */
ExecResult exec_add(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) + INT_SRC2(ctx));
  return exec_ok();
}

ExecResult exec_sub(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) - INT_SRC2(ctx));
  return exec_ok();
}

ExecResult exec_sll(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_SRC2(ctx) & 0x3fU;
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) << shamt);
  return exec_ok();
}

ExecResult exec_slt(ExecContext* ctx) {
  const word_t result = (sword_t)INT_SRC1(ctx) < (sword_t)INT_SRC2(ctx);
  gpr_write(ctx->hart, INT_RD(ctx), result);
  return exec_ok();
}

ExecResult exec_sltu(ExecContext* ctx) {
  const word_t result = INT_SRC1(ctx) < INT_SRC2(ctx);
  gpr_write(ctx->hart, INT_RD(ctx), result);
  return exec_ok();
}

ExecResult exec_xor(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) ^ INT_SRC2(ctx));
  return exec_ok();
}

ExecResult exec_srl(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_SRC2(ctx) & 0x3fU;
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) >> shamt);
  return exec_ok();
}

ExecResult exec_sra(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_SRC2(ctx) & 0x3fU;
  gpr_write(ctx->hart, INT_RD(ctx),
            arithmetic_shift_right64(INT_SRC1(ctx), shamt));
  return exec_ok();
}

ExecResult exec_or(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) | INT_SRC2(ctx));
  return exec_ok();
}

ExecResult exec_and(ExecContext* ctx) {
  gpr_write(ctx->hart, INT_RD(ctx), INT_SRC1(ctx) & INT_SRC2(ctx));
  return exec_ok();
}

/* RV64：32 位寄存器运算 */
ExecResult exec_addw(ExecContext* ctx) {
  uint32_t result = (uint32_t)INT_SRC1(ctx) + (uint32_t)INT_SRC2(ctx);
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

ExecResult exec_subw(ExecContext* ctx) {
  const uint32_t result = (uint32_t)INT_SRC1(ctx) - (uint32_t)INT_SRC2(ctx);
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

ExecResult exec_sllw(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_SRC2(ctx) & 0x1fU;
  const uint32_t result = (uint32_t)INT_SRC1(ctx) << shamt;
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

ExecResult exec_srlw(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_SRC2(ctx) & 0x1fU;
  const uint32_t result = (uint32_t)INT_SRC1(ctx) >> shamt;
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

ExecResult exec_sraw(ExecContext* ctx) {
  const unsigned shamt = (unsigned)INT_SRC2(ctx) & 0x1fU;
  const uint32_t result =
      arithmetic_shift_right32((uint32_t)INT_SRC1(ctx), shamt);
  gpr_write(ctx->hart, INT_RD(ctx), sign_extend_word32(result));
  return exec_ok();
}

/* ================= 内存序 ================= */
ExecResult exec_fence(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}

ExecResult exec_fence_tso(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}

/* PAUSE 编码属 FENCE 类（funct3=000, opcode=0001111） */
ExecResult exec_pause(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}

/* ================= 环境调用 ================= */
ExecResult exec_ecall(ExecContext* ctx) {
  return exec_trap(ecall_exception(ctx->hart), 0);
}

ExecResult exec_ebreak(ExecContext* ctx) {
  return exec_trap(EXCP_BREAKPOINT, PC(ctx));
}