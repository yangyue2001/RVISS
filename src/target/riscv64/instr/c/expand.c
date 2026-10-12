#include <common.h>
#include <cpu/decode.h>

enum {
#define CINSTR(n,k,m) C_##n,
#include "instr.def"
#undef CINSTR
};

static const struct { const char *name; uint16_t key, mask; int kind; } entries[] = {
#define CINSTR(n,k,m) {#n,k,m,C_##n},
#include "instr.def"
#undef CINSTR
};

static uint32_t imm_op(unsigned opcode, unsigned funct3, unsigned rd,
                       unsigned rs1, int32_t imm) {
  return (((uint32_t)imm & 0xfff) << 20) | (rs1 << 15) | (funct3 << 12) |
         (rd << 7) | opcode;
}

static uint32_t reg_op(unsigned opcode, unsigned funct3, unsigned funct7,
                       unsigned rd, unsigned rs1, unsigned rs2) {
  return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
         (rd << 7) | opcode;
}

static uint32_t store(unsigned funct3, unsigned rs1, unsigned rs2, unsigned imm) {
  return ((imm >> 5) << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
         ((imm & 31) << 7) | 0x23;
}

static uint32_t branch(unsigned funct3, unsigned rs1, uint32_t imm) {
  return (((imm >> 12) & 1) << 31) | (((imm >> 5) & 63) << 25) |
         (rs1 << 15) | (funct3 << 12) | (((imm >> 1) & 15) << 8) |
         (((imm >> 11) & 1) << 7) | 0x63;
}

static uint32_t jump(uint32_t imm) {
  return (((imm >> 20) & 1) << 31) | (((imm >> 1) & 1023) << 21) |
         (((imm >> 11) & 1) << 20) | (((imm >> 12) & 255) << 12) | 0x6f;
}

static bool expand(Decode *s, int kind) {
  uint16_t c = s->instr;
  unsigned rd = BITS(c,11,7), rs2 = BITS(c,6,2);
  unsigned rp = 8 + BITS(c,9,7), r2p = 8 + BITS(c,4,2);
  unsigned ci = (BIT(c,12) << 5) | BITS(c,6,2);
  int32_t simm = SEXT(ci,6);
  unsigned word_offset = (BITS(c,12,10) << 3) | (BIT(c,6) << 2) | (BIT(c,5) << 6);
  unsigned double_offset = (BITS(c,12,10) << 3) | (BITS(c,6,5) << 6);
  unsigned imm;
  uint32_t e;
  switch (kind) {
    case C_c_addi4spn:
      imm = (BITS(c,12,11) << 4) | (BITS(c,10,7) << 6) |
            (BIT(c,6) << 2) | (BIT(c,5) << 3);
      if (!imm) return false;
      e = imm_op(0x13,0,r2p,2,imm); break;
    case C_c_lw: e = imm_op(0x03,2,r2p,rp,word_offset); break;
    case C_c_ld: e = imm_op(0x03,3,r2p,rp,double_offset); break;
#ifdef CONFIG_RV_D
    case C_c_fld: e = imm_op(0x07,3,r2p,rp,double_offset); break;
    case C_c_fsd:
      e = store(3,rp,r2p,double_offset); e = (e&~0x7fu)|0x27; break;
    case C_c_fldsp:
      imm = (BIT(c,12) << 5) | (BITS(c,6,5) << 3) | (BITS(c,4,2) << 6);
      e = imm_op(0x07,3,rd,2,imm); break; /* f0 合法，区别整数 LDSP */
    case C_c_fsdsp:
      imm = (BITS(c,12,10) << 3) | (BITS(c,9,7) << 6);
      e = store(3,2,rs2,imm); e=(e&~0x7fu)|0x27; break;
#endif
    case C_c_sw: e = store(2,rp,r2p,word_offset); break;
    case C_c_sd: e = store(3,rp,r2p,double_offset); break;
    case C_c_nop: e = 0x13; break;
    case C_c_addi: e = imm_op(0x13,0,rd,rd,simm); break;
    case C_c_addiw:
      if (!rd) return false;
      e = imm_op(0x1b,0,rd,rd,simm); break;
    case C_c_li: e = imm_op(0x13,0,rd,0,simm); break;
    case C_c_addi16sp:
      imm = (BIT(c,12) << 9) | (BIT(c,6) << 4) | (BIT(c,5) << 6) |
            (BITS(c,4,3) << 7) | (BIT(c,2) << 5);
      if (!imm) return false;
      e = imm_op(0x13,0,2,2,SEXT(imm,10)); break;
    case C_c_lui:
      if (!ci) return false;
      e = (((uint32_t)simm & 0xfffff) << 12) | (rd << 7) | 0x37; break;
    case C_c_srli: e = imm_op(0x13,5,rp,rp,ci); break;
    case C_c_srai: e = imm_op(0x13,5,rp,rp,0x400 | ci); break;
    case C_c_andi: e = imm_op(0x13,7,rp,rp,simm); break;
    case C_c_sub: e = reg_op(0x33,0,0x20,rp,rp,r2p); break;
    case C_c_xor: e = reg_op(0x33,4,0,rp,rp,r2p); break;
    case C_c_or: e = reg_op(0x33,6,0,rp,rp,r2p); break;
    case C_c_and: e = reg_op(0x33,7,0,rp,rp,r2p); break;
    case C_c_subw: e = reg_op(0x3b,0,0x20,rp,rp,r2p); break;
    case C_c_addw: e = reg_op(0x3b,0,0,rp,rp,r2p); break;
    case C_c_j:
      imm = (BIT(c,12) << 11) | (BIT(c,11) << 4) | (BITS(c,10,9) << 8) |
            (BIT(c,8) << 10) | (BIT(c,7) << 6) | (BIT(c,6) << 7) |
            (BITS(c,5,3) << 1) | (BIT(c,2) << 5);
      e = jump(SEXT(imm,12)); break;
    case C_c_beqz:
    case C_c_bnez:
      imm = (BIT(c,12) << 8) | (BITS(c,11,10) << 3) |
            (BITS(c,6,5) << 6) | (BITS(c,4,3) << 1) | (BIT(c,2) << 5);
      e = branch(kind == C_c_beqz ? 0 : 1,rp,SEXT(imm,9)); break;
    case C_c_slli: e = imm_op(0x13,1,rd,rd,ci); break;
    case C_c_lwsp:
      if (!rd) return false;
      imm = (BIT(c,12) << 5) | (BITS(c,6,4) << 2) | (BITS(c,3,2) << 6);
      e = imm_op(0x03,2,rd,2,imm); break;
    case C_c_ldsp:
      if (!rd) return false;
      imm = (BIT(c,12) << 5) | (BITS(c,6,5) << 3) | (BITS(c,4,2) << 6);
      e = imm_op(0x03,3,rd,2,imm); break;
    case C_c_jr:
      if (!rd) return false;
      e = imm_op(0x67,0,0,rd,0); break;
    case C_c_mv: e = reg_op(0x33,0,0,rd,0,rs2); break;
    case C_c_ebreak: e = 0x00100073; break;
    case C_c_jalr: e = imm_op(0x67,0,1,rd,0); break;
    case C_c_add: e = reg_op(0x33,0,0,rd,rd,rs2); break;
    case C_c_swsp:
      imm = (BITS(c,12,9) << 2) | (BITS(c,8,7) << 6);
      e = store(2,2,rs2,imm); break;
    case C_c_sdsp:
      imm = (BITS(c,12,10) << 3) | (BITS(c,9,7) << 6);
      e = store(3,2,rs2,imm); break;
    default: return false;
  }
  s->encoding = e;
  return true;
}

bool riscv_expand_compressed(Decode *s) {
  for (size_t i = 0; i < ARRLEN(entries); i++) {
    if ((s->instr & entries[i].mask) == entries[i].key) {
      s->mnemonic = entries[i].name;
      return expand(s,entries[i].kind);
    }
  }
  return false;
}
