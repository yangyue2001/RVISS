#include "../fp/exec_common.h"

/* F stores single-precision values in NaN-boxed 64-bit FPRs. */
#define FP32_CANONICAL_NAN UINT32_C(0x7fc00000)
#define FP32_BOX_MASK UINT64_C(0xffffffff00000000)

static word_t fp32_read_bits(ExecContext* ctx, unsigned reg) {
  const word_t value = fpr_read(ctx->hart, (uint8_t)reg);
  return (value >> 32) == UINT32_MAX ? (uint32_t)value : FP32_CANONICAL_NAN;
}

static void fp32_write_bits(ExecContext* ctx, unsigned reg, word_t value) {
  fpr_write(ctx->hart, (uint8_t)reg, FP32_BOX_MASK | (uint32_t)value);
}

#ifdef CONFIG_RV_F
static bool fp32_is_nan(uint32_t bits) {
  return (bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
         (bits & UINT32_C(0x007fffff)) != 0;
}

static bool fp32_is_snan(uint32_t bits) {
  return fp32_is_nan(bits) && (bits & UINT32_C(0x00400000)) == 0;
}

static word_t fp32_classify(word_t value) {
  const uint32_t bits = (uint32_t)value;
  const bool sign = (bits >> 31) != 0;
  const uint32_t exponent = (bits >> 23) & UINT32_C(0xff);
  const uint32_t fraction = bits & UINT32_C(0x007fffff);

  if (exponent == UINT32_C(0xff)) {
    if (fraction != 0) return UINT32_C(1) << (fp32_is_snan(bits) ? 8 : 9);
    return UINT32_C(1) << (sign ? 0 : 7);
  }
  if (exponent == 0) {
    if (fraction != 0) return UINT32_C(1) << (sign ? 2 : 5);
    return UINT32_C(1) << (sign ? 3 : 4);
  }
  return UINT32_C(1) << (sign ? 1 : 6);
}

static word_t fp32_minmax_bits(word_t lhs, word_t rhs, bool max) {
  const uint32_t a = (uint32_t)lhs;
  const uint32_t b = (uint32_t)rhs;
  const bool a_nan = fp32_is_nan(a);
  const bool b_nan = fp32_is_nan(b);

  if (fp32_is_snan(a) || fp32_is_snan(b))
    softfloat_exceptionFlags |= softfloat_flag_invalid;

  if (a_nan && b_nan) return FP32_CANONICAL_NAN;
  if (a_nan) return b;
  if (b_nan) return a;

  /* RISC-V selects -0 for min and +0 for max. */
  if ((a & UINT32_C(0x7fffffff)) == 0 &&
      (b & UINT32_C(0x7fffffff)) == 0)
    return max ? a & b : a | b;

  const bool less = f32_lt_quiet((float32_t){a}, (float32_t){b});
  return max ? (less ? b : a) : (less ? a : b);
}

static word_t fp32_min_bits(word_t lhs, word_t rhs) {
  return fp32_minmax_bits(lhs, rhs, false);
}

static word_t fp32_max_bits(word_t lhs, word_t rhs) {
  return fp32_minmax_bits(lhs, rhs, true);
}

static word_t fp32_add_bits(word_t lhs, word_t rhs) {
  return f32_add((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs}).v;
}

static word_t fp32_sub_bits(word_t lhs, word_t rhs) {
  return f32_sub((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs}).v;
}

static word_t fp32_mul_bits(word_t lhs, word_t rhs) {
  return f32_mul((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs}).v;
}

static word_t fp32_div_bits(word_t lhs, word_t rhs) {
  return f32_div((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs}).v;
}

static word_t fp32_sqrt_bits(word_t value) {
  return f32_sqrt((float32_t){(uint32_t)value}).v;
}

static word_t fp32_fma_bits(word_t lhs, word_t rhs, word_t third) {
  return f32_mulAdd((float32_t){(uint32_t)lhs},
                    (float32_t){(uint32_t)rhs},
                    (float32_t){(uint32_t)third})
      .v;
}

static bool fp32_eq_bits(word_t lhs, word_t rhs) {
  return f32_eq((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs});
}

static bool fp32_lt_bits(word_t lhs, word_t rhs) {
  return f32_lt((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs});
}

static bool fp32_le_bits(word_t lhs, word_t rhs) {
  return f32_le((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs});
}

static word_t fp32_to_i32_bits(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(int_fast32_t)f32_to_i32((float32_t){(uint32_t)value}, rm,
                                          exact);
}

static word_t fp32_to_ui32_bits(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(uint_fast32_t)f32_to_ui32((float32_t){(uint32_t)value}, rm,
                                            exact);
}

static word_t fp32_to_i64_bits(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(int_fast64_t)f32_to_i64((float32_t){(uint32_t)value}, rm,
                                          exact);
}

static word_t fp32_to_ui64_bits(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(uint_fast64_t)f32_to_ui64((float32_t){(uint32_t)value}, rm,
                                            exact);
}

static word_t fp32_from_i32_bits(word_t value) {
  return i32_to_f32((int32_t)value).v;
}

static word_t fp32_from_ui32_bits(word_t value) {
  return ui32_to_f32((uint32_t)value).v;
}

static word_t fp32_from_i64_bits(word_t value) {
  return i64_to_f32((int64_t)value).v;
}

static word_t fp32_from_ui64_bits(word_t value) {
  return ui64_to_f32(value).v;
}

static ExecResult exec_fp32_sign_inject(ExecContext* ctx, bool negate,
                                        bool xor_sign) {
  if (!fp_common_enabled(ctx->hart)) return fp_common_illegal(ctx);
  const word_t lhs = fp32_read_bits(ctx, fp_common_rs1(ctx));
  const word_t rhs = fp32_read_bits(ctx, fp_common_rs2(ctx));
  fp32_write_bits(ctx, fp_common_rd(ctx),
                  fp_common_sign_inject(lhs, rhs, UINT32_C(1) << 31, negate,
                                        xor_sign));
  fp_common_mark_dirty(ctx->hart);
  return exec_ok();
}

static ExecResult exec_fmv_x_w_impl(ExecContext* ctx) {
  if (!fp_common_enabled(ctx->hart)) return fp_common_illegal(ctx);
  /* FMV.X.W is a bit move: it copies the low word even if the FPR is not
   * currently a valid NaN-boxed single. */
  const uint32_t value =
      (uint32_t)fpr_read(ctx->hart, (uint8_t)fp_common_rs1(ctx));
  gpr_write(ctx->hart, (uint8_t)fp_common_rd(ctx),
            (word_t)(sword_t)(int32_t)value);
  return exec_ok();
}

static ExecResult exec_fmv_w_x_impl(ExecContext* ctx) {
  if (!fp_common_enabled(ctx->hart)) return fp_common_illegal(ctx);
  fp32_write_bits(ctx, fp_common_rd(ctx),
                  gpr_read(ctx->hart, (uint8_t)fp_common_rs1(ctx)));
  fp_common_mark_dirty(ctx->hart);
  return exec_ok();
}
#endif /* CONFIG_RV_F */

/* Load/store are intentionally placeholders for this slice. */
ExecResult exec_flw(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}

ExecResult exec_fsw(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}

#define FP_CALL(_ctx, _call) (_call)

ExecResult exec_fmadd_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_fma(ctx, fp32_read_bits, fp32_write_bits,
                                    fp32_fma_bits, UINT32_C(1) << 31, false,
                                    false));
}

ExecResult exec_fmsub_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_fma(ctx, fp32_read_bits, fp32_write_bits,
                                    fp32_fma_bits, UINT32_C(1) << 31, false,
                                    true));
}

ExecResult exec_fnmsub_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_fma(ctx, fp32_read_bits, fp32_write_bits,
                                    fp32_fma_bits, UINT32_C(1) << 31, true,
                                    true));
}

ExecResult exec_fnmadd_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_fma(ctx, fp32_read_bits, fp32_write_bits,
                                    fp32_fma_bits, UINT32_C(1) << 31, true,
                                    false));
}

ExecResult exec_fadd_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_binary(ctx, fp32_read_bits, fp32_write_bits,
                                       fp32_add_bits));
}

ExecResult exec_fsub_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_binary(ctx, fp32_read_bits, fp32_write_bits,
                                       fp32_sub_bits));
}

ExecResult exec_fmul_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_binary(ctx, fp32_read_bits, fp32_write_bits,
                                       fp32_mul_bits));
}

ExecResult exec_fdiv_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_binary(ctx, fp32_read_bits, fp32_write_bits,
                                       fp32_div_bits));
}

ExecResult exec_fsqrt_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_unary(ctx, fp32_read_bits, fp32_write_bits,
                                      fp32_sqrt_bits));
}

ExecResult exec_fclass_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_classify(ctx, fp32_read_bits, fp32_classify));
}

ExecResult exec_fcvt_l_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_to_int(ctx, fp32_read_bits, fp32_to_i64_bits,
                                       false));
}

ExecResult exec_fcvt_lu_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_to_int(ctx, fp32_read_bits, fp32_to_ui64_bits,
                                       false));
}

ExecResult exec_fcvt_s_l(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_from_int(ctx, fp32_write_bits,
                                         fp32_from_i64_bits));
}

ExecResult exec_fcvt_s_lu(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_from_int(ctx, fp32_write_bits,
                                         fp32_from_ui64_bits));
}

ExecResult exec_fcvt_s_w(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_from_int(ctx, fp32_write_bits,
                                         fp32_from_i32_bits));
}

ExecResult exec_fcvt_s_wu(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_from_int(ctx, fp32_write_bits,
                                         fp32_from_ui32_bits));
}

ExecResult exec_fcvt_w_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_to_int(ctx, fp32_read_bits, fp32_to_i32_bits,
                                       true));
}

ExecResult exec_fcvt_wu_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_to_int(ctx, fp32_read_bits, fp32_to_ui32_bits,
                                       true));
}

ExecResult exec_feq_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_compare(ctx, fp32_read_bits, fp32_eq_bits));
}

ExecResult exec_fle_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_compare(ctx, fp32_read_bits, fp32_le_bits));
}

ExecResult exec_flt_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_compare(ctx, fp32_read_bits, fp32_lt_bits));
}

ExecResult exec_fmax_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_minmax(ctx, fp32_read_bits, fp32_write_bits,
                                       fp32_max_bits));
}

ExecResult exec_fmin_s(ExecContext* ctx) {
  return FP_CALL(ctx, fp_common_minmax(ctx, fp32_read_bits, fp32_write_bits,
                                       fp32_min_bits));
}

ExecResult exec_fmv_w_x(ExecContext* ctx) {
  return FP_CALL(ctx, exec_fmv_w_x_impl(ctx));
}

ExecResult exec_fmv_x_w(ExecContext* ctx) {
  return FP_CALL(ctx, exec_fmv_x_w_impl(ctx));
}

ExecResult exec_fsgnj_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_fp32_sign_inject(ctx, false, false));
}

ExecResult exec_fsgnjn_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_fp32_sign_inject(ctx, true, false));
}

ExecResult exec_fsgnjx_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_fp32_sign_inject(ctx, false, true));
}

#undef FP_CALL
