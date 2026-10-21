#include <cpu/exec.h>
#include <memory/vaddr.h>

/* funct7[31:27]，和规范里的 AMO 操作码一致。 */
enum {
  AMO_ADD = 0,
  AMO_SWAP = 1,
  AMO_XOR = 4,
  AMO_OR = 8,
  AMO_AND = 12,
  AMO_MIN = 16,
  AMO_MAX = 20,
  AMO_MINU = 24,
  AMO_MAXU = 28
};

static inline word_t amo_sext(word_t value, int len) {
  if (len == 4) return (word_t)(sword_t)(int32_t)value;
  return value;
}

static inline ExecResult amo_operate(ExecContext* ctx, int len, unsigned op) {
  const vaddr_t addr = INT_SRC1(ctx);
  word_t src = INT_SRC2(ctx);
  if (len == 4) src &= UINT32_MAX;
  reservation_clear(ctx->hart);

  const MemResult loaded = vaddr_amo_read(addr, len);
  if (loaded.exception) return exec_trap(loaded.cause, loaded.tval);

  const word_t old = loaded.data & (len == 4 ? UINT32_MAX : UINT64_MAX);
  const sword_t signed_old = (sword_t)amo_sext(old, len);
  const sword_t signed_src = (sword_t)amo_sext(src, len);
  word_t result;
  switch (op) {
    case AMO_ADD:
      result = old + src;
      break;
    case AMO_SWAP:
      result = src;
      break;
    case AMO_XOR:
      result = old ^ src;
      break;
    case AMO_OR:
      result = old | src;
      break;
    case AMO_AND:
      result = old & src;
      break;
    case AMO_MIN:
      result = signed_old < signed_src ? old : src;
      break;
    case AMO_MAX:
      result = signed_old > signed_src ? old : src;
      break;
    case AMO_MINU:
      result = old < src ? old : src;
      break;
    case AMO_MAXU:
      result = old > src ? old : src;
      break;
    default:
      return exec_trap(EXCP_ILLEGAL_INST, ctx->raw_instr);
  }

  const MemResult stored = vaddr_write(addr, len, result);
  if (stored.exception) return exec_trap(stored.cause, stored.tval);
  gpr_write(ctx->hart, INT_RD(ctx), amo_sext(old, len));
  return exec_ok();
}

ExecResult exec_amoadd_w(ExecContext* ctx) {
  return amo_operate(ctx, 4, AMO_ADD);
}
ExecResult exec_amoadd_d(ExecContext* ctx) {
  return amo_operate(ctx, 8, AMO_ADD);
}
ExecResult exec_amoswap_w(ExecContext* ctx) {
  return amo_operate(ctx, 4, AMO_SWAP);
}
ExecResult exec_amoswap_d(ExecContext* ctx) {
  return amo_operate(ctx, 8, AMO_SWAP);
}
ExecResult exec_amoxor_w(ExecContext* ctx) {
  return amo_operate(ctx, 4, AMO_XOR);
}
ExecResult exec_amoxor_d(ExecContext* ctx) {
  return amo_operate(ctx, 8, AMO_XOR);
}
ExecResult exec_amoor_w(ExecContext* ctx) {
  return amo_operate(ctx, 4, AMO_OR);
}
ExecResult exec_amoor_d(ExecContext* ctx) {
  return amo_operate(ctx, 8, AMO_OR);
}
ExecResult exec_amoand_w(ExecContext* ctx) {
  return amo_operate(ctx, 4, AMO_AND);
}
ExecResult exec_amoand_d(ExecContext* ctx) {
  return amo_operate(ctx, 8, AMO_AND);
}
ExecResult exec_amomin_w(ExecContext* ctx) {
  return amo_operate(ctx, 4, AMO_MIN);
}
ExecResult exec_amomin_d(ExecContext* ctx) {
  return amo_operate(ctx, 8, AMO_MIN);
}
ExecResult exec_amomax_w(ExecContext* ctx) {
  return amo_operate(ctx, 4, AMO_MAX);
}
ExecResult exec_amomax_d(ExecContext* ctx) {
  return amo_operate(ctx, 8, AMO_MAX);
}
ExecResult exec_amominu_w(ExecContext* ctx) {
  return amo_operate(ctx, 4, AMO_MINU);
}
ExecResult exec_amominu_d(ExecContext* ctx) {
  return amo_operate(ctx, 8, AMO_MINU);
}
ExecResult exec_amomaxu_w(ExecContext* ctx) {
  return amo_operate(ctx, 4, AMO_MAXU);
}
ExecResult exec_amomaxu_d(ExecContext* ctx) {
  return amo_operate(ctx, 8, AMO_MAXU);
}
