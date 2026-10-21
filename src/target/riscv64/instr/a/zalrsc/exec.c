#include <cpu/exec.h>
#include <memory/vaddr.h>

static inline word_t amo_sext(word_t value, int len) {
  if (len == 4) return (word_t)(sword_t)(int32_t)value;
  return value;
}

static inline void reservation_set(CpuState* hart, vaddr_t addr, int len) {
  hart->reservation_valid = true;
  hart->reservation_addr = addr;
  hart->reservation_len = (uint8_t)len;
}

static inline bool reservation_match(const CpuState* hart, vaddr_t addr,
                                     int len) {
  return hart->reservation_valid && hart->reservation_addr == addr &&
         hart->reservation_len == (uint8_t)len;
}

static inline ExecResult amo_lr(ExecContext* ctx, int len) {
  const vaddr_t addr = INT_SRC1(ctx);
  if ((addr & ((vaddr_t)len - 1)) != 0) {
    return exec_trap(EXCP_LOAD_ADDR_MIS, addr);
  }
  const MemResult loaded = vaddr_read(addr, len);
  if (loaded.exception) return exec_trap(loaded.cause, loaded.tval);
  reservation_set(ctx->hart, addr, len);
  gpr_write(ctx->hart, INT_RD(ctx), amo_sext(loaded.data, len));
  return exec_ok();
}

static inline ExecResult amo_sc(ExecContext* ctx, int len) {
  const vaddr_t addr = INT_SRC1(ctx);
  if ((addr & ((vaddr_t)len - 1)) != 0) {
    return exec_trap(EXCP_STORE_AMO_ADDR_MIS, addr);
  }
  const bool reserved = reservation_match(ctx->hart, addr, len);
  reservation_clear(ctx->hart);
  if (!reserved) {
    const MemResult check = vaddr_amo_read(addr, len);
    if (check.exception) return exec_trap(check.cause, check.tval);
    gpr_write(ctx->hart, INT_RD(ctx), 1);
    return exec_ok();
  }
  const MemResult stored = vaddr_write(addr, len, INT_SRC2(ctx));
  if (stored.exception) return exec_trap(stored.cause, stored.tval);
  gpr_write(ctx->hart, INT_RD(ctx), 0);
  return exec_ok();
}

ExecResult exec_lr_w(ExecContext* ctx) { return amo_lr(ctx, 4); }
ExecResult exec_lr_d(ExecContext* ctx) { return amo_lr(ctx, 8); }
ExecResult exec_sc_w(ExecContext* ctx) { return amo_sc(ctx, 4); }
ExecResult exec_sc_d(ExecContext* ctx) { return amo_sc(ctx, 8); }
