#include "../fp/exec_common.h"
#include <memory/vaddr.h>

static word_t fp32_classify(word_t value) {
  return fp_classify_bits(value, FP32_MANT_BITS, FP32_EXP_BITS);
}

static bool fp32_less(word_t lhs, word_t rhs) {
  return f32_lt_quiet((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs});
}

static word_t fp32_min(word_t lhs, word_t rhs) {
  return fp_minmax_bits(lhs, rhs, false, FP32_MANT_BITS, FP32_EXP_BITS,
                        fp32_less);
}

static word_t fp32_max(word_t lhs, word_t rhs) {
  return fp_minmax_bits(lhs, rhs, true, FP32_MANT_BITS, FP32_EXP_BITS,
                        fp32_less);
}

static word_t fp32_add(word_t lhs, word_t rhs) {
  return f32_add((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs}).v;
}

static word_t fp32_sub(word_t lhs, word_t rhs) {
  return f32_sub((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs}).v;
}

static word_t fp32_mul(word_t lhs, word_t rhs) {
  return f32_mul((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs}).v;
}

static word_t fp32_div(word_t lhs, word_t rhs) {
  return f32_div((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs}).v;
}

static word_t fp32_sqrt(word_t value) {
  return f32_sqrt((float32_t){(uint32_t)value}).v;
}

static word_t fp32_fma(word_t lhs, word_t rhs, word_t addend) {
  return f32_mulAdd((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs},
                    (float32_t){(uint32_t)addend})
      .v;
}

static bool fp32_eq(word_t lhs, word_t rhs) {
  return f32_eq((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs});
}

static bool fp32_lt(word_t lhs, word_t rhs) {
  return f32_lt((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs});
}

static bool fp32_le(word_t lhs, word_t rhs) {
  return f32_le((float32_t){(uint32_t)lhs}, (float32_t){(uint32_t)rhs});
}

static word_t fp32_to_i32(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(int_fast32_t)f32_to_i32((float32_t){(uint32_t)value}, rm,
                                          exact);
}

static word_t fp32_to_ui32(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(uint_fast32_t)f32_to_ui32((float32_t){(uint32_t)value}, rm,
                                            exact);
}

static word_t fp32_to_i64(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(int_fast64_t)f32_to_i64((float32_t){(uint32_t)value}, rm,
                                          exact);
}

static word_t fp32_to_ui64(word_t value, uint_fast8_t rm, bool exact) {
  return (word_t)(uint_fast64_t)f32_to_ui64((float32_t){(uint32_t)value}, rm,
                                            exact);
}

static word_t fp32_from_i32(word_t value) {
  return i32_to_f32((int32_t)value).v;
}

static word_t fp32_from_ui32(word_t value) {
  return ui32_to_f32((uint32_t)value).v;
}

static word_t fp32_from_i64(word_t value) {
  return i64_to_f32((int64_t)value).v;
}

static word_t fp32_from_ui64(word_t value) { return ui64_to_f32(value).v; }

/* FMV.X.W moves the raw low word. fp32_read would turn a bad NaN-box into a
 * canonical NaN. */
static word_t fp32_move_to_x(CpuState* hart, uint8_t rs) {
  return (word_t)(sword_t)(int32_t)(uint32_t)hart->fpr[rs];
}

/* Load/store stay placeholders until the new memory operand path exists. */
ExecResult exec_flw(ExecContext* ctx) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  const vaddr_t addr = gpr_read(ctx->hart, FLOAT_RS1(ctx)) + FLOAT_IMM(ctx);
  const MemResult mem = vaddr_read(addr, 4);
  if (mem.exception) return exec_trap(mem.cause, mem.tval);
  fp32_write(ctx->hart, FLOAT_RD(ctx), mem.data);
  fp_mark_dirty(ctx->hart);
  return exec_ok();
}

ExecResult exec_fsw(ExecContext* ctx) {
  if (!fp_enabled(ctx->hart)) return fp_illegal(ctx);
  const vaddr_t addr = gpr_read(ctx->hart, FLOAT_RS1(ctx)) + FLOAT_IMM(ctx);
  ctx->hart->reservation_valid = false;
  const MemResult mem = vaddr_write(addr, 4, (uint32_t)ctx->hart->fpr[FLOAT_RS2(ctx)]);
  if (mem.exception) return exec_trap(mem.cause, mem.tval);
  return exec_ok();
}

ExecResult exec_fmadd_s(ExecContext* ctx) {
  return fp_fma(ctx, fp32_read, fp32_write, fp32_fma, FP32_SIGN_BIT, false,
                false);
}

ExecResult exec_fmsub_s(ExecContext* ctx) {
  return fp_fma(ctx, fp32_read, fp32_write, fp32_fma, FP32_SIGN_BIT, false,
                true);
}

ExecResult exec_fnmsub_s(ExecContext* ctx) {
  return fp_fma(ctx, fp32_read, fp32_write, fp32_fma, FP32_SIGN_BIT, true,
                false);
}

ExecResult exec_fnmadd_s(ExecContext* ctx) {
  return fp_fma(ctx, fp32_read, fp32_write, fp32_fma, FP32_SIGN_BIT, true,
                true);
}

ExecResult exec_fadd_s(ExecContext* ctx) {
  return fp_binary(ctx, fp32_read, fp32_write, fp32_add);
}

ExecResult exec_fsub_s(ExecContext* ctx) {
  return fp_binary(ctx, fp32_read, fp32_write, fp32_sub);
}

ExecResult exec_fmul_s(ExecContext* ctx) {
  return fp_binary(ctx, fp32_read, fp32_write, fp32_mul);
}

ExecResult exec_fdiv_s(ExecContext* ctx) {
  return fp_binary(ctx, fp32_read, fp32_write, fp32_div);
}

ExecResult exec_fsqrt_s(ExecContext* ctx) {
  return fp_unary(ctx, fp32_read, fp32_write, fp32_sqrt);
}

ExecResult exec_fclass_s(ExecContext* ctx) {
  return fp_classify(ctx, fp32_read, fp32_classify);
}

ExecResult exec_fcvt_l_s(ExecContext* ctx) {
  return fp_to_int(ctx, fp32_read, fp32_to_i64, false);
}

ExecResult exec_fcvt_lu_s(ExecContext* ctx) {
  return fp_to_int(ctx, fp32_read, fp32_to_ui64, false);
}

ExecResult exec_fcvt_s_l(ExecContext* ctx) {
  return fp_from_int(ctx, fp32_write, fp32_from_i64);
}

ExecResult exec_fcvt_s_lu(ExecContext* ctx) {
  return fp_from_int(ctx, fp32_write, fp32_from_ui64);
}

ExecResult exec_fcvt_s_w(ExecContext* ctx) {
  return fp_from_int(ctx, fp32_write, fp32_from_i32);
}

ExecResult exec_fcvt_s_wu(ExecContext* ctx) {
  return fp_from_int(ctx, fp32_write, fp32_from_ui32);
}

ExecResult exec_fcvt_w_s(ExecContext* ctx) {
  return fp_to_int(ctx, fp32_read, fp32_to_i32, true);
}

ExecResult exec_fcvt_wu_s(ExecContext* ctx) {
  return fp_to_int(ctx, fp32_read, fp32_to_ui32, true);
}

ExecResult exec_feq_s(ExecContext* ctx) {
  return fp_compare(ctx, fp32_read, fp32_eq);
}

ExecResult exec_fle_s(ExecContext* ctx) {
  return fp_compare(ctx, fp32_read, fp32_le);
}

ExecResult exec_flt_s(ExecContext* ctx) {
  return fp_compare(ctx, fp32_read, fp32_lt);
}

ExecResult exec_fmax_s(ExecContext* ctx) {
  return fp_minmax(ctx, fp32_read, fp32_write, fp32_max);
}

ExecResult exec_fmin_s(ExecContext* ctx) {
  return fp_minmax(ctx, fp32_read, fp32_write, fp32_min);
}

ExecResult exec_fmv_w_x(ExecContext* ctx) {
  return fp_fmv_from_gpr(ctx, fp32_write);
}

ExecResult exec_fmv_x_w(ExecContext* ctx) {
  return fp_fmv_to_gpr(ctx, fp32_move_to_x);
}

ExecResult exec_fsgnj_s(ExecContext* ctx) {
  return fp_sign_inject_exec(ctx, fp32_read, fp32_write, FP32_SIGN_BIT, false,
                             false);
}

ExecResult exec_fsgnjn_s(ExecContext* ctx) {
  return fp_sign_inject_exec(ctx, fp32_read, fp32_write, FP32_SIGN_BIT, true,
                             false);
}

ExecResult exec_fsgnjx_s(ExecContext* ctx) {
  return fp_sign_inject_exec(ctx, fp32_read, fp32_write, FP32_SIGN_BIT, false,
                             true);
}
