#include "../fp/exec_common.h"

static word_t fp64_read_bits(ExecContext* ctx, unsigned reg) {
  return fpr_read(ctx->hart, (uint8_t)reg);
}

static void fp64_write_bits(ExecContext* ctx, unsigned reg, word_t value) {
  fpr_write(ctx->hart, (uint8_t)reg, value);
}

#ifdef CONFIG_RV_D
static word_t fp64_read_s_bits(ExecContext* ctx, unsigned reg) {
  const word_t value = fpr_read(ctx->hart, (uint8_t)reg);
  return (value >> 32) == UINT32_MAX ? (uint32_t)value : UINT32_C(0x7fc00000);
}

static void fp64_write_s_bits(ExecContext* ctx, unsigned reg, word_t value) {
  fpr_write(ctx->hart, (uint8_t)reg,
            UINT64_C(0xffffffff00000000) | (uint32_t)value);
}

static bool fp64_is_nan(uint64_t bits) {
  return (bits & UINT64_C(0x7ff0000000000000)) ==
             UINT64_C(0x7ff0000000000000) &&
         (bits & UINT64_C(0x000fffffffffffff)) != 0;
}

static bool fp64_is_snan(uint64_t bits) {
  return fp64_is_nan(bits) &&
         (bits & UINT64_C(0x0008000000000000)) == 0;
}

static word_t fp64_classify(word_t value) {
  const uint64_t bits = value;
  const bool sign = (bits >> 63) != 0;
  const uint64_t exponent = (bits >> 52) & UINT64_C(0x7ff);
  const uint64_t fraction = bits & UINT64_C(0x000fffffffffffff);

  if (exponent == UINT64_C(0x7ff)) {
    if (fraction != 0) return UINT64_C(1) << (fp64_is_snan(bits) ? 8 : 9);
    return UINT64_C(1) << (sign ? 0 : 7);
  }
  if (exponent == 0) {
    if (fraction != 0) return UINT64_C(1) << (sign ? 2 : 5);
    return UINT64_C(1) << (sign ? 3 : 4);
  }
  return UINT64_C(1) << (sign ? 1 : 6);
}

static word_t fp64_minmax_bits(word_t lhs, word_t rhs, bool max) {
  const uint64_t a = lhs;
  const uint64_t b = rhs;
  const bool a_nan = fp64_is_nan(a);
  const bool b_nan = fp64_is_nan(b);

  if (fp64_is_snan(a) || fp64_is_snan(b))
    softfloat_exceptionFlags |= softfloat_flag_invalid;

  if (a_nan && b_nan) return UINT64_C(0x7ff8000000000000);
  if (a_nan) return b;
  if (b_nan) return a;

  /* RISC-V selects -0 for min and +0 for max. */
  if ((a & UINT64_C(0x7fffffffffffffff)) == 0 &&
      (b & UINT64_C(0x7fffffffffffffff)) == 0)
    return max ? a & b : a | b;

  const bool less = f64_lt_quiet((float64_t){a}, (float64_t){b});
  return max ? (less ? b : a) : (less ? a : b);
}

static word_t fp64_min_bits(word_t lhs, word_t rhs) {
  return fp64_minmax_bits(lhs, rhs, false);
}

static word_t fp64_max_bits(word_t lhs, word_t rhs) {
  return fp64_minmax_bits(lhs, rhs, true);
}

static word_t fp64_add_bits(word_t lhs, word_t rhs) {
  return f64_add((float64_t){lhs}, (float64_t){rhs}).v;
}

static word_t fp64_sub_bits(word_t lhs, word_t rhs) {
  return f64_sub((float64_t){lhs}, (float64_t){rhs}).v;
}

static word_t fp64_mul_bits(word_t lhs, word_t rhs) {
  return f64_mul((float64_t){lhs}, (float64_t){rhs}).v;
}

static word_t fp64_div_bits(word_t lhs, word_t rhs) {
  return f64_div((float64_t){lhs}, (float64_t){rhs}).v;
}

static word_t fp64_sqrt_bits(word_t value) {
  return f64_sqrt((float64_t){value}).v;
}

static word_t fp64_fma_bits(word_t lhs, word_t rhs, word_t third) {
  return f64_mulAdd((float64_t){lhs}, (float64_t){rhs},
                    (float64_t){third})
      .v;
}

static bool fp64_eq_bits(word_t lhs, word_t rhs) {
  return f64_eq((float64_t){lhs}, (float64_t){rhs});
}

static bool fp64_lt_bits(word_t lhs, word_t rhs) {
  return f64_lt((float64_t){lhs}, (float64_t){rhs});
}

static bool fp64_le_bits(word_t lhs, word_t rhs) {
  return f64_le((float64_t){lhs}, (float64_t){rhs});
}

static word_t fp64_to_i32_bits(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(int_fast32_t)f64_to_i32((float64_t){value}, rm, exact);
}

static word_t fp64_to_ui32_bits(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(uint_fast32_t)f64_to_ui32((float64_t){value}, rm, exact);
}

static word_t fp64_to_i64_bits(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(int_fast64_t)f64_to_i64((float64_t){value}, rm, exact);
}

static word_t fp64_to_ui64_bits(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(uint_fast64_t)f64_to_ui64((float64_t){value}, rm, exact);
}

static word_t fp64_from_i32_bits(word_t value) {
  return i32_to_f64((int32_t)value).v;
}

static word_t fp64_from_ui32_bits(word_t value) {
  return ui32_to_f64((uint32_t)value).v;
}

static word_t fp64_from_i64_bits(word_t value) {
  return i64_to_f64((int64_t)value).v;
}

static word_t fp64_from_ui64_bits(word_t value) {
  return ui64_to_f64(value).v;
}

static word_t fp64_from_s_bits(word_t value) {
  return f32_to_f64((float32_t){(uint32_t)value}).v;
}

static word_t fp64_to_s_bits(word_t value) {
  return f64_to_f32((float64_t){value}).v;
}

static ExecResult exec_fp64_sign_inject(ExecContext* ctx, bool negate,
                                        bool xor_sign) {
  if (!fp_common_enabled(ctx->hart)) return fp_common_illegal(ctx);
  const word_t lhs = fp64_read_bits(ctx, fp_common_rs1(ctx));
  const word_t rhs = fp64_read_bits(ctx, fp_common_rs2(ctx));
  fp64_write_bits(ctx, fp_common_rd(ctx),
                  fp_common_sign_inject(lhs, rhs, UINT64_C(1) << 63, negate,
                                        xor_sign));
  fp_common_mark_dirty(ctx->hart);
  return exec_ok();
}

static ExecResult exec_fmv_x_d_impl(ExecContext* ctx) {
  if (!fp_common_enabled(ctx->hart)) return fp_common_illegal(ctx);
  gpr_write(ctx->hart, (uint8_t)fp_common_rd(ctx),
            fp64_read_bits(ctx, fp_common_rs1(ctx)));
  return exec_ok();
}

static ExecResult exec_fmv_d_x_impl(ExecContext* ctx) {
  if (!fp_common_enabled(ctx->hart)) return fp_common_illegal(ctx);
  fp64_write_bits(ctx, fp_common_rd(ctx),
                  gpr_read(ctx->hart, (uint8_t)fp_common_rs1(ctx)));
  fp_common_mark_dirty(ctx->hart);
  return exec_ok();
}
#endif /* CONFIG_RV_D */

/* Load/store are intentionally placeholders for this slice. */
ExecResult exec_fld(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}

ExecResult exec_fsd(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}

#define DP_CALL(_ctx, _call) (_call)

ExecResult exec_fmadd_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_fma(ctx, fp64_read_bits, fp64_write_bits,
                                    fp64_fma_bits, UINT64_C(1) << 63, false,
                                    false));
}

ExecResult exec_fmsub_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_fma(ctx, fp64_read_bits, fp64_write_bits,
                                    fp64_fma_bits, UINT64_C(1) << 63, false,
                                    true));
}

ExecResult exec_fnmsub_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_fma(ctx, fp64_read_bits, fp64_write_bits,
                                    fp64_fma_bits, UINT64_C(1) << 63, true,
                                    true));
}

ExecResult exec_fnmadd_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_fma(ctx, fp64_read_bits, fp64_write_bits,
                                    fp64_fma_bits, UINT64_C(1) << 63, true,
                                    false));
}

ExecResult exec_fadd_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_binary(ctx, fp64_read_bits, fp64_write_bits,
                                       fp64_add_bits));
}

ExecResult exec_fsub_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_binary(ctx, fp64_read_bits, fp64_write_bits,
                                       fp64_sub_bits));
}

ExecResult exec_fmul_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_binary(ctx, fp64_read_bits, fp64_write_bits,
                                       fp64_mul_bits));
}

ExecResult exec_fdiv_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_binary(ctx, fp64_read_bits, fp64_write_bits,
                                       fp64_div_bits));
}

ExecResult exec_fsqrt_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_unary(ctx, fp64_read_bits, fp64_write_bits,
                                      fp64_sqrt_bits));
}

ExecResult exec_fclass_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_classify(ctx, fp64_read_bits, fp64_classify));
}

ExecResult exec_fcvt_l_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_to_int(ctx, fp64_read_bits, fp64_to_i64_bits,
                                       false));
}

ExecResult exec_fcvt_lu_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_to_int(ctx, fp64_read_bits, fp64_to_ui64_bits,
                                       false));
}

ExecResult exec_fcvt_w_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_to_int(ctx, fp64_read_bits, fp64_to_i32_bits,
                                       true));
}

ExecResult exec_fcvt_wu_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_to_int(ctx, fp64_read_bits, fp64_to_ui32_bits,
                                       true));
}

ExecResult exec_fcvt_d_l(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_from_int(ctx, fp64_write_bits,
                                         fp64_from_i64_bits));
}

ExecResult exec_fcvt_d_lu(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_from_int(ctx, fp64_write_bits,
                                         fp64_from_ui64_bits));
}

ExecResult exec_fcvt_d_w(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_from_int(ctx, fp64_write_bits,
                                         fp64_from_i32_bits));
}

ExecResult exec_fcvt_d_wu(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_from_int(ctx, fp64_write_bits,
                                         fp64_from_ui32_bits));
}

ExecResult exec_fcvt_s_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_unary(ctx, fp64_read_bits, fp64_write_s_bits,
                                      fp64_to_s_bits));
}

ExecResult exec_fcvt_d_s(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_unary(ctx, fp64_read_s_bits, fp64_write_bits,
                                      fp64_from_s_bits));
}

ExecResult exec_feq_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_compare(ctx, fp64_read_bits, fp64_eq_bits));
}

ExecResult exec_fle_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_compare(ctx, fp64_read_bits, fp64_le_bits));
}

ExecResult exec_flt_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_compare(ctx, fp64_read_bits, fp64_lt_bits));
}

ExecResult exec_fmax_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_minmax(ctx, fp64_read_bits, fp64_write_bits,
                                       fp64_max_bits));
}

ExecResult exec_fmin_d(ExecContext* ctx) {
  return DP_CALL(ctx, fp_common_minmax(ctx, fp64_read_bits, fp64_write_bits,
                                       fp64_min_bits));
}

ExecResult exec_fmv_d_x(ExecContext* ctx) {
  return DP_CALL(ctx, exec_fmv_d_x_impl(ctx));
}

ExecResult exec_fmv_x_d(ExecContext* ctx) {
  return DP_CALL(ctx, exec_fmv_x_d_impl(ctx));
}

ExecResult exec_fsgnj_d(ExecContext* ctx) {
  return DP_CALL(ctx, exec_fp64_sign_inject(ctx, false, false));
}

ExecResult exec_fsgnjn_d(ExecContext* ctx) {
  return DP_CALL(ctx, exec_fp64_sign_inject(ctx, true, false));
}

ExecResult exec_fsgnjx_d(ExecContext* ctx) {
  return DP_CALL(ctx, exec_fp64_sign_inject(ctx, false, true));
}

#undef DP_CALL
