#include "../fp/exec_common.h"

static word_t fp64_classify(word_t value) {
  return fp_classify_bits(value, FP64_MANT_BITS, FP64_EXP_BITS);
}

static bool fp64_less(word_t lhs, word_t rhs) {
  return f64_lt_quiet((float64_t){lhs}, (float64_t){rhs});
}

static word_t fp64_min(word_t lhs, word_t rhs) {
  return fp_minmax_bits(lhs, rhs, false, FP64_MANT_BITS, FP64_EXP_BITS,
                        fp64_less);
}

static word_t fp64_max(word_t lhs, word_t rhs) {
  return fp_minmax_bits(lhs, rhs, true, FP64_MANT_BITS, FP64_EXP_BITS,
                        fp64_less);
}

static word_t fp64_add(word_t lhs, word_t rhs) {
  return f64_add((float64_t){lhs}, (float64_t){rhs}).v;
}

static word_t fp64_sub(word_t lhs, word_t rhs) {
  return f64_sub((float64_t){lhs}, (float64_t){rhs}).v;
}

static word_t fp64_mul(word_t lhs, word_t rhs) {
  return f64_mul((float64_t){lhs}, (float64_t){rhs}).v;
}

static word_t fp64_div(word_t lhs, word_t rhs) {
  return f64_div((float64_t){lhs}, (float64_t){rhs}).v;
}

static word_t fp64_sqrt(word_t value) { return f64_sqrt((float64_t){value}).v; }

static word_t fp64_fma(word_t lhs, word_t rhs, word_t addend) {
  return f64_mulAdd((float64_t){lhs}, (float64_t){rhs}, (float64_t){addend}).v;
}

static bool fp64_eq(word_t lhs, word_t rhs) {
  return f64_eq((float64_t){lhs}, (float64_t){rhs});
}

static bool fp64_lt(word_t lhs, word_t rhs) {
  return f64_lt((float64_t){lhs}, (float64_t){rhs});
}

static bool fp64_le(word_t lhs, word_t rhs) {
  return f64_le((float64_t){lhs}, (float64_t){rhs});
}

static word_t fp64_to_i32(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(int_fast32_t)f64_to_i32((float64_t){value}, rm, exact);
}

static word_t fp64_to_ui32(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(uint_fast32_t)f64_to_ui32((float64_t){value}, rm, exact);
}

static word_t fp64_to_i64(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(int_fast64_t)f64_to_i64((float64_t){value}, rm, exact);
}

static word_t fp64_to_ui64(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(uint_fast64_t)f64_to_ui64((float64_t){value}, rm, exact);
}

static word_t fp64_from_i32(word_t value) {
  return i32_to_f64((int32_t)value).v;
}

static word_t fp64_from_ui32(word_t value) {
  return ui32_to_f64((uint32_t)value).v;
}

static word_t fp64_from_i64(word_t value) {
  return i64_to_f64((int64_t)value).v;
}

static word_t fp64_from_ui64(word_t value) { return ui64_to_f64(value).v; }

static word_t fp64_from_s(word_t value) {
  return f32_to_f64((float32_t){(uint32_t)value}).v;
}

static word_t fp64_to_s(word_t value) {
  return f64_to_f32((float64_t){value}).v;
}

/* Load/store stay placeholders until the new memory operand path exists. */
ExecResult exec_fld(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}

ExecResult exec_fsd(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}

ExecResult exec_fmadd_d(ExecContext* ctx) {
  return fp_fma(ctx, fp64_read, fp64_write, fp64_fma, FP64_SIGN_BIT, false,
                false);
}

ExecResult exec_fmsub_d(ExecContext* ctx) {
  return fp_fma(ctx, fp64_read, fp64_write, fp64_fma, FP64_SIGN_BIT, false,
                true);
}

ExecResult exec_fnmsub_d(ExecContext* ctx) {
  return fp_fma(ctx, fp64_read, fp64_write, fp64_fma, FP64_SIGN_BIT, true,
                false);
}

ExecResult exec_fnmadd_d(ExecContext* ctx) {
  return fp_fma(ctx, fp64_read, fp64_write, fp64_fma, FP64_SIGN_BIT, true,
                true);
}

ExecResult exec_fadd_d(ExecContext* ctx) {
  return fp_binary(ctx, fp64_read, fp64_write, fp64_add);
}

ExecResult exec_fsub_d(ExecContext* ctx) {
  return fp_binary(ctx, fp64_read, fp64_write, fp64_sub);
}

ExecResult exec_fmul_d(ExecContext* ctx) {
  return fp_binary(ctx, fp64_read, fp64_write, fp64_mul);
}

ExecResult exec_fdiv_d(ExecContext* ctx) {
  return fp_binary(ctx, fp64_read, fp64_write, fp64_div);
}

ExecResult exec_fsqrt_d(ExecContext* ctx) {
  return fp_unary(ctx, fp64_read, fp64_write, fp64_sqrt);
}

ExecResult exec_fclass_d(ExecContext* ctx) {
  return fp_classify(ctx, fp64_read, fp64_classify);
}

ExecResult exec_fcvt_l_d(ExecContext* ctx) {
  return fp_to_int(ctx, fp64_read, fp64_to_i64, false);
}

ExecResult exec_fcvt_lu_d(ExecContext* ctx) {
  return fp_to_int(ctx, fp64_read, fp64_to_ui64, false);
}

ExecResult exec_fcvt_w_d(ExecContext* ctx) {
  return fp_to_int(ctx, fp64_read, fp64_to_i32, true);
}

ExecResult exec_fcvt_wu_d(ExecContext* ctx) {
  return fp_to_int(ctx, fp64_read, fp64_to_ui32, true);
}

ExecResult exec_fcvt_d_l(ExecContext* ctx) {
  return fp_from_int(ctx, fp64_write, fp64_from_i64);
}

ExecResult exec_fcvt_d_lu(ExecContext* ctx) {
  return fp_from_int(ctx, fp64_write, fp64_from_ui64);
}

ExecResult exec_fcvt_d_w(ExecContext* ctx) {
  return fp_from_int(ctx, fp64_write, fp64_from_i32);
}

ExecResult exec_fcvt_d_wu(ExecContext* ctx) {
  return fp_from_int(ctx, fp64_write, fp64_from_ui32);
}

ExecResult exec_fcvt_s_d(ExecContext* ctx) {
  return fp_unary(ctx, fp64_read, fp32_write, fp64_to_s);
}

ExecResult exec_fcvt_d_s(ExecContext* ctx) {
  return fp_unary(ctx, fp32_read, fp64_write, fp64_from_s);
}

ExecResult exec_feq_d(ExecContext* ctx) {
  return fp_compare(ctx, fp64_read, fp64_eq);
}

ExecResult exec_fle_d(ExecContext* ctx) {
  return fp_compare(ctx, fp64_read, fp64_le);
}

ExecResult exec_flt_d(ExecContext* ctx) {
  return fp_compare(ctx, fp64_read, fp64_lt);
}

ExecResult exec_fmax_d(ExecContext* ctx) {
  return fp_minmax(ctx, fp64_read, fp64_write, fp64_max);
}

ExecResult exec_fmin_d(ExecContext* ctx) {
  return fp_minmax(ctx, fp64_read, fp64_write, fp64_min);
}

ExecResult exec_fmv_d_x(ExecContext* ctx) {
  return fp_fmv_from_gpr(ctx, fp64_write);
}

ExecResult exec_fmv_x_d(ExecContext* ctx) {
  return fp_fmv_to_gpr(ctx, fp64_read);
}

ExecResult exec_fsgnj_d(ExecContext* ctx) {
  return fp_sign_inject_exec(ctx, fp64_read, fp64_write, FP64_SIGN_BIT, false,
                             false);
}

ExecResult exec_fsgnjn_d(ExecContext* ctx) {
  return fp_sign_inject_exec(ctx, fp64_read, fp64_write, FP64_SIGN_BIT, true,
                             false);
}

ExecResult exec_fsgnjx_d(ExecContext* ctx) {
  return fp_sign_inject_exec(ctx, fp64_read, fp64_write, FP64_SIGN_BIT, false,
                             true);
}
