#ifndef RVISS_RISCV64_FP_EXEC_COMMON_H
#define RVISS_RISCV64_FP_EXEC_COMMON_H

#include <cpu/exec.h>

#if defined(CONFIG_RV_F) || defined(CONFIG_RV_D)
#include "platform.h"
#include "softfloat.h"
#endif

typedef word_t (*FpRawRead)(ExecContext* ctx, unsigned reg);
typedef void (*FpRawWrite)(ExecContext* ctx, unsigned reg, word_t value);
typedef word_t (*FpRawUnaryOp)(word_t value);
typedef word_t (*FpRawBinaryOp)(word_t lhs, word_t rhs);
typedef word_t (*FpRawTernaryOp)(word_t lhs, word_t rhs, word_t third);
typedef bool (*FpRawCompareOp)(word_t lhs, word_t rhs);
typedef word_t (*FpRawToIntOp)(word_t value, uint_fast8_t rm, bool exact);
typedef word_t (*FpRawFromIntOp)(word_t value);
typedef word_t (*FpRawMinMaxOp)(word_t lhs, word_t rhs);
typedef word_t (*FpRawClassifyOp)(word_t value);

static inline uint32_t fp_common_raw(ExecContext* ctx) {
  return ctx->raw_instr;
}

static inline unsigned fp_common_rd(ExecContext* ctx) {
  return (fp_common_raw(ctx) >> 7) & 0x1f;
}

static inline unsigned fp_common_rs1(ExecContext* ctx) {
  return (fp_common_raw(ctx) >> 15) & 0x1f;
}

static inline unsigned fp_common_rs2(ExecContext* ctx) {
  return (fp_common_raw(ctx) >> 20) & 0x1f;
}

static inline unsigned fp_common_rs3(ExecContext* ctx) {
  return (fp_common_raw(ctx) >> 27) & 0x1f;
}

static inline unsigned fp_common_rm(ExecContext* ctx) {
  return (fp_common_raw(ctx) >> 12) & 0x7;
}

static inline bool fp_common_enabled(const CpuState* hart) {
  return (hart->mstatus & MSTATUS_FS) != MSTATUS_FS_OFF;
}

static inline ExecResult fp_common_illegal(ExecContext* ctx) {
  return exec_trap(EXCP_ILLEGAL_INST, fp_common_raw(ctx));
}

#if defined(CONFIG_RV_F) || defined(CONFIG_RV_D)
static inline void fp_common_mark_dirty(CpuState* hart) {
  hart->mstatus = (hart->mstatus & ~MSTATUS_FS) | MSTATUS_FS_DIRTY;
}

static inline bool fp_common_set_rounding(ExecContext* ctx, unsigned rm) {
  if (rm == 7) rm = (ctx->hart->fcsr >> 5) & 0x7;
  if (rm > 4) return false;

  softfloat_roundingMode = (uint_fast8_t)rm;
  softfloat_detectTininess = softfloat_tininess_afterRounding;
  return true;
}

static inline void fp_common_begin(void) { softfloat_exceptionFlags = 0; }

static inline void fp_common_finish(ExecContext* ctx, bool writes_fpr) {
  const uint_fast8_t flags = softfloat_exceptionFlags & 0x1f;
  if (flags != 0) {
    ctx->hart->fcsr |= flags;
    fp_common_mark_dirty(ctx->hart);
  }
  if (writes_fpr) fp_common_mark_dirty(ctx->hart);
  softfloat_exceptionFlags = 0;
}

static inline ExecResult fp_common_binary(ExecContext* ctx, FpRawRead read,
                                          FpRawWrite write,
                                          FpRawBinaryOp operation) {
  if (!fp_common_enabled(ctx->hart)) return fp_common_illegal(ctx);
  if (!fp_common_set_rounding(ctx, fp_common_rm(ctx)))
    return fp_common_illegal(ctx);

  fp_common_begin();
  const word_t lhs = read(ctx, fp_common_rs1(ctx));
  const word_t rhs = read(ctx, fp_common_rs2(ctx));
  write(ctx, fp_common_rd(ctx), operation(lhs, rhs));
  fp_common_finish(ctx, true);
  return exec_ok();
}

static inline ExecResult fp_common_unary(ExecContext* ctx, FpRawRead read,
                                         FpRawWrite write,
                                         FpRawUnaryOp operation) {
  if (!fp_common_enabled(ctx->hart)) return fp_common_illegal(ctx);
  if (!fp_common_set_rounding(ctx, fp_common_rm(ctx)))
    return fp_common_illegal(ctx);

  fp_common_begin();
  const word_t value = read(ctx, fp_common_rs1(ctx));
  write(ctx, fp_common_rd(ctx), operation(value));
  fp_common_finish(ctx, true);
  return exec_ok();
}

static inline ExecResult fp_common_fma(ExecContext* ctx, FpRawRead read,
                                       FpRawWrite write, FpRawTernaryOp op,
                                       word_t sign_bit, bool negate_product,
                                       bool negate_third) {
  if (!fp_common_enabled(ctx->hart)) return fp_common_illegal(ctx);
  if (!fp_common_set_rounding(ctx, fp_common_rm(ctx)))
    return fp_common_illegal(ctx);

  fp_common_begin();
  word_t lhs = read(ctx, fp_common_rs1(ctx));
  const word_t rhs = read(ctx, fp_common_rs2(ctx));
  word_t third = read(ctx, fp_common_rs3(ctx));
  if (negate_product) lhs ^= sign_bit;
  if (negate_third) third ^= sign_bit;
  write(ctx, fp_common_rd(ctx), op(lhs, rhs, third));
  fp_common_finish(ctx, true);
  return exec_ok();
}

static inline ExecResult fp_common_compare(ExecContext* ctx, FpRawRead read,
                                           FpRawCompareOp operation) {
  if (!fp_common_enabled(ctx->hart)) return fp_common_illegal(ctx);

  fp_common_begin();
  const word_t lhs = read(ctx, fp_common_rs1(ctx));
  const word_t rhs = read(ctx, fp_common_rs2(ctx));
  gpr_write(ctx->hart, (uint8_t)fp_common_rd(ctx), operation(lhs, rhs) ? 1 : 0);
  fp_common_finish(ctx, false);
  return exec_ok();
}

static inline ExecResult fp_common_to_int(ExecContext* ctx, FpRawRead read,
                                          FpRawToIntOp operation,
                                          bool word_result) {
  if (!fp_common_enabled(ctx->hart)) return fp_common_illegal(ctx);
  if (!fp_common_set_rounding(ctx, fp_common_rm(ctx)))
    return fp_common_illegal(ctx);

  fp_common_begin();
  const word_t value = operation(read(ctx, fp_common_rs1(ctx)),
                                 softfloat_roundingMode, true);
  word_t result = value;
  if (word_result) result = (word_t)(sword_t)(int32_t)(uint32_t)value;
  gpr_write(ctx->hart, (uint8_t)fp_common_rd(ctx), result);
  fp_common_finish(ctx, false);
  return exec_ok();
}

static inline ExecResult fp_common_from_int(ExecContext* ctx, FpRawWrite write,
                                            FpRawFromIntOp operation) {
  if (!fp_common_enabled(ctx->hart)) return fp_common_illegal(ctx);
  if (!fp_common_set_rounding(ctx, fp_common_rm(ctx)))
    return fp_common_illegal(ctx);

  fp_common_begin();
  const word_t value = gpr_read(ctx->hart, (uint8_t)fp_common_rs1(ctx));
  write(ctx, fp_common_rd(ctx), operation(value));
  fp_common_finish(ctx, true);
  return exec_ok();
}

static inline ExecResult fp_common_minmax(ExecContext* ctx, FpRawRead read,
                                          FpRawWrite write,
                                          FpRawMinMaxOp operation) {
  if (!fp_common_enabled(ctx->hart)) return fp_common_illegal(ctx);

  fp_common_begin();
  const word_t lhs = read(ctx, fp_common_rs1(ctx));
  const word_t rhs = read(ctx, fp_common_rs2(ctx));
  write(ctx, fp_common_rd(ctx), operation(lhs, rhs));
  fp_common_finish(ctx, true);
  return exec_ok();
}

static inline ExecResult fp_common_classify(ExecContext* ctx, FpRawRead read,
                                            FpRawClassifyOp operation) {
  if (!fp_common_enabled(ctx->hart)) return fp_common_illegal(ctx);

  const word_t value = read(ctx, fp_common_rs1(ctx));
  gpr_write(ctx->hart, (uint8_t)fp_common_rd(ctx), operation(value));
  return exec_ok();
}

static inline word_t fp_common_sign_inject(word_t lhs, word_t rhs,
                                           word_t sign_bit, bool negate,
                                           bool xor_sign) {
  const word_t selected = xor_sign ? lhs ^ rhs : negate ? ~rhs : rhs;
  return (lhs & ~sign_bit) | (selected & sign_bit);
}
#endif /* defined(CONFIG_RV_F) || defined(CONFIG_RV_D) */

#endif /* RVISS_RISCV64_FP_EXEC_COMMON_H */
