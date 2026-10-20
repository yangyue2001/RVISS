#include <common.h>
#include <cpu/decode.h>
#include <cpu/interrupt.h>
#include <memory/vaddr.h>
#include <util/log.h>

#include "cpu_state.h"
#include "encoding.h"

/*
 * 译码：取指 → opcode/funct3 候选索引 → 完整模式匹配 → 填操作数。
 *
 * 对比 NEMU：它用"opcode 一级表 + funct3/funct7 二级表"分级译码；
 * 这里保留一张扁平表（一条指令一行完整模式）作为唯一来源，
 * 只在初始化阶段生成轻量的 opcode/funct3 候选索引。这样新增指令时
 * 仍只需维护一行模式，运行时也不必扫描整张表。
 */

/* 注意：宏参数名不能叫 name/pattern/fmt —— 它们会和结构体成员
 * .name/.pattern/.fmt 撞名，预处理后变成 .add = "add"
 * 这种垃圾。所以参数统一加下划线前缀。 */

/* ---- 由 .def 生成执行函数声明（exec_<名字> 在 exec.c 里实现） ---- */
#define INSTR(_n, _p, _f) void CONCAT(exec_, _n)(Decode * s);
#include "instr/a/instr.def"
#include "instr/fp/instr.def"
#include "instr/i/instr.def"
#include "instr/m/instr.def"
#include "instr/system/instr.def"
#undef INSTR

/* ---- 由同一份 .def 生成指令元信息表 ---- */
#define INSTR(_n, _p, _f) \
  {.name = #_n, .pattern = _p, .fmt = _f, .exec = CONCAT(exec_, _n)},
static InstrEntry instr_table[] = {
#include "instr/a/instr.def"
#include "instr/fp/instr.def"
#include "instr/i/instr.def"
#include "instr/m/instr.def"
#include "instr/system/instr.def"
};
#undef INSTR

#define NR_INSTRS ((int)ARRLEN(instr_table))

/*
 * 扁平表是可读性和匹配优先级的来源；这个索引只是运行时加速结构。
 * 每条指令最多因 funct3 通配而出现在 8 个桶中，最终仍用完整 key/mask
 * 检查，因此不会改变重叠 pattern 的原有表序优先级。
 */
enum { DECODE_OPCODE_COUNT = 1 << 7, DECODE_FUNCT3_COUNT = 1 << 3 };
static uint16_t decode_bucket_count[DECODE_OPCODE_COUNT][DECODE_FUNCT3_COUNT];
static uint16_t decode_bucket_offset[DECODE_OPCODE_COUNT][DECODE_FUNCT3_COUNT];
static uint16_t decode_bucket_cursor[DECODE_OPCODE_COUNT][DECODE_FUNCT3_COUNT];
static uint16_t decode_candidates[NR_INSTRS * DECODE_FUNCT3_COUNT];
static bool decode_index_ready;

/* x0 的写入要被丢弃：指向这个哑元寄存器即可（NEMU 里叫 zero_null） */
static reg_t zero_reg;
static reg_t* gpr_or_zero(uint8_t r) {
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

/* 从原始 32 位 pattern 中提取一个字段，不受尾部通配压缩影响。 */
static void pattern_field(const char* pat, unsigned low, unsigned width,
                          uint32_t* key, uint32_t* mask) {
  uint32_t k = 0, m = 0;
  int bit = 31;

  Assert(low + width <= 32, "pattern field out of range");
  for (const char* p = pat; *p != '\0'; p++) {
    if (*p == ' ') continue;
    Assert(bit >= 0, "pattern has more than 32 bits: %s", pat);
    if ((unsigned)bit >= low && (unsigned)bit < low + width) {
      unsigned field_bit = (unsigned)bit - low;
      if (*p == '0' || *p == '1') {
        m |= UINT32_C(1) << field_bit;
        if (*p == '1') k |= UINT32_C(1) << field_bit;
      } else {
        Assert(*p == '?', "非法 pattern 字符 '%c' (pattern: %s)", *p, pat);
      }
    } else {
      Assert(*p == '0' || *p == '1' || *p == '?',
             "非法 pattern 字符 '%c' (pattern: %s)", *p, pat);
    }
    bit--;
  }
  Assert(bit == -1, "pattern has fewer than 32 bits: %s", pat);
  *key = k;
  *mask = m;
}

static void pattern_bucket(const InstrEntry* e, unsigned* opcode,
                           uint32_t* funct3_key, uint32_t* funct3_mask) {
  uint32_t opcode_key = 0, opcode_mask = 0;
  pattern_field(e->pattern, 0, 7, &opcode_key, &opcode_mask);
  pattern_field(e->pattern, 12, 3, funct3_key, funct3_mask);
  Assert(opcode_mask == UINT32_C(0x7f),
         "instruction %s must fix all opcode bits", e->name);
  *opcode = opcode_key;
}

static bool pattern_matches_funct3(unsigned funct3, uint32_t key,
                                   uint32_t mask) {
  return ((funct3 & mask) == key);
}

void riscv_decode_init(void) {
  for (unsigned opcode = 0; opcode < DECODE_OPCODE_COUNT; opcode++) {
    for (unsigned funct3 = 0; funct3 < DECODE_FUNCT3_COUNT; funct3++) {
      decode_bucket_count[opcode][funct3] = 0;
      decode_bucket_offset[opcode][funct3] = 0;
      decode_bucket_cursor[opcode][funct3] = 0;
    }
  }

  for (int i = 0; i < NR_INSTRS; i++) {
    InstrEntry* e = &instr_table[i];
    pattern_decode(e->pattern, &e->key, &e->mask, &e->shift);

    unsigned opcode;
    uint32_t funct3_key, funct3_mask;
    pattern_bucket(e, &opcode, &funct3_key, &funct3_mask);
    for (unsigned funct3 = 0; funct3 < DECODE_FUNCT3_COUNT; funct3++) {
      if (pattern_matches_funct3(funct3, funct3_key, funct3_mask)) {
        Assert(decode_bucket_count[opcode][funct3] < UINT16_MAX,
               "decoder bucket is too large");
        decode_bucket_count[opcode][funct3]++;
      }
    }
  }

  unsigned total = 0;
  unsigned max_bucket = 0;
  for (unsigned opcode = 0; opcode < DECODE_OPCODE_COUNT; opcode++) {
    for (unsigned funct3 = 0; funct3 < DECODE_FUNCT3_COUNT; funct3++) {
      Assert(total <= UINT16_MAX, "decoder candidate index overflow");
      decode_bucket_offset[opcode][funct3] = (uint16_t)total;
      decode_bucket_cursor[opcode][funct3] = (uint16_t)total;
      total += decode_bucket_count[opcode][funct3];
      if (decode_bucket_count[opcode][funct3] > max_bucket)
        max_bucket = decode_bucket_count[opcode][funct3];
    }
  }
  Assert(total <= ARRLEN(decode_candidates), "decoder candidate pool overflow");

  for (int i = 0; i < NR_INSTRS; i++) {
    const InstrEntry* e = &instr_table[i];
    unsigned opcode;
    uint32_t funct3_key, funct3_mask;
    pattern_bucket(e, &opcode, &funct3_key, &funct3_mask);
    for (unsigned funct3 = 0; funct3 < DECODE_FUNCT3_COUNT; funct3++) {
      if (pattern_matches_funct3(funct3, funct3_key, funct3_mask)) {
        unsigned pos = decode_bucket_cursor[opcode][funct3]++;
        Assert(pos < ARRLEN(decode_candidates),
               "decoder candidate pool overflow");
        decode_candidates[pos] = (uint16_t)i;
      }
    }
  }
  decode_index_ready = true;
  Log("decoder: %d instructions, %u candidate entries, max bucket %u",
      NR_INSTRS, total, max_bucket);
}

static void fill_operands(word_t instr, Decode* s, char fmt) {
  const uint32_t i = s->encoding;
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

static bool fetch_halfword(Decode* s, vaddr_t addr, uint16_t* data) {
  MemAccess access = vaddr_ifetch(addr, 2);
  if (access.status != MEM_OK) {
    s->memory = access;
    if (access.status == MEM_HOST_ERROR)
      s->host_error = true;
    else
      riscv_exec_trap(s, access.cause, access.tval);
    return false;
  }
  *data = (uint16_t)access.data;
  return true;
}

Decode riscv_decode(word_t instr) {
  Assert(decode_index_ready, "riscv_decode_init() was not called");

  unsigned opcode = s->encoding & UINT32_C(0x7f);
  unsigned funct3 = (s->encoding >> 12) & UINT32_C(0x7);
  unsigned begin = decode_bucket_offset[opcode][funct3];
  unsigned end = begin + decode_bucket_count[opcode][funct3];
  for (unsigned pos = begin; pos < end; pos++) {
    const InstrEntry* e = &instr_table[decode_candidates[pos]];
    if (((s->encoding >> e->shift) & e->mask) == e->key) {
      s->exec = e->exec;
      if (!s->mnemonic) s->mnemonic = e->name;
      fill_operands(s, e->fmt);
      return true;
    }
  }
  return false;
}
