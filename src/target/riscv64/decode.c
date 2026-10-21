#include <common.h>
#include <cpu/decode.h>
#include <util/log.h>

#include "encoding.h"

/*
 * 译码只匹配已经取到的 32 位编码，并填好 ExecOperands。
 * 取指在 riscv_execute()。扁平表仍是一条指令一行；初始化时建
 * opcode/funct3 索引，运行时再用完整 key/mask 确认。
 */

#define INSTR(_n, _p, _f) ExecResult CONCAT(exec_, _n)(ExecContext * ctx);
#include "instr/a/zaamo/zaamo.def"
#include "instr/a/zalrsc/zalrsc.def"
#include "instr/d/d.def"
#include "instr/f/f.def"
#include "instr/i/i.def"
#include "instr/m/m.def"
#include "instr/m/zmmul/zmmul.def"
#include "instr/priv-m/priv-m.def"
#include "instr/priv-s/priv-s.def"
#include "instr/zicsr/zicsr.def"
#include "instr/zifencei/zifencei.def"
#undef INSTR

#define INSTR(_n, _p, _f) \
  {.name = #_n, .pattern = _p, .fmt = #_f, .exec = CONCAT(exec_, _n)},
static InstrEntry instr_table[] = {
#include "instr/a/zaamo/zaamo.def"
#include "instr/a/zalrsc/zalrsc.def"
#include "instr/d/d.def"
#include "instr/f/f.def"
#include "instr/i/i.def"
#include "instr/m/m.def"
#include "instr/m/zmmul/zmmul.def"
#include "instr/priv-m/priv-m.def"
#include "instr/priv-s/priv-s.def"
#include "instr/zicsr/zicsr.def"
#include "instr/zifencei/zifencei.def"
};
#undef INSTR

#define NR_INSTRS ((int)ARRLEN(instr_table))

enum {
  DECODE_OPCODE_COUNT = 1 << 7,
  DECODE_FUNCT3_COUNT = 1 << 3,
  OPCODE_LOAD_FP = 0x07,
  OPCODE_STORE_FP = 0x27,
  OPCODE_MADD = 0x43,
  OPCODE_MSUB = 0x47,
  OPCODE_NMSUB = 0x4b,
  OPCODE_NMADD = 0x4f,
  OPCODE_OP_FP = 0x53
};

static uint16_t decode_bucket_count[DECODE_OPCODE_COUNT][DECODE_FUNCT3_COUNT];
static uint16_t decode_bucket_offset[DECODE_OPCODE_COUNT][DECODE_FUNCT3_COUNT];
static uint16_t decode_bucket_cursor[DECODE_OPCODE_COUNT][DECODE_FUNCT3_COUNT];
static uint16_t decode_candidates[NR_INSTRS * DECODE_FUNCT3_COUNT];
static bool decode_index_ready;

static void pattern_decode(const char* pat, uint64_t* key, uint64_t* mask,
                           uint8_t* shift) {
  uint64_t k = 0, m = 0;
  int trailing_wild = 0;

  for (const char* p = pat; *p != '\0'; p++) {
    switch (*p) {
      case ' ':
        continue;
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

  *shift = (uint8_t)trailing_wild;
  *key = k >> trailing_wild;
  *mask = m >> trailing_wild;
}

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
  return (funct3 & mask) == key;
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

static bool opcode_uses_fp(uint32_t instr) {
  switch (instr & 0x7f) {
    case OPCODE_LOAD_FP:
    case OPCODE_STORE_FP:
    case OPCODE_MADD:
    case OPCODE_MSUB:
    case OPCODE_NMSUB:
    case OPCODE_NMADD:
    case OPCODE_OP_FP:
      return true;
    default:
      return false;
  }
}

static word_t format_imm(uint32_t instr, const char* fmt) {
  if (fmt[0] == 'I' && fmt[1] == '\0') return (word_t)IMM_I(instr);
  if (fmt[0] == 'S' && fmt[1] == '\0') return (word_t)IMM_S(instr);
  if (fmt[0] == 'B' && fmt[1] == '\0') return (word_t)IMM_B(instr);
  if (fmt[0] == 'U' && fmt[1] == '\0') return (word_t)IMM_U(instr);
  if (fmt[0] == 'J' && fmt[1] == '\0') return (word_t)IMM_J(instr);
  return 0;
}

static void fill_operands(uint32_t instr, Decode* s, const char* fmt) {
  const uint8_t rd = RD(instr);
  const uint8_t rs1 = RS1(instr);
  const uint8_t rs2 = RS2(instr);
  const word_t imm = format_imm(instr, fmt);
  if (opcode_uses_fp(instr)) {
    s->operands.floating = (FpOperands){
        .rd = rd,
        .rs1 = rs1,
        .rs2 = rs2,
        .rs3 = RS3(instr),
        .imm = imm,
        .rm = (FpRoundingMode)FUNCT3(instr),
    };
    return;
  }
  s->operands.integer = (IntOperands){
      .rd = rd,
      .rs1 = rs1,
      .rs2 = rs2,
      .imm = imm,
  };
}

bool riscv_decode(uint32_t instr, Decode* s) {
  Assert(decode_index_ready, "riscv_decode_init() was not called");
  Assert(s != NULL, "riscv_decode 缺少结果");

  s->illegal = true;
  s->exec = NULL;
  const unsigned opcode = instr & 0x7fu;
  const unsigned funct3 = (instr >> 12) & 0x7u;
  const unsigned begin = decode_bucket_offset[opcode][funct3];
  const unsigned end = begin + decode_bucket_count[opcode][funct3];
  for (unsigned pos = begin; pos < end; pos++) {
    const InstrEntry* e = &instr_table[decode_candidates[pos]];
    if (((instr >> e->shift) & e->mask) == e->key) {
      s->illegal = false;
      s->exec = e->exec;
      fill_operands(instr, s, e->fmt);
      return true;
    }
  }
  return false;
}
