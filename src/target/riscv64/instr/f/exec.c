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
  return (value >> 32) == UINT32_MAX ? (uint32_t)value : FP32_CANONICAL_NAN;
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
  if ((a & UINT32_C(0x7fffffff)) == 0 && (b & UINT32_C(0x7fffffff)) == 0)
    return max ? a & b : a | b;

  return max ? f32_lt_quiet((float32_t){a}, (float32_t){b}) ? b : a
         : f32_lt_quiet((float32_t){a}, (float32_t){b}) ? a
                                                        : b;
}

static inline uint32_t fp_sign_inject(uint32_t a, uint32_t b, bool negate,
                                      bool xor_sign) {
  const uint32_t sign = UINT32_C(1) << 31;
  const uint32_t selected = xor_sign ? a ^ b : negate ? ~b : b;
  return (a & ~sign) | (selected & sign);
}

typedef float32_t (*FpBinaryOp)(float32_t, float32_t);
typedef bool (*FpCompareOp)(float32_t, float32_t);

/* Adapt the instruction execution context to one concrete SoftFloat binary op.
 * The caller selects the operation directly; this helper does no dispatch. */
static ExecResult exec_f32_binary(ExecContext* ctx, FpBinaryOp operation) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  if (!fp_set_rounding(ctx, fp_rm(ctx))) return fp_illegal(ctx);
  fp_begin();

  const float32_t a = {(uint32_t)fp_read_s(ctx, fp_rs1(ctx))};
  const float32_t b = {(uint32_t)fp_read_s(ctx, fp_rs2(ctx))};
  const uint32_t result = operation(a, b).v;

  fp_write_s(ctx, fp_rd(ctx), result);
  fp_finish(ctx, true);
  return exec_ok();
}

static ExecResult exec_f32_sqrt(ExecContext* ctx) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  if (!fp_set_rounding(ctx, fp_rm(ctx))) return fp_illegal(ctx);
  fp_begin();

  const float32_t a = {(uint32_t)fp_read_s(ctx, fp_rs1(ctx))};
  const uint32_t result = f32_sqrt(a).v;
  fp_write_s(ctx, fp_rd(ctx), result);
  fp_finish(ctx, true);
  return exec_ok();
}

static ExecResult exec_f32_fma(ExecContext* ctx, bool negate_product,
                               bool negate_c) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  if (!fp_set_rounding(ctx, fp_rm(ctx))) return fp_illegal(ctx);
  fp_begin();

  uint32_t a = (uint32_t)fp_read_s(ctx, fp_rs1(ctx));
  const uint32_t b = (uint32_t)fp_read_s(ctx, fp_rs2(ctx));
  uint32_t c = (uint32_t)fp_read_s(ctx, fp_rs3(ctx));
  const uint32_t sign = UINT32_C(1) << 31;

  if (negate_product) a ^= sign;
  if (negate_c) c ^= sign;

  const uint32_t result =
      f32_mulAdd((float32_t){a}, (float32_t){b}, (float32_t){c}).v;
  fp_write_s(ctx, fp_rd(ctx), result);
  fp_finish(ctx, true);
  return exec_ok();
}

typedef word_t (*FpToIntOp)(float32_t, uint_fast8_t, bool);

static word_t fp_to_i32(float32_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(int_fast32_t)f32_to_i32(value, rm, exact);
}

static word_t fp_to_ui32(float32_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(uint_fast32_t)f32_to_ui32(value, rm, exact);
}

static word_t fp_to_i64(float32_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(int_fast64_t)f32_to_i64(value, rm, exact);
}

static word_t fp_to_ui64(float32_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(uint_fast64_t)f32_to_ui64(value, rm, exact);
}

static ExecResult exec_f32_to_int(ExecContext* ctx, FpToIntOp operation,
                                  bool word_result) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  if (!fp_set_rounding(ctx, fp_rm(ctx))) return fp_illegal(ctx);
  fp_begin();

  const float32_t value = {(uint32_t)fp_read_s(ctx, fp_rs1(ctx))};
  word_t result = operation(value, softfloat_roundingMode, true);

  /* RV64 sign-extends both 32-bit conversion results. */
  if (word_result) result = (word_t)(sword_t)(int32_t)(uint32_t)result;
  gpr_write(ctx->hart, (uint8_t)fp_rd(ctx), result);
  fp_finish(ctx, false);
  return exec_ok();
}

typedef uint32_t (*FpFromIntOp)(word_t);

static uint32_t fp_from_i32(word_t value) {
  return i32_to_f32((int32_t)value).v;
}

static uint32_t fp_from_ui32(word_t value) {
  return ui32_to_f32((uint32_t)value).v;
}

static uint32_t fp_from_i64(word_t value) {
  return i64_to_f32((int64_t)value).v;
}

static uint32_t fp_from_ui64(word_t value) { return ui64_to_f32(value).v; }

static ExecResult exec_f32_from_int(ExecContext* ctx, FpFromIntOp operation) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  if (!fp_set_rounding(ctx, fp_rm(ctx))) return fp_illegal(ctx);
  fp_begin();

  const word_t value = fp_gpr_read(ctx, fp_rs1(ctx));
  const uint32_t result = operation(value);
  fp_write_s(ctx, fp_rd(ctx), result);
  fp_finish(ctx, true);
  return exec_ok();
}

static ExecResult exec_f32_compare(ExecContext* ctx, FpCompareOp operation) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  fp_begin();
  const float32_t a = {(uint32_t)fp_read_s(ctx, fp_rs1(ctx))};
  const float32_t b = {(uint32_t)fp_read_s(ctx, fp_rs2(ctx))};
  const bool result = operation(a, b);

  gpr_write(ctx->hart, (uint8_t)fp_rd(ctx), result ? 1 : 0);
  fp_finish(ctx, false);
  return exec_ok();
}

static ExecResult exec_f_class(ExecContext* ctx) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  const uint32_t value = (uint32_t)fp_read_s(ctx, fp_rs1(ctx));
  gpr_write(ctx->hart, (uint8_t)fp_rd(ctx), fp_classify(value));
  return exec_ok();
}

static ExecResult exec_f_move_x_w(ExecContext* ctx) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  const uint32_t value = (uint32_t)fpr_read(ctx->hart, (uint8_t)fp_rs1(ctx));
  gpr_write(ctx->hart, (uint8_t)fp_rd(ctx), (word_t)(sword_t)(int32_t)value);
  return exec_ok();
}

static ExecResult exec_f_move_w_x(ExecContext* ctx) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  fp_write_s(ctx, fp_rd(ctx), (uint32_t)fp_gpr_read(ctx, fp_rs1(ctx)));
  fp_mark_dirty(ctx->hart);
  return exec_ok();
}

static ExecResult exec_f_sign_inject(ExecContext* ctx, bool negate,
                                     bool xor_sign) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  const uint32_t a = (uint32_t)fp_read_s(ctx, fp_rs1(ctx));
  const uint32_t b = (uint32_t)fp_read_s(ctx, fp_rs2(ctx));
  fp_write_s(ctx, fp_rd(ctx), fp_sign_inject(a, b, negate, xor_sign));
  fp_mark_dirty(ctx->hart);
  return exec_ok();
}

static ExecResult exec_f_minmax(ExecContext* ctx, bool max) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  fp_begin();
  const uint32_t a = (uint32_t)fp_read_s(ctx, fp_rs1(ctx));
  const uint32_t b = (uint32_t)fp_read_s(ctx, fp_rs2(ctx));
  fp_write_s(ctx, fp_rd(ctx), fp_minmax(a, b, max));
  fp_finish(ctx, true);
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

#ifdef CONFIG_RV_F
#define FP_CALL(_ctx, _call) (_call)
#else
#define FP_CALL(_ctx, _call) fp_illegal(_ctx)
#endif

ExecResult exec_fmadd_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_fma(ctx, false, false));
}

ExecResult exec_fmsub_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_fma(ctx, false, true));
}

ExecResult exec_fnmsub_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_fma(ctx, true, true));
}

ExecResult exec_fnmadd_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_fma(ctx, true, false));
}

ExecResult exec_fadd_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_binary(ctx, f32_add));
}

ExecResult exec_fsub_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_binary(ctx, f32_sub));
}

ExecResult exec_fmul_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_binary(ctx, f32_mul));
}

ExecResult exec_fdiv_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_binary(ctx, f32_div));
}

ExecResult exec_fsqrt_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_sqrt(ctx));
}

ExecResult exec_fclass_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f_class(ctx));
}

ExecResult exec_fcvt_l_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_to_int(ctx, fp_to_i64, false));
}

ExecResult exec_fcvt_lu_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_to_int(ctx, fp_to_ui64, false));
}

ExecResult exec_fcvt_s_l(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_from_int(ctx, fp_from_i64));
}

ExecResult exec_fcvt_s_lu(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_from_int(ctx, fp_from_ui64));
}

ExecResult exec_fcvt_s_w(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_from_int(ctx, fp_from_i32));
}

ExecResult exec_fcvt_s_wu(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_from_int(ctx, fp_from_ui32));
}

ExecResult exec_fcvt_w_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_to_int(ctx, fp_to_i32, true));
}

ExecResult exec_fcvt_wu_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_to_int(ctx, fp_to_ui32, true));
}

ExecResult exec_feq_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_compare(ctx, f32_eq));
}

ExecResult exec_fle_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_compare(ctx, f32_le));
}

ExecResult exec_flt_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f32_compare(ctx, f32_lt));
}

ExecResult exec_fmax_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f_minmax(ctx, true));
}

ExecResult exec_fmin_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f_minmax(ctx, false));
}

ExecResult exec_fmv_w_x(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f_move_w_x(ctx));
}

ExecResult exec_fmv_x_w(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f_move_x_w(ctx));
}

ExecResult exec_fsgnj_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f_sign_inject(ctx, false, false));
}

ExecResult exec_fsgnjn_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f_sign_inject(ctx, true, false));
}

ExecResult exec_fsgnjx_s(ExecContext* ctx) {
  return FP_CALL(ctx, exec_f_sign_inject(ctx, false, true));
}

#undef FP_CALL
