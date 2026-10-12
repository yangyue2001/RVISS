#include <common.h>
#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <memory/paddr.h>
#include <memory/vaddr.h>

static bool prepare(Decode *s, int len, int type) {
  s->memory = isa_mem_prepare(*s->src1.preg, len, type, true);
  if (s->memory.status == MEM_HOST_ERROR) s->host_error = true;
  if (s->memory.status == MEM_FAULT)
    riscv_exec_trap(s, s->memory.cause, s->memory.tval);
  return s->memory.status == MEM_OK;
}

static bool read_ram(Decode *s, word_t *old) {
  if (paddr_try_read(s->memory.paddr, s->memory.length, old, NULL)) return true;
  s->memory.status = MEM_HOST_ERROR;
  s->host_error = true; /* prepare 已检查 RAM；无并行修改映射 */
  return false;
}

static bool write_ram(Decode *s, word_t value) {
  if (s->memory.length == 4) value = (uint32_t)value;
  if (!paddr_try_write(s->memory.paddr, s->memory.length, value, NULL)) {
    s->memory.status = MEM_HOST_ERROR;
    s->host_error = true;
    return false;
  }
  s->memory.data = value;
  s->memory.write_performed = true;
  return true;
}

static word_t extend(word_t value, int len) {
  return len == 4 ? (word_t)(sword_t)(int32_t)value : value;
}

static void lr(Decode *s, int len) {
  if (!prepare(s, len, MEM_TYPE_READ)) return;
  word_t old;
  if (!read_ram(s, &old)) return;
  cpu.reservation_addr = s->memory.paddr;
  cpu.reservation_length = len;
  cpu.reservation_valid = true;
  s->memory.data = old;
  *s->dest.preg = extend(old, len);
}

static void sc(Decode *s, int len) {
  word_t value = *s->src2.preg;
  bool valid = cpu.reservation_valid;
  cpu.reservation_valid = false; /* 每次 SC，包括故障，消耗 reservation */
  if (!prepare(s, len, MEM_TYPE_WRITE)) return;
  bool success = valid && cpu.reservation_addr == s->memory.paddr &&
                 len <= cpu.reservation_length;
  if (success && !write_ram(s, value)) return;
  *s->dest.preg = success ? 0 : 1;
}

static void amo(Decode *s, int len, unsigned op) {
  word_t value = *s->src2.preg;
  if (!prepare(s, len, MEM_TYPE_WRITE)) return;
  word_t old;
  if (!read_ram(s, &old)) return;
  if (len == 4) value = (uint32_t)value;
  word_t result;
  sword_t signed_old = (sword_t)extend(old, len);
  sword_t signed_value = (sword_t)extend(value, len);
  switch (op) {
    case 0: result = old + value; break;
    case 1: result = value; break;
    case 4: result = old ^ value; break;
    case 8: result = old | value; break;
    case 12: result = old & value; break;
    case 16: result = signed_old < signed_value ? old : value; break;
    case 20: result = signed_old > signed_value ? old : value; break;
    case 24: result = old < value ? old : value; break;
    case 28: result = old > value ? old : value; break;
    default: s->host_error = true; return;
  }
  if (write_ram(s, result)) *s->dest.preg = extend(old, len);
}

void exec_lr_w(Decode *s) { lr(s, 4); }
void exec_lr_d(Decode *s) { lr(s, 8); }
void exec_sc_w(Decode *s) { sc(s, 4); }
void exec_sc_d(Decode *s) { sc(s, 8); }
#define AMO(name, op) \
  void exec_##name##_w(Decode *s) { amo(s, 4, op); } \
  void exec_##name##_d(Decode *s) { amo(s, 8, op); }
AMO(amoadd, 0)
AMO(amoswap, 1)
AMO(amoxor, 4)
AMO(amoor, 8)
AMO(amoand, 12)
AMO(amomin, 16)
AMO(amomax, 20)
AMO(amominu, 24)
AMO(amomaxu, 28)
