#include <cpu/exec.h>

#ifdef CONFIG_RV_F
#include "platform.h"
#include "softfloat.h"
#endif

/* F stores single-precision values in NaN-boxed 64-bit FPRs. */
#define FP32_CANONICAL_NAN UINT32_C(0x7fc00000)
#define FP32_BOX_MASK UINT64_C(0xffffffff00000000)

static inline uint32_t fp_raw(ExecContext* ctx) { return ctx->raw_instr; }

static inline unsigned fp_rd(ExecContext* ctx) {
  return (fp_raw(ctx) >> 7) & 0x1f;
}

static inline unsigned fp_rs1(ExecContext* ctx) {
  return (fp_raw(ctx) >> 15) & 0x1f;
}

static inline unsigned fp_rs2(ExecContext* ctx) {
  return (fp_raw(ctx) >> 20) & 0x1f;
}

static inline unsigned fp_rs3(ExecContext* ctx) {
  return (fp_raw(ctx) >> 27) & 0x1f;
}

static inline unsigned fp_rm(ExecContext* ctx) {
  return (fp_raw(ctx) >> 12) & 0x7;
}

static inline word_t fp_gpr_read(ExecContext* ctx, unsigned reg) {
  return gpr_read(ctx->hart, (uint8_t)reg);
}

static inline word_t fp_read_s(ExecContext* ctx, unsigned reg) {
  const word_t value = fpr_read(ctx->hart, (uint8_t)reg);
  return (value >> 32) == UINT32_MAX ? (uint32_t)value
                                     : FP32_CANONICAL_NAN;
}

static inline void fp_write_s(ExecContext* ctx, unsigned reg, uint32_t value) {
  fpr_write(ctx->hart, (uint8_t)reg, FP32_BOX_MASK | value);
}

static inline void fp_mark_dirty(CpuState* hart) {
  hart->mstatus = (hart->mstatus & ~MSTATUS_FS) | MSTATUS_FS_DIRTY;
}

static inline bool fp_enabled(const CpuState* hart) {
  return (hart->mstatus & MSTATUS_FS) != MSTATUS_FS_OFF;
}

static inline ExecResult fp_illegal(ExecContext* ctx) {
  return exec_trap(EXCP_ILLEGAL_INST, fp_raw(ctx));
}

#ifdef CONFIG_RV_F
static bool fp_set_rounding(ExecContext* ctx, unsigned rm) {
  if (rm == 7) rm = (ctx->hart->fcsr >> 5) & 0x7;
  if (rm > 4) return false;

  softfloat_roundingMode = (uint_fast8_t)rm;
  softfloat_detectTininess = softfloat_tininess_afterRounding;
  return true;
}

static inline void fp_begin(void) { softfloat_exceptionFlags = 0; }

static inline void fp_finish(ExecContext* ctx, bool writes_fpr) {
  const uint_fast8_t flags = softfloat_exceptionFlags & 0x1f;
  if (flags != 0) {
    ctx->hart->fcsr |= flags;
    fp_mark_dirty(ctx->hart);
  }
  if (writes_fpr) fp_mark_dirty(ctx->hart);
  softfloat_exceptionFlags = 0;
}

static bool fp_is_nan(uint32_t bits) {
  return (bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
         (bits & UINT32_C(0x007fffff)) != 0;
}

static bool fp_is_snan(uint32_t bits) {
  return fp_is_nan(bits) && (bits & UINT32_C(0x00400000)) == 0;
}

static uint32_t fp_classify(uint32_t bits) {
  const bool sign = (bits >> 31) != 0;
  const uint32_t exponent = (bits >> 23) & UINT32_C(0xff);
  const uint32_t fraction = bits & UINT32_C(0x007fffff);

  if (exponent == UINT32_C(0xff)) {
    if (fraction != 0) return UINT32_C(1) << (fp_is_snan(bits) ? 8 : 9);
    return UINT32_C(1) << (sign ? 0 : 7);
  }
  if (exponent == 0) {
    if (fraction != 0) return UINT32_C(1) << (sign ? 2 : 5);
    return UINT32_C(1) << (sign ? 3 : 4);
  }
  return UINT32_C(1) << (sign ? 1 : 6);
}

static uint32_t fp_minmax(uint32_t a, uint32_t b, bool max) {
  const bool a_nan = fp_is_nan(a);
  const bool b_nan = fp_is_nan(b);

  if (fp_is_snan(a) || fp_is_snan(b))
    softfloat_exceptionFlags |= softfloat_flag_invalid;

  if (a_nan && b_nan) return FP32_CANONICAL_NAN;
  if (a_nan) return b;
  if (b_nan) return a;

  /* RISC-V selects -0 for min and +0 for max. */
  if ((a & UINT32_C(0x7fffffff)) == 0 &&
      (b & UINT32_C(0x7fffffff)) == 0)
    return max ? a & b : a | b;

  return max ? f32_lt_quiet((float32_t){a}, (float32_t){b}) ? b : a
             : f32_lt_quiet((float32_t){a}, (float32_t){b}) ? a : b;
}

static inline uint32_t fp_sign_inject(uint32_t a, uint32_t b, unsigned op) {
  const uint32_t sign = UINT32_C(1) << 31;
  const uint32_t selected = op == 0 ? b : op == 1 ? ~b : a ^ b;
  return (a & ~sign) | (selected & sign);
}

static ExecResult exec_f_arith(ExecContext* ctx, unsigned op) {
  if (!fp_set_rounding(ctx, fp_rm(ctx))) return fp_illegal(ctx);
  fp_begin();

  const float32_t a = {(uint32_t)fp_read_s(ctx, fp_rs1(ctx))};
  const float32_t b = {(uint32_t)fp_read_s(ctx, fp_rs2(ctx))};
  uint32_t result;
  switch (op) {
    case 0:
      result = f32_add(a, b).v;
      break;
    case 1:
      result = f32_sub(a, b).v;
      break;
    case 2:
      result = f32_mul(a, b).v;
      break;
    case 3:
      result = f32_div(a, b).v;
      break;
    default:
      result = f32_sqrt(a).v;
      break;
  }

  fp_write_s(ctx, fp_rd(ctx), result);
  fp_finish(ctx, true);
  return exec_ok();
}

static ExecResult exec_f_fma(ExecContext* ctx, unsigned op) {
  if (!fp_set_rounding(ctx, fp_rm(ctx))) return fp_illegal(ctx);
  fp_begin();

  uint32_t a = (uint32_t)fp_read_s(ctx, fp_rs1(ctx));
  const uint32_t b = (uint32_t)fp_read_s(ctx, fp_rs2(ctx));
  uint32_t c = (uint32_t)fp_read_s(ctx, fp_rs3(ctx));
  const uint32_t sign = UINT32_C(1) << 31;

  /* fmadd, fmsub, fnmsub, fnmadd */
  if (op == 2 || op == 3) a ^= sign;
  if (op == 1 || op == 3) c ^= sign;

  const uint32_t result =
      f32_mulAdd((float32_t){a}, (float32_t){b}, (float32_t){c}).v;
  fp_write_s(ctx, fp_rd(ctx), result);
  fp_finish(ctx, true);
  return exec_ok();
}

static ExecResult exec_f_convert_to_int(ExecContext* ctx, unsigned kind) {
  if (!fp_set_rounding(ctx, fp_rm(ctx))) return fp_illegal(ctx);
  fp_begin();

  const float32_t value = {(uint32_t)fp_read_s(ctx, fp_rs1(ctx))};
  const unsigned rs2 = fp_rs2(ctx);
  word_t result;
  switch (kind) {
    case 0:
      result = (word_t)(int_fast32_t)f32_to_i32(value, softfloat_roundingMode,
                                               true);
      break;
    case 1:
      result = (word_t)(uint_fast32_t)f32_to_ui32(value, softfloat_roundingMode,
                                                  true);
      break;
    case 2:
      result = (word_t)(int_fast64_t)f32_to_i64(value, softfloat_roundingMode,
                                               true);
      break;
    default:
      result = (word_t)(uint_fast64_t)f32_to_ui64(value, softfloat_roundingMode,
                                                  true);
      break;
  }

  /* RV64 sign-extends both 32-bit conversion results. */
  if (rs2 < 2) result = (word_t)(sword_t)(int32_t)(uint32_t)result;
  gpr_write(ctx->hart, (uint8_t)fp_rd(ctx), result);
  fp_finish(ctx, false);
  return exec_ok();
}

static ExecResult exec_f_convert_from_int(ExecContext* ctx, unsigned kind) {
  if (!fp_set_rounding(ctx, fp_rm(ctx))) return fp_illegal(ctx);
  fp_begin();

  const word_t value = fp_gpr_read(ctx, fp_rs1(ctx));
  const unsigned rs2 = fp_rs2(ctx);
  uint32_t result;
  switch (kind) {
    case 0:
      result = i32_to_f32((int32_t)value).v;
      break;
    case 1:
      result = ui32_to_f32((uint32_t)value).v;
      break;
    case 2:
      result = i64_to_f32((int64_t)value).v;
      break;
    default:
      result = ui64_to_f32(value).v;
      break;
  }

  (void)rs2;
  fp_write_s(ctx, fp_rd(ctx), result);
  fp_finish(ctx, true);
  return exec_ok();
}

static ExecResult exec_f_compare(ExecContext* ctx, unsigned op) {
  fp_begin();
  const float32_t a = {(uint32_t)fp_read_s(ctx, fp_rs1(ctx))};
  const float32_t b = {(uint32_t)fp_read_s(ctx, fp_rs2(ctx))};
  bool result;
  switch (op) {
    case 0:
      result = f32_eq(a, b);
      break;
    case 1:
      result = f32_lt(a, b);
      break;
    default:
      result = f32_le(a, b);
      break;
  }

  gpr_write(ctx->hart, (uint8_t)fp_rd(ctx), result ? 1 : 0);
  fp_finish(ctx, false);
  return exec_ok();
}

static ExecResult exec_f_class(ExecContext* ctx) {
  const uint32_t value = (uint32_t)fp_read_s(ctx, fp_rs1(ctx));
  gpr_write(ctx->hart, (uint8_t)fp_rd(ctx), fp_classify(value));
  return exec_ok();
}

static ExecResult exec_f_move_x_w(ExecContext* ctx) {
  const uint32_t value = (uint32_t)fpr_read(ctx->hart, (uint8_t)fp_rs1(ctx));
  gpr_write(ctx->hart, (uint8_t)fp_rd(ctx),
            (word_t)(sword_t)(int32_t)value);
  return exec_ok();
}

static ExecResult exec_f_move_w_x(ExecContext* ctx) {
  fp_write_s(ctx, fp_rd(ctx), (uint32_t)fp_gpr_read(ctx, fp_rs1(ctx)));
  fp_mark_dirty(ctx->hart);
  return exec_ok();
}

static ExecResult exec_f_sign_inject(ExecContext* ctx, unsigned op) {
  const uint32_t a = (uint32_t)fp_read_s(ctx, fp_rs1(ctx));
  const uint32_t b = (uint32_t)fp_read_s(ctx, fp_rs2(ctx));
  fp_write_s(ctx, fp_rd(ctx), fp_sign_inject(a, b, op));
  fp_mark_dirty(ctx->hart);
  return exec_ok();
}

static ExecResult exec_f_minmax(ExecContext* ctx, bool max) {
  fp_begin();
  const uint32_t a = (uint32_t)fp_read_s(ctx, fp_rs1(ctx));
  const uint32_t b = (uint32_t)fp_read_s(ctx, fp_rs2(ctx));
  fp_write_s(ctx, fp_rd(ctx), fp_minmax(a, b, max));
  fp_finish(ctx, true);
  return exec_ok();
}

#endif /* CONFIG_RV_F */

static ExecResult execute_f(ExecContext* ctx, unsigned op) {
#ifndef CONFIG_RV_F
  (void)op;
  return fp_illegal(ctx);
#else
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);

  switch (op) {
    case 0:
      return exec_f_arith(ctx, 0); /* fadd.s */
    case 1:
      return exec_f_arith(ctx, 1); /* fsub.s */
    case 2:
      return exec_f_arith(ctx, 2); /* fmul.s */
    case 3:
      return exec_f_arith(ctx, 3); /* fdiv.s */
    case 4:
      return exec_f_arith(ctx, 4); /* fsqrt.s */
    case 5:
      return exec_f_sign_inject(ctx, 0);
    case 6:
      return exec_f_sign_inject(ctx, 1);
    case 7:
      return exec_f_sign_inject(ctx, 2);
    case 8:
      return exec_f_minmax(ctx, false);
    case 9:
      return exec_f_minmax(ctx, true);
    case 10:
      return exec_f_convert_to_int(ctx, 0);
    case 11:
      return exec_f_convert_to_int(ctx, 1);
    case 12:
      return exec_f_convert_to_int(ctx, 2);
    case 13:
      return exec_f_convert_to_int(ctx, 3);
    case 14:
      return exec_f_move_x_w(ctx);
    case 15:
      return exec_f_compare(ctx, 0); /* feq.s */
    case 16:
      return exec_f_compare(ctx, 1); /* flt.s */
    case 17:
      return exec_f_compare(ctx, 2); /* fle.s */
    case 18:
      return exec_f_class(ctx);
    case 19:
      return exec_f_convert_from_int(ctx, 0);
    case 20:
      return exec_f_convert_from_int(ctx, 1);
    case 21:
      return exec_f_convert_from_int(ctx, 2);
    case 22:
      return exec_f_convert_from_int(ctx, 3);
    case 23:
      return exec_f_move_w_x(ctx);
    case 24:
      return exec_f_fma(ctx, 0);
    case 25:
      return exec_f_fma(ctx, 1);
    case 26:
      return exec_f_fma(ctx, 2);
    case 27:
      return exec_f_fma(ctx, 3);
    default:
      return fp_illegal(ctx);
  }
#endif
}

/* Load/store are intentionally placeholders for this slice. */
ExecResult exec_flw(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}

ExecResult exec_fsw(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}

ExecResult exec_fmadd_s(ExecContext* ctx) { return execute_f(ctx, 24); }
ExecResult exec_fmsub_s(ExecContext* ctx) { return execute_f(ctx, 25); }
ExecResult exec_fnmsub_s(ExecContext* ctx) { return execute_f(ctx, 26); }
ExecResult exec_fnmadd_s(ExecContext* ctx) { return execute_f(ctx, 27); }

ExecResult exec_fadd_s(ExecContext* ctx) { return execute_f(ctx, 0); }
ExecResult exec_fsub_s(ExecContext* ctx) { return execute_f(ctx, 1); }
ExecResult exec_fmul_s(ExecContext* ctx) { return execute_f(ctx, 2); }
ExecResult exec_fdiv_s(ExecContext* ctx) { return execute_f(ctx, 3); }
ExecResult exec_fsqrt_s(ExecContext* ctx) { return execute_f(ctx, 4); }

ExecResult exec_fclass_s(ExecContext* ctx) { return execute_f(ctx, 18); }
ExecResult exec_fcvt_l_s(ExecContext* ctx) { return execute_f(ctx, 12); }
ExecResult exec_fcvt_lu_s(ExecContext* ctx) { return execute_f(ctx, 13); }
ExecResult exec_fcvt_s_l(ExecContext* ctx) { return execute_f(ctx, 21); }
ExecResult exec_fcvt_s_lu(ExecContext* ctx) { return execute_f(ctx, 22); }
ExecResult exec_fcvt_s_w(ExecContext* ctx) { return execute_f(ctx, 19); }
ExecResult exec_fcvt_s_wu(ExecContext* ctx) { return execute_f(ctx, 20); }
ExecResult exec_fcvt_w_s(ExecContext* ctx) { return execute_f(ctx, 10); }
ExecResult exec_fcvt_wu_s(ExecContext* ctx) { return execute_f(ctx, 11); }
ExecResult exec_feq_s(ExecContext* ctx) { return execute_f(ctx, 15); }
ExecResult exec_fle_s(ExecContext* ctx) { return execute_f(ctx, 17); }
ExecResult exec_flt_s(ExecContext* ctx) { return execute_f(ctx, 16); }
ExecResult exec_fmax_s(ExecContext* ctx) { return execute_f(ctx, 9); }
ExecResult exec_fmin_s(ExecContext* ctx) { return execute_f(ctx, 8); }
ExecResult exec_fmv_w_x(ExecContext* ctx) { return execute_f(ctx, 23); }
ExecResult exec_fmv_x_w(ExecContext* ctx) { return execute_f(ctx, 14); }
ExecResult exec_fsgnj_s(ExecContext* ctx) { return execute_f(ctx, 5); }
ExecResult exec_fsgnjn_s(ExecContext* ctx) { return execute_f(ctx, 6); }
ExecResult exec_fsgnjx_s(ExecContext* ctx) { return execute_f(ctx, 7); }
