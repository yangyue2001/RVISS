#include <common.h>
#include <cpu/decode.h>

/* 128 位乘积不溢出；先转为无符号再提取高位，避免依赖负数右移。
 * signed * unsigned 的最大绝对值也在 signed __int128 范围内。 */
static word_t mulh(word_t a, word_t b) {
  __int128 product = (__int128)(sword_t)a * (__int128)(sword_t)b;
  return (unsigned __int128)product >> 64;
}

static word_t mulhsu(word_t a, word_t b) {
  __int128 product = (__int128)(sword_t)a * (__int128)b;
  return (unsigned __int128)product >> 64;
}

static word_t mulhu(word_t a, word_t b) {
  return ((unsigned __int128)a * b) >> 64;
}

static word_t div_signed(word_t a, word_t b) {
  if (!b) return UINT64_MAX;
  if (a == UINT64_C(0x8000000000000000) && b == UINT64_MAX) return a;
  return (sword_t)a / (sword_t)b;
}

static word_t rem_signed(word_t a, word_t b) {
  if (!b) return a;
  if (a == UINT64_C(0x8000000000000000) && b == UINT64_MAX) return 0;
  return (sword_t)a % (sword_t)b;
}

static word_t div_unsigned(word_t a, word_t b) { return b ? a / b : UINT64_MAX; }
static word_t rem_unsigned(word_t a, word_t b) { return b ? a % b : a; }

static word_t word_div(word_t a, word_t b) {
  int32_t lhs = (int32_t)a, rhs = (int32_t)b;
  if (!rhs) return UINT64_MAX;
  if (lhs == INT32_MIN && rhs == -1) return (sword_t)lhs;
  return (sword_t)(lhs / rhs);
}

static word_t word_rem(word_t a, word_t b) {
  int32_t lhs = (int32_t)a, rhs = (int32_t)b;
  if (!rhs) return (sword_t)lhs;
  if (lhs == INT32_MIN && rhs == -1) return 0;
  return (sword_t)(lhs % rhs);
}

/* unsigned *W 也必须符号扩展，包括除零结果。 */
static word_t word_divu(word_t a, word_t b) {
  uint32_t lhs = a, rhs = b;
  return (sword_t)(int32_t)(rhs ? lhs / rhs : UINT32_MAX);
}

static word_t word_remu(word_t a, word_t b) {
  uint32_t lhs = a, rhs = b;
  return (sword_t)(int32_t)(rhs ? lhs % rhs : lhs);
}

/* 在写 rd 前读取两个输入，包含 rd=rs1/rs2、x0 的真实执行路径。 */
#define EXEC_M(name, expression) \
  void exec_##name(Decode *s) { \
    word_t a = *s->src1.preg, b = *s->src2.preg; \
    *s->dest.preg = (expression); \
  }

EXEC_M(mul, a * b)
EXEC_M(mulh, mulh(a, b))
EXEC_M(mulhsu, mulhsu(a, b))
EXEC_M(mulhu, mulhu(a, b))
EXEC_M(div, div_signed(a, b))
EXEC_M(divu, div_unsigned(a, b))
EXEC_M(rem, rem_signed(a, b))
EXEC_M(remu, rem_unsigned(a, b))
EXEC_M(mulw, (sword_t)(int32_t)((uint32_t)a * (uint32_t)b))
EXEC_M(divw, word_div(a, b))
EXEC_M(divuw, word_divu(a, b))
EXEC_M(remw, word_rem(a, b))
EXEC_M(remuw, word_remu(a, b))
