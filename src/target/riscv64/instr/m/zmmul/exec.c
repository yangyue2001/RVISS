#include <cpu/exec.h>

static word_t mulh(word_t a, word_t b) {
  const __int128 product = (__int128)(sword_t)a * (__int128)(sword_t)b;
  return (word_t)((unsigned __int128)product >> 64);
}

static word_t mulhsu(word_t a, word_t b) {
  const __int128 product = (__int128)(sword_t)a * (__int128)b;
  return (word_t)((unsigned __int128)product >> 64);
}

static word_t mulhu(word_t a, word_t b) {
  return (word_t)(((unsigned __int128)a * b) >> 64);
}

static ExecResult exec_int(ExecContext* ctx, word_t value) {
  gpr_write(ctx->hart, INT_RD(ctx), value);
  return exec_ok();
}

ExecResult exec_mul(ExecContext* ctx) {
  return exec_int(ctx, INT_SRC1(ctx) * INT_SRC2(ctx));
}
ExecResult exec_mulh(ExecContext* ctx) {
  return exec_int(ctx, mulh(INT_SRC1(ctx), INT_SRC2(ctx)));
}
ExecResult exec_mulhsu(ExecContext* ctx) {
  return exec_int(ctx, mulhsu(INT_SRC1(ctx), INT_SRC2(ctx)));
}
ExecResult exec_mulhu(ExecContext* ctx) {
  return exec_int(ctx, mulhu(INT_SRC1(ctx), INT_SRC2(ctx)));
}
ExecResult exec_mulw(ExecContext* ctx) {
  const uint32_t product = (uint32_t)INT_SRC1(ctx) * (uint32_t)INT_SRC2(ctx);
  return exec_int(ctx, (word_t)(sword_t)(int32_t)product);
}
