#include <common.h>
#include <cpu/decode.h>
#include <cpu/interrupt.h>
#include <memory/vaddr.h>
#include <util/log.h>

#include "cpu_state.h"
#include "encoding.h"

/*
 * 译码：取指 → 线性扫描指令表 → 填操作数。
 *
 * 对比 NEMU：它用"opcode 一级表 + funct3/funct7 二级表"分级译码；
 * 这里改成一张扁平表（一条指令一行完整模式），可读性优先。
 * 将来若表变大（RVV 全量 ~600 条）再换 hash/分级，接口不变。
 */

/* 注意：宏参数名不能叫 name/pattern/fmt —— 它们会和结构体成员
 * .name/.pattern/.fmt 撞名，预处理后变成 .add = "add"
 * 这种垃圾。所以参数统一加下划线前缀。 */

/* ---- 由 .def 生成执行函数声明（exec_<名字> 在 exec.c 里实现） ---- */
#define INSTR(_n, _p, _f) void concat(exec_, _n)(Decode * s);
#include "instr/i/instr.def"
#include "instr/m/instr.def"
#include "instr/a/instr.def"
#include "instr/system/instr.def"
#undef INSTR

/* ---- 由同一份 .def 生成指令元信息表 ---- */
#define INSTR(_n, _p, _f) \
  {.name = #_n, .pattern = _p, .fmt = _f, .exec = concat(exec_, _n)},
static InstrEntry instr_table[] = {
#include "instr/i/instr.def"
#include "instr/m/instr.def"
#include "instr/a/instr.def"
#include "instr/system/instr.def"
};
#undef INSTR

#define NR_INSTRS ((int)ARRLEN(instr_table))

/* x0 的写入要被丢弃：指向这个哑元寄存器即可（NEMU 里叫 zero_null） */
static rtlreg_t zero_reg;
static rtlreg_t* gpr_or_zero(uint8_t r) {
  return r != 0 ? &cpu.gpr[r] : &zero_reg;
}

/* 把 "0000000 ????? ????? 000 ????? 0110011" 编译成 (key, mask, shift) */
static void pattern_decode(const char* pat, uint64_t* key, uint64_t* mask,
                           uint8_t* shift) {
  uint64_t k = 0, m = 0;
  int trailing_wild = 0;

  for (const char* p = pat; *p != '\0'; p++) {
    switch (*p) {
      case ' ':
        continue;
      /* 注意：'0' 是"固定为 0"，mask 也必须置 1；只有 '?' 才是通配 */
      case '0':
        k <<= 1;
        m = (m << 1) | 1u;
        trailing_wild = 0;
        break;
      case '1':
        k = (k << 1) | 1u;
        m = (m << 1) | 1u;
        trailing_wild = 0;
        break;
      case '?':
        k <<= 1;
        m <<= 1;
        trailing_wild++;
        break;
      default:
        panic("非法 pattern 字符 '%c' (pattern: %s)", *p, pat);
    }
  }

  /* 尾部通配位不算，缩短移位 */
  *shift = (uint8_t)trailing_wild;
  *key = k >> trailing_wild;
  *mask = m >> trailing_wild;
}

void riscv_decode_init(void) {
  for (int i = 0; i < NR_INSTRS; i++) {
    InstrEntry* e = &instr_table[i];
    pattern_decode(e->pattern, &e->key, &e->mask, &e->shift);
  }
  Log("decoder: %d instructions in table", NR_INSTRS);
}

static void fill_operands(Decode* s, char fmt) {
  const uint32_t i = s->instr;
  s->rd = RD(i);
  s->rs1 = RS1(i);
  s->rs2 = RS2(i);

  switch (fmt) {
    case 'R':
      s->dest.preg = gpr_or_zero(s->rd);
      s->src1.preg = &cpu.gpr[s->rs1];
      s->src2.preg = &cpu.gpr[s->rs2];
      break;

    case 'I':
      s->dest.preg = gpr_or_zero(s->rd);
      s->src1.preg = &cpu.gpr[s->rs1];
      s->src2.imm = IMM_I(i);
      s->src2.is_imm = true;
      break;

    case 'X': /* 移位立即数 */
      s->dest.preg = gpr_or_zero(s->rd);
      s->src1.preg = &cpu.gpr[s->rs1];
      s->src2.imm = SHAMT(i);
      s->src2.is_imm = true;
      break;

    case 'Z': /* CSR 立即数形式：zimm 在 rs1 位域 */
      s->dest.preg = gpr_or_zero(s->rd);
      s->src1.imm = ZIMM5(i);
      s->src1.is_imm = true;
      break;

    case 'S': /* dest.imm 借用来放 store 偏移；src2 是数据寄存器 */
      s->dest.imm = IMM_S(i);
      s->src1.preg = &cpu.gpr[s->rs1];
      s->src2.preg = &cpu.gpr[s->rs2];
      break;

    case 'B':
      s->dest.imm = IMM_B(i); /* dest.imm 借用来放分支偏移 */
      s->src1.preg = &cpu.gpr[s->rs1];
      s->src2.preg = &cpu.gpr[s->rs2];
      break;

    case 'U':
      s->dest.preg = gpr_or_zero(s->rd);
      s->src1.imm = IMM_U(i);
      s->src1.is_imm = true;
      break;

    case 'J':
      s->dest.preg = gpr_or_zero(s->rd);
      s->src1.imm = IMM_J(i);
      s->src1.is_imm = true;
      break;

    case 'N': /* 无操作数 */
      break;

    default:
      panic("未知操作数格式 '%c' (指令 %s)", fmt, s->mnemonic);
  }
}

static bool fetch_halfword(Decode *s, vaddr_t addr, uint16_t *data) {
  MemAccess access = vaddr_ifetch(addr, 2);
  if (access.status != MEM_OK) {
    s->memory = access;
    if (access.status == MEM_HOST_ERROR) s->host_error = true;
    else riscv_exec_trap(s, access.cause, access.tval);
    return false;
  }
  *data = (uint16_t)access.data;
  return true;
}

bool riscv_decode(Decode* s) {
  /* 当前没有 C，IALIGN=32。错误的跳转在其执行处报告；这里也拒绝
   * 调试/导入提供的非法 PC，避免宿主越界或错误执行。
   */
  if (s->pc & 3) {
    riscv_exec_trap(s, EX_IAM, s->pc);
    return false;
  }
  uint16_t low = 0, high = 0;
  if (!fetch_halfword(s, s->pc, &low)) return false;
  s->instr = low;
  s->raw_bytes = 2;

  if (unlikely((s->instr & 0x3u) != 0x3u)) {
    /* 压缩指令（RVC）：M5 实现。先如实报告译码失败。 */
    s->ilen = 2;
    s->instruction_valid = true;
    s->snpc = s->pc + 2;
    return false;
  }

  s->ilen = 4;
  s->snpc = s->pc + 4;
  if (!fetch_halfword(s, s->pc + 2, &high)) return false;
  s->instr |= (uint32_t)high << 16;
  s->raw_bytes = 4;
  s->instruction_valid = true;

  for (int i = 0; i < NR_INSTRS; i++) {
    const InstrEntry* e = &instr_table[i];
    if (((s->instr >> e->shift) & e->mask) == e->key) {
      s->exec = e->exec;
      s->mnemonic = e->name;
      fill_operands(s, e->fmt);
      return true;
    }
  }
  return false;
}
