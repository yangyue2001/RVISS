#include <common.h>
#include <cpu/decode.h>
#include <rtl/rtl.h>

#include "cpu_state.h"
#include "encoding.h"
#include "rtl.h" /* RV64 私有组合原语（*W 系列） */

/*
 * RV64I 指令语义。
 * 每个函数名 = exec_<instr.def 里的名字>，由 decode.c 自动取用。
 * 语义只允许用 include/rtl/rtl.h 里的原语描述（引擎无关）。
 */

/* ---------------- R 型 ---------------- */
#define EXEC_R(name, rtl_fn)                          \
  void concat(exec_, name)(Decode * s) {              \
    rtl_fn(s->dest.preg, s->src1.preg, s->src2.preg); \
  }

EXEC_R(add, rtl_add)
EXEC_R(sub, rtl_sub)
EXEC_R(sll, rtl_sll)
EXEC_R(slt, rtl_slt)
EXEC_R(sltu, rtl_sltu)
EXEC_R(xor, rtl_xor)
EXEC_R(srl, rtl_srl)
EXEC_R(sra, rtl_sra)
EXEC_R(or, rtl_or)
EXEC_R(and, rtl_and)

/* ---------------- I 型（算术立即数） ---------------- */
#define EXEC_I(name, rtl_fn)                         \
  void concat(exec_, name)(Decode * s) {             \
    rtl_fn(s->dest.preg, s->src1.preg, s->src2.imm); \
  }

EXEC_I(addi, rtl_addi)
EXEC_I(slti, rtl_slti)
EXEC_I(sltiu, rtl_sltui)
EXEC_I(xori, rtl_xori)
EXEC_I(ori, rtl_ori)
EXEC_I(andi, rtl_andi)

/* ---------------- X 型（移位立即数） ---------------- */
#define EXEC_X(name, rtl_fn)                              \
  void concat(exec_, name)(Decode * s) {                  \
    rtl_fn(s->dest.preg, s->src1.preg, (int)s->src2.imm); \
  }

EXEC_X(slli, rtl_slli)
EXEC_X(srli, rtl_srli)
EXEC_X(srai, rtl_srai)

/* ---------------- 32 位字运算（*W） ----------------
 * 语义细节（移位量 &31、算术右移不能用 64 位版本）封装在 local_include/rtl.h，
 * 并有单测覆盖。 */
EXEC_R(addw, rtl_addw)
EXEC_R(subw, rtl_subw)
EXEC_R(sllw, rtl_sllw)
EXEC_R(srlw, rtl_srlw)
EXEC_R(sraw, rtl_sraw)

EXEC_I(addiw, rtl_addiw)
EXEC_X(slliw, rtl_slliw)
EXEC_X(srliw, rtl_srliw)
EXEC_X(sraiw, rtl_sraiw)

/* ---------------- fence ----------------
 * 单 hart 顺序执行的功能模型里无需真正排序/刷缓存，按 nop 处理。
 * （fence.i 在实现译码缓存后需要演变成"刷新缓存"，见 TODO(M5)。） */
void exec_fence(Decode* s) { (void)s; }
void exec_fence_i(Decode* s) { (void)s; }

/* ---------------- 加载 ----------------
 * rtl_lm 返回零扩展的原始字节，符号/零扩展是 ISA 语义，在这里做。 */
#define EXEC_LOAD(name, len, ext)                         \
  void concat(exec_, name)(Decode * s) {                  \
    word_t v = 0;                                        \
    if (rtl_lm(s, s->src1.preg, s->src2.imm, len, &v))   \
      *s->dest.preg = ext(v);                             \
  }

EXEC_LOAD(lb, 1, ext_s8)
EXEC_LOAD(lh, 2, ext_s16)
EXEC_LOAD(lw, 4, ext_s32)
EXEC_LOAD(ld, 8, ext_none)
EXEC_LOAD(lbu, 1, ext_u8)
EXEC_LOAD(lhu, 2, ext_u16)
EXEC_LOAD(lwu, 4, ext_u32)

/* ---------------- 存储 ---------------- */
#define EXEC_STORE(name, len)                                \
  void concat(exec_, name)(Decode * s) {                     \
    rtl_sm(s, s->src1.preg, s->dest.imm, len, s->src2.preg); \
  }

EXEC_STORE(sb, 1)
EXEC_STORE(sh, 2)
EXEC_STORE(sw, 4)
EXEC_STORE(sd, 8)

/* ---------------- 分支 ---------------- */
#define EXEC_BRANCH(name, cond)                      \
  void concat(exec_, name)(Decode * s) {             \
    if (cond) rtl_j(s, s->pc + (word_t)s->dest.imm); \
  }

EXEC_BRANCH(beq, (*s->src1.preg == *s->src2.preg))
EXEC_BRANCH(bne, (*s->src1.preg != *s->src2.preg))
EXEC_BRANCH(blt, ((sword_t)*s->src1.preg < (sword_t)*s->src2.preg))
EXEC_BRANCH(bge, ((sword_t)*s->src1.preg >= (sword_t)*s->src2.preg))
EXEC_BRANCH(bltu, (*s->src1.preg < *s->src2.preg))
EXEC_BRANCH(bgeu, (*s->src1.preg >= *s->src2.preg))

/* ---------------- 高位立即数 / 跳转 ---------------- */
void exec_lui(Decode* s) { rtl_li(s->dest.preg, (word_t)s->src1.imm); }

void exec_auipc(Decode* s) {
  rtl_li(s->dest.preg, s->pc + (word_t)s->src1.imm);
}

void exec_jal(Decode* s) {
  if (rtl_j(s, s->pc + (word_t)s->src1.imm)) {
    rtl_li(s->dest.preg, s->snpc);
  }
}

void exec_jalr(Decode* s) {
  /* 先算目标再写 rd：jalr 允许 rd == rs1 */
  vaddr_t target = (*s->src1.preg + (word_t)s->src2.imm) & ~1ull;
  if (rtl_j(s, target)) rtl_li(s->dest.preg, s->snpc);
}
