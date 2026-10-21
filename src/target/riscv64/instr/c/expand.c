#include <common.h>
#include <cpu/decode.h>

enum {
#define CINSTR(name, key, mask) C_##name,
#include "instr.def"
#undef CINSTR
};

static const struct {
  uint16_t key;
  uint16_t mask;
  int kind;
} entries[] = {
#define CINSTR(name, key, mask) {key, mask, C_##name},
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

static uint32_t store(unsigned funct3, unsigned rs1, unsigned rs2,
                      unsigned imm) {
  return ((imm >> 5) << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
         ((imm & 31) << 7) | 0x23;
}

static uint32_t branch(unsigned funct3, unsigned rs1, uint32_t imm) {
  return (((imm >> 12) & 1) << 31) | (((imm >> 5) & 63) << 25) | (rs1 << 15) |
         (funct3 << 12) | (((imm >> 1) & 15) << 8) | (((imm >> 11) & 1) << 7) |
         0x63;
}

static uint32_t jump(uint32_t imm) {
  return (((imm >> 20) & 1) << 31) | (((imm >> 1) & 1023) << 21) |
         (((imm >> 11) & 1) << 20) | (((imm >> 12) & 255) << 12) | 0x6f;
}

static bool expand(uint16_t parcel, int kind, uint32_t* out) {
  const unsigned rd = (unsigned)BITS(parcel, 11, 7);
  const unsigned rs2 = (unsigned)BITS(parcel, 6, 2);
  const unsigned rp = 8 + (unsigned)BITS(parcel, 9, 7);
  const unsigned r2p = 8 + (unsigned)BITS(parcel, 4, 2);
  const unsigned ci = ((unsigned)BIT(parcel, 12) << 5) | (unsigned)BITS(parcel, 6, 2);
  const int32_t simm = (int32_t)SEXT(ci, 6);
  const unsigned word_offset = ((unsigned)BITS(parcel, 12, 10) << 3) |
                               ((unsigned)BIT(parcel, 6) << 2) |
                               ((unsigned)BIT(parcel, 5) << 6);
  const unsigned double_offset = ((unsigned)BITS(parcel, 12, 10) << 3) |
                                 ((unsigned)BITS(parcel, 6, 5) << 6);
  unsigned imm;
  uint32_t expanded;
  switch (kind) {
    case C_c_addi4spn:
      imm = ((unsigned)BITS(parcel, 12, 11) << 4) |
            ((unsigned)BITS(parcel, 10, 7) << 6) |
            ((unsigned)BIT(parcel, 6) << 2) | ((unsigned)BIT(parcel, 5) << 3);
      if (imm == 0) return false;
      expanded = imm_op(0x13, 0, r2p, 2, (int32_t)imm);
      break;
    case C_c_lw:
      expanded = imm_op(0x03, 2, r2p, rp, (int32_t)word_offset);
      break;
    case C_c_ld:
      expanded = imm_op(0x03, 3, r2p, rp, (int32_t)double_offset);
      break;
#ifdef CONFIG_RV_D
    case C_c_fld:
      expanded = imm_op(0x07, 3, r2p, rp, (int32_t)double_offset);
      break;
    case C_c_fsd:
      expanded = (store(3, rp, r2p, double_offset) & ~0x7fu) | 0x27;
      break;
    case C_c_fldsp:
      imm = ((unsigned)BIT(parcel, 12) << 5) |
            ((unsigned)BITS(parcel, 6, 5) << 3) |
            ((unsigned)BITS(parcel, 4, 2) << 6);
      expanded = imm_op(0x07, 3, rd, 2, (int32_t)imm);
      break;
    case C_c_fsdsp:
      imm = ((unsigned)BITS(parcel, 12, 10) << 3) |
            ((unsigned)BITS(parcel, 9, 7) << 6);
      expanded = (store(3, 2, rs2, imm) & ~0x7fu) | 0x27;
      break;
#endif
    case C_c_sw:
      expanded = store(2, rp, r2p, word_offset);
      break;
    case C_c_sd:
      expanded = store(3, rp, r2p, double_offset);
      break;
    case C_c_nop:
      expanded = 0x13;
      break;
    case C_c_addi:
      expanded = imm_op(0x13, 0, rd, rd, simm);
      break;
    case C_c_addiw:
      if (rd == 0) return false;
      expanded = imm_op(0x1b, 0, rd, rd, simm);
      break;
    case C_c_li:
      expanded = imm_op(0x13, 0, rd, 0, simm);
      break;
    case C_c_addi16sp:
      imm = ((unsigned)BIT(parcel, 12) << 9) | ((unsigned)BIT(parcel, 6) << 4) |
            ((unsigned)BIT(parcel, 5) << 6) |
            ((unsigned)BITS(parcel, 4, 3) << 7) | ((unsigned)BIT(parcel, 2) << 5);
      if (imm == 0) return false;
      expanded = imm_op(0x13, 0, 2, 2, (int32_t)SEXT(imm, 10));
      break;
    case C_c_lui:
      if (ci == 0) return false;
      expanded = (((uint32_t)simm & 0xfffff) << 12) | (rd << 7) | 0x37;
      break;
    case C_c_srli:
      expanded = imm_op(0x13, 5, rp, rp, (int32_t)ci);
      break;
    case C_c_srai:
      expanded = imm_op(0x13, 5, rp, rp, (int32_t)(0x400 | ci));
      break;
    case C_c_andi:
      expanded = imm_op(0x13, 7, rp, rp, simm);
      break;
    case C_c_sub:
      expanded = reg_op(0x33, 0, 0x20, rp, rp, r2p);
      break;
    case C_c_xor:
      expanded = reg_op(0x33, 4, 0, rp, rp, r2p);
      break;
    case C_c_or:
      expanded = reg_op(0x33, 6, 0, rp, rp, r2p);
      break;
    case C_c_and:
      expanded = reg_op(0x33, 7, 0, rp, rp, r2p);
      break;
    case C_c_subw:
      expanded = reg_op(0x3b, 0, 0x20, rp, rp, r2p);
      break;
    case C_c_addw:
      expanded = reg_op(0x3b, 0, 0, rp, rp, r2p);
      break;
    case C_c_j:
      imm = ((unsigned)BIT(parcel, 12) << 11) | ((unsigned)BIT(parcel, 11) << 4) |
            ((unsigned)BITS(parcel, 10, 9) << 8) | ((unsigned)BIT(parcel, 8) << 10) |
            ((unsigned)BIT(parcel, 7) << 6) | ((unsigned)BIT(parcel, 6) << 7) |
            ((unsigned)BITS(parcel, 5, 3) << 1) | ((unsigned)BIT(parcel, 2) << 5);
      expanded = jump((uint32_t)SEXT(imm, 12));
      break;
    case C_c_beqz:
    case C_c_bnez:
      imm = ((unsigned)BIT(parcel, 12) << 8) |
            ((unsigned)BITS(parcel, 11, 10) << 3) |
            ((unsigned)BITS(parcel, 6, 5) << 6) |
            ((unsigned)BITS(parcel, 4, 3) << 1) | ((unsigned)BIT(parcel, 2) << 5);
      expanded = branch(kind == C_c_beqz ? 0 : 1, rp, (uint32_t)SEXT(imm, 9));
      break;
    case C_c_slli:
      expanded = imm_op(0x13, 1, rd, rd, (int32_t)ci);
      break;
    case C_c_lwsp:
      if (rd == 0) return false;
      imm = ((unsigned)BIT(parcel, 12) << 5) |
            ((unsigned)BITS(parcel, 6, 4) << 2) |
            ((unsigned)BITS(parcel, 3, 2) << 6);
      expanded = imm_op(0x03, 2, rd, 2, (int32_t)imm);
      break;
    case C_c_ldsp:
      if (rd == 0) return false;
      imm = ((unsigned)BIT(parcel, 12) << 5) |
            ((unsigned)BITS(parcel, 6, 5) << 3) |
            ((unsigned)BITS(parcel, 4, 2) << 6);
      expanded = imm_op(0x03, 3, rd, 2, (int32_t)imm);
      break;
    case C_c_jr:
      if (rd == 0) return false;
      expanded = imm_op(0x67, 0, 0, rd, 0);
      break;
    case C_c_mv:
      expanded = reg_op(0x33, 0, 0, rd, 0, rs2);
      break;
    case C_c_ebreak:
      expanded = 0x00100073;
      break;
    case C_c_jalr:
      expanded = imm_op(0x67, 0, 1, rd, 0);
      break;
    case C_c_add:
      expanded = reg_op(0x33, 0, 0, rd, rd, rs2);
      break;
    case C_c_swsp:
      imm = ((unsigned)BITS(parcel, 12, 9) << 2) |
            ((unsigned)BITS(parcel, 8, 7) << 6);
      expanded = store(2, 2, rs2, imm);
      break;
    case C_c_sdsp:
      imm = ((unsigned)BITS(parcel, 12, 10) << 3) |
            ((unsigned)BITS(parcel, 9, 7) << 6);
      expanded = store(3, 2, rs2, imm);
      break;
    default:
      return false;
  }
  *out = expanded;
  return true;
}

bool riscv_expand_compressed(uint16_t bits, uint32_t* instr) {
  Assert(instr != NULL, "压缩指令展开缺少输出");
  for (size_t i = 0; i < ARRLEN(entries); i++) {
    if ((bits & entries[i].mask) == entries[i].key) {
      return expand(bits, entries[i].kind, instr);
    }
  }
  return false;
}
