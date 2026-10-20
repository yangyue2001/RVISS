#ifndef RVISS_RISCV64_ENCODING_H
#define RVISS_RISCV64_ENCODING_H

#include <common.h>

/*
 * RV64 指令编码：位域提取 + 各格式立即数。
 * 参考 spec: "RISC-V Instruction Set Manual, Volume I, Chapter 2"。
 */

/* 通用字段 */
#define RD(i) ((uint8_t)BITS(i, 11, 7))
#define RS1(i) ((uint8_t)BITS(i, 19, 15))
#define RS2(i) ((uint8_t)BITS(i, 24, 20))
#define FUNCT3(i) BITS(i, 14, 12)
#define FUNCT7(i) BITS(i, 31, 25)
#define OPCODE(i) BITS(i, 6, 0)

/* 立即数（I/S/B/U/J 五种重排方式） */
#define IMM_I(i) ((sword_t)SEXT(BITS(i, 31, 20), 12))
#define IMM_S(i) ((sword_t)SEXT((BITS(i, 31, 25) << 5) | BITS(i, 11, 7), 12))
#define IMM_B(i)                                                     \
  ((sword_t)SEXT((BITS(i, 31, 31) << 12) | (BITS(i, 7, 7) << 11) |   \
                     (BITS(i, 30, 25) << 5) | (BITS(i, 11, 8) << 1), \
                 13))
#define IMM_U(i) ((sword_t)(int32_t)((uint32_t)(i) & 0xfffff000u))
#define IMM_J(i)                                                       \
  ((sword_t)SEXT((BITS(i, 31, 31) << 20) | (BITS(i, 19, 12) << 12) |   \
                     (BITS(i, 20, 20) << 11) | (BITS(i, 30, 21) << 1), \
                 21))

/* 移位量（shamt，5/6 位） */
#define SHAMT(i) ((int)BITS(i, 25, 20))
/* CSR 立即数形式：zimm[4:0] 放在 rs1 位域，零扩展 */
#define ZIMM5(i) ((word_t)BITS(i, 19, 15))

/* 加载结果的扩展方式：属于 ISA 语义，不属于 RTL */
static inline word_t ext_s8(word_t v) { return (word_t)(sword_t)(int8_t)v; }
static inline word_t ext_s16(word_t v) { return (word_t)(sword_t)(int16_t)v; }
static inline word_t ext_s32(word_t v) { return (word_t)(sword_t)(int32_t)v; }
static inline word_t ext_u8(word_t v) { return (word_t)(uint8_t)v; }
static inline word_t ext_u16(word_t v) { return (word_t)(uint16_t)v; }
static inline word_t ext_u32(word_t v) { return (word_t)(uint32_t)v; }
static inline word_t ext_none(word_t v) { return v; }

#endif /* RVISS_RISCV64_ENCODING_H */
