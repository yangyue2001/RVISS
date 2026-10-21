#ifndef RVISS_RISCV64_FP_EXEC_COMMON_H
#define RVISS_RISCV64_FP_EXEC_COMMON_H

#include <cpu/csr_machine.h>
#include <cpu/csr_user.h>
#include <cpu/exec.h>


#include "platform.h"
#include "softfloat.h"

#define FP32_MANT_BITS 23u
#define FP32_EXP_BITS 8u
#define FP32_BITS (FP32_MANT_BITS + FP32_EXP_BITS + 1u)
#define FP64_MANT_BITS 52u
#define FP64_EXP_BITS 11u

typedef enum {
  FP_CLASS_NEG_INF = 0,
  FP_CLASS_NEG_NORMAL = 1,
  FP_CLASS_NEG_SUBNORMAL = 2,
  FP_CLASS_NEG_ZERO = 3,
  FP_CLASS_POS_ZERO = 4,
  FP_CLASS_POS_SUBNORMAL = 5,
  FP_CLASS_POS_NORMAL = 6,
  FP_CLASS_POS_INF = 7,
  FP_CLASS_SNAN = 8,
  FP_CLASS_QNAN = 9
} FpClass;

typedef word_t (*FpRawRead)(CpuState* hart, uint8_t reg);
typedef void (*FpRawWrite)(CpuState* hart, uint8_t reg, word_t value);
typedef word_t (*FpRawUnaryOp)(word_t value);
typedef word_t (*FpRawBinaryOp)(word_t lhs, word_t rhs);
typedef word_t (*FpRawTernaryOp)(word_t lhs, word_t rhs, word_t third);
typedef bool (*FpRawCompareOp)(word_t lhs, word_t rhs);
typedef word_t (*FpRawToIntOp)(word_t value, uint_fast8_t rm, bool exact);
typedef word_t (*FpRawFromIntOp)(word_t value);
typedef word_t (*FpRawMinMaxOp)(word_t lhs, word_t rhs);
typedef word_t (*FpRawClassifyOp)(word_t value);

static inline word_t fp_low_mask(unsigned bits) {
  if (bits >= sizeof(word_t) * 8) return ~(word_t)0;
  return ((word_t)1 << bits) - 1;
}

static inline word_t fp_sign_bit(unsigned mant_bits, unsigned exp_bits) {
  return (word_t)1 << (mant_bits + exp_bits);
}

static inline word_t fp_exp_mask(unsigned mant_bits, unsigned exp_bits) {
  return fp_low_mask(exp_bits) << mant_bits;
}

static inline word_t fp_mant_mask(unsigned mant_bits) {
  return fp_low_mask(mant_bits);
}

static inline word_t fp_quiet_bit(unsigned mant_bits) {
  return (word_t)1 << (mant_bits - 1);
}

static inline word_t fp_canonical_nan(unsigned mant_bits, unsigned exp_bits) {
  return fp_exp_mask(mant_bits, exp_bits) | fp_quiet_bit(mant_bits);
}

#define FP32_SIGN_BIT fp_sign_bit(FP32_MANT_BITS, FP32_EXP_BITS)
#define FP64_SIGN_BIT fp_sign_bit(FP64_MANT_BITS, FP64_EXP_BITS)

static inline bool fp_enabled(const CpuState* hart) {
  return (hart->mstatus & MSTATUS_FS) != MSTATUS_FS_OFF;
}

static inline ExecResult fp_illegal(ExecContext* ctx) {
  return exec_trap(EXCP_ILLEGAL_INST, ctx->raw_instr);
}

static inline void fp_mark_dirty(CpuState* hart) {
  hart->mstatus = (hart->mstatus & ~MSTATUS_FS) | MSTATUS_FS_DIRTY;
}

static inline bool fp_set_rounding(ExecContext* ctx) {
  unsigned rm = (unsigned)ctx->operands.floating.rm;
  if (rm == (unsigned)kDyn) {
    rm = (unsigned)((ctx->hart->fcsr & FCSR_FRM) >> FCSR_FRM_SHIFT);
  }
  if (rm > (unsigned)kRmm) return false;

  softfloat_roundingMode = (uint_fast8_t)rm;
  softfloat_detectTininess = softfloat_tininess_afterRounding;
  return true;
}

static inline void fp_begin(void) { softfloat_exceptionFlags = 0; }

static inline void fp_finish(ExecContext* ctx, bool writes_fpr) {
  const uint_fast8_t flags =
      (uint_fast8_t)(softfloat_exceptionFlags & FCSR_FFLAGS);
  if (flags != 0) {
    ctx->hart->fcsr |= flags;
    fp_mark_dirty(ctx->hart);
  }
  if (writes_fpr) fp_mark_dirty(ctx->hart);
  softfloat_exceptionFlags = 0;
}

static inline word_t fp32_read(CpuState* hart, uint8_t rs) {
  const word_t value = hart->fpr[rs];
  if ((value >> FP32_BITS) != fp_low_mask(FP32_BITS)) {
    return fp_canonical_nan(FP32_MANT_BITS, FP32_EXP_BITS);
  }
  return (uint32_t)value;
}

static inline void fp32_write(CpuState* hart, uint8_t rd, word_t value) {
  hart->fpr[rd] = ~fp_low_mask(FP32_BITS) | (word_t)(uint32_t)value;
}

static inline word_t fp64_read(CpuState* hart, uint8_t rs) {
  return hart->fpr[rs];
}

static inline void fp64_write(CpuState* hart, uint8_t rd, word_t value) {
  hart->fpr[rd] = value;
}

static inline bool fp_is_nan(word_t bits, unsigned mant_bits,
                             unsigned exp_bits) {
  const word_t exp_mask = fp_exp_mask(mant_bits, exp_bits);
  return (bits & exp_mask) == exp_mask && (bits & fp_mant_mask(mant_bits)) != 0;
}

static inline bool fp_is_snan(word_t bits, unsigned mant_bits,
                              unsigned exp_bits) {
  return fp_is_nan(bits, mant_bits, exp_bits) &&
         (bits & fp_quiet_bit(mant_bits)) == 0;
}

static inline word_t fp_class_bit(FpClass cls) { return (word_t)1 << cls; }

static inline word_t fp_classify_bits(word_t bits, unsigned mant_bits,
                                      unsigned exp_bits) {
  const word_t exp_max = fp_low_mask(exp_bits);
  const bool sign = (bits & fp_sign_bit(mant_bits, exp_bits)) != 0;
  const word_t exponent = (bits >> mant_bits) & exp_max;
  const word_t fraction = bits & fp_mant_mask(mant_bits);

  if (exponent == exp_max) {
    if (fraction != 0) {
      const FpClass cls =
          fp_is_snan(bits, mant_bits, exp_bits) ? FP_CLASS_SNAN : FP_CLASS_QNAN;
      return fp_class_bit(cls);
    }
    return fp_class_bit(sign ? FP_CLASS_NEG_INF : FP_CLASS_POS_INF);
  }
  if (exponent == 0) {
    if (fraction != 0) {
      return fp_class_bit(sign ? FP_CLASS_NEG_SUBNORMAL
                               : FP_CLASS_POS_SUBNORMAL);
    }
    return fp_class_bit(sign ? FP_CLASS_NEG_ZERO : FP_CLASS_POS_ZERO);
  }
  return fp_class_bit(sign ? FP_CLASS_NEG_NORMAL : FP_CLASS_POS_NORMAL);
}

static inline word_t fp_minmax_bits(word_t lhs, word_t rhs, bool max,
                                    unsigned mant_bits, unsigned exp_bits,
                                    FpRawCompareOp less) {
  const word_t sign_bit = fp_sign_bit(mant_bits, exp_bits);
  const bool lhs_nan = fp_is_nan(lhs, mant_bits, exp_bits);
  const bool rhs_nan = fp_is_nan(rhs, mant_bits, exp_bits);

  if (fp_is_snan(lhs, mant_bits, exp_bits) ||
      fp_is_snan(rhs, mant_bits, exp_bits)) {
    softfloat_exceptionFlags |= softfloat_flag_invalid;
  }
  if (lhs_nan && rhs_nan) return fp_canonical_nan(mant_bits, exp_bits);
  if (lhs_nan) return rhs;
  if (rhs_nan) return lhs;

  /* RISC-V selects -0 for min and +0 for max. */
  if ((lhs & ~sign_bit) == 0 && (rhs & ~sign_bit) == 0) {
    return max ? lhs & rhs : lhs | rhs;
  }

  const bool lhs_less = less(lhs, rhs);
  return max ? (lhs_less ? rhs : lhs) : (lhs_less ? lhs : rhs);
}

static inline ExecResult fp_binary(ExecContext* ctx, FpRawRead read,
                                   FpRawWrite write, FpRawBinaryOp operation) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  if (!fp_set_rounding(ctx)) return fp_illegal(ctx);

  fp_begin();
  const word_t lhs = FLOAT_SRC1(ctx, read);
  const word_t rhs = FLOAT_SRC2(ctx, read);
  write(ctx->hart, FLOAT_RD(ctx), operation(lhs, rhs));
  fp_finish(ctx, true);
  return exec_ok();
}

static inline ExecResult fp_unary(ExecContext* ctx, FpRawRead read,
                                  FpRawWrite write, FpRawUnaryOp operation) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  if (!fp_set_rounding(ctx)) return fp_illegal(ctx);

  fp_begin();
  const word_t value = FLOAT_SRC1(ctx, read);
  write(ctx->hart, FLOAT_RD(ctx), operation(value));
  fp_finish(ctx, true);
  return exec_ok();
}

static inline ExecResult fp_fma(ExecContext* ctx, FpRawRead read,
                                FpRawWrite write, FpRawTernaryOp operation,
                                word_t sign_bit, bool negate_product,
                                bool negate_addend) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  if (!fp_set_rounding(ctx)) return fp_illegal(ctx);

  fp_begin();
  word_t lhs = FLOAT_SRC1(ctx, read);
  const word_t rhs = FLOAT_SRC2(ctx, read);
  word_t addend = FLOAT_SRC3(ctx, read);
  if (negate_product) lhs ^= sign_bit;
  if (negate_addend) addend ^= sign_bit;
  write(ctx->hart, FLOAT_RD(ctx), operation(lhs, rhs, addend));
  fp_finish(ctx, true);
  return exec_ok();
}

static inline ExecResult fp_compare(ExecContext* ctx, FpRawRead read,
                                    FpRawCompareOp operation) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);

  fp_begin();
  const word_t lhs = FLOAT_SRC1(ctx, read);
  const word_t rhs = FLOAT_SRC2(ctx, read);
  gpr_write(ctx->hart, FLOAT_RD(ctx), operation(lhs, rhs) ? 1 : 0);
  fp_finish(ctx, false);
  return exec_ok();
}

static inline ExecResult fp_to_int(ExecContext* ctx, FpRawRead read,
                                   FpRawToIntOp operation, bool word_result) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  if (!fp_set_rounding(ctx)) return fp_illegal(ctx);

  fp_begin();
  const word_t value =
      operation(FLOAT_SRC1(ctx, read), softfloat_roundingMode, true);
  word_t result = value;
  if (word_result) result = (word_t)(sword_t)(int32_t)(uint32_t)value;
  gpr_write(ctx->hart, FLOAT_RD(ctx), result);
  fp_finish(ctx, false);
  return exec_ok();
}

static inline ExecResult fp_from_int(ExecContext* ctx, FpRawWrite write,
                                     FpRawFromIntOp operation) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  if (!fp_set_rounding(ctx)) return fp_illegal(ctx);

  fp_begin();
  const word_t value = gpr_read(ctx->hart, FLOAT_RS1(ctx));
  write(ctx->hart, FLOAT_RD(ctx), operation(value));
  fp_finish(ctx, true);
  return exec_ok();
}

static inline ExecResult fp_minmax(ExecContext* ctx, FpRawRead read,
                                   FpRawWrite write, FpRawMinMaxOp operation) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);

  fp_begin();
  const word_t lhs = FLOAT_SRC1(ctx, read);
  const word_t rhs = FLOAT_SRC2(ctx, read);
  write(ctx->hart, FLOAT_RD(ctx), operation(lhs, rhs));
  fp_finish(ctx, true);
  return exec_ok();
}

static inline ExecResult fp_classify(ExecContext* ctx, FpRawRead read,
                                     FpRawClassifyOp operation) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);

  const word_t value = FLOAT_SRC1(ctx, read);
  gpr_write(ctx->hart, FLOAT_RD(ctx), operation(value));
  return exec_ok();
}

static inline word_t fp_sign_inject(word_t lhs, word_t rhs, word_t sign_bit,
                                    bool negate, bool xor_sign) {
  const word_t selected = xor_sign ? lhs ^ rhs : negate ? ~rhs : rhs;
  return (lhs & ~sign_bit) | (selected & sign_bit);
}

static inline ExecResult fp_fmv_to_gpr(ExecContext* ctx, FpRawRead read) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  gpr_write(ctx->hart, FLOAT_RD(ctx), read(ctx->hart, FLOAT_RS1(ctx)));
  return exec_ok();
}

static inline ExecResult fp_fmv_from_gpr(ExecContext* ctx, FpRawWrite write) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  write(ctx->hart, FLOAT_RD(ctx), gpr_read(ctx->hart, FLOAT_RS1(ctx)));
  fp_mark_dirty(ctx->hart);
  return exec_ok();
}

static inline ExecResult fp_sign_inject_exec(ExecContext* ctx, FpRawRead read,
                                             FpRawWrite write, word_t sign_bit,
                                             bool negate, bool xor_sign) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  const word_t lhs = read(ctx->hart, FLOAT_RS1(ctx));
  const word_t rhs = read(ctx->hart, FLOAT_RS2(ctx));
  write(ctx->hart, FLOAT_RD(ctx),
        fp_sign_inject(lhs, rhs, sign_bit, negate, xor_sign));
  fp_mark_dirty(ctx->hart);
  return exec_ok();
}

#endif /* RVISS_RISCV64_FP_EXEC_COMMON_H */
