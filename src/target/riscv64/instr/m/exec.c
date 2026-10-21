#include <cpu/exec.h>

static word_t div_signed(word_t a, word_t b) {
  if (b == 0) return UINT64_MAX;
  if (a == UINT64_C(0x8000000000000000) && b == UINT64_MAX) return a;
  return (word_t)((sword_t)a / (sword_t)b);
}

static word_t rem_signed(word_t a, word_t b) {
  if (b == 0) return a;
  if (a == UINT64_C(0x8000000000000000) && b == UINT64_MAX) return 0;
  return (word_t)((sword_t)a % (sword_t)b);
}

static word_t div_unsigned(word_t a, word_t b) {
  return b == 0 ? UINT64_MAX : a / b;
}

static word_t rem_unsigned(word_t a, word_t b) { return b == 0 ? a : a % b; }

static word_t word_div(word_t a, word_t b) {
  const int32_t lhs = (int32_t)a;
  const int32_t rhs = (int32_t)b;
  if (rhs == 0) return UINT64_MAX;
  if (lhs == INT32_MIN && rhs == -1) return (word_t)(sword_t)lhs;
  return (word_t)(sword_t)(lhs / rhs);
}

static word_t word_rem(word_t a, word_t b) {
  const int32_t lhs = (int32_t)a;
  const int32_t rhs = (int32_t)b;
  if (rhs == 0) return (word_t)(sword_t)lhs;
  if (lhs == INT32_MIN && rhs == -1) return 0;
  return (word_t)(sword_t)(lhs % rhs);
}

static word_t word_divu(word_t a, word_t b) {
  const uint32_t lhs = (uint32_t)a;
  const uint32_t rhs = (uint32_t)b;
  const uint32_t result = rhs == 0 ? UINT32_MAX : lhs / rhs;
  return (word_t)(sword_t)(int32_t)result;
}

static word_t word_remu(word_t a, word_t b) {
  const uint32_t lhs = (uint32_t)a;
  const uint32_t rhs = (uint32_t)b;
  const uint32_t result = rhs == 0 ? lhs : lhs % rhs;
  return (word_t)(sword_t)(int32_t)result;
}

static ExecResult exec_int(ExecContext* ctx, word_t value) {
  gpr_write(ctx->hart, INT_RD(ctx), value);
  return exec_ok();
}

ExecResult exec_div(ExecContext* ctx) {
  return exec_int(ctx, div_signed(INT_SRC1(ctx), INT_SRC2(ctx)));
}
ExecResult exec_divu(ExecContext* ctx) {
  return exec_int(ctx, div_unsigned(INT_SRC1(ctx), INT_SRC2(ctx)));
}
ExecResult exec_rem(ExecContext* ctx) {
  return exec_int(ctx, rem_signed(INT_SRC1(ctx), INT_SRC2(ctx)));
}
ExecResult exec_remu(ExecContext* ctx) {
  return exec_int(ctx, rem_unsigned(INT_SRC1(ctx), INT_SRC2(ctx)));
}
ExecResult exec_divw(ExecContext* ctx) {
  return exec_int(ctx, word_div(INT_SRC1(ctx), INT_SRC2(ctx)));
}
ExecResult exec_divuw(ExecContext* ctx) {
  return exec_int(ctx, word_divu(INT_SRC1(ctx), INT_SRC2(ctx)));
}
ExecResult exec_remw(ExecContext* ctx) {
  return exec_int(ctx, word_rem(INT_SRC1(ctx), INT_SRC2(ctx)));
}
ExecResult exec_remuw(ExecContext* ctx) {
  return exec_int(ctx, word_remu(INT_SRC1(ctx), INT_SRC2(ctx)));
}
