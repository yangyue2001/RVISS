#ifndef RVISS_RISCV64_RTL_H
#define RVISS_RISCV64_RTL_H

#include <common.h>
#include <rtl/rtl.h>

#include "encoding.h"

/*
 * RV64 私有的 RTL 组合原语。
 *
 * 通用原语（rtl_add / rtl_lm …）在 include/rtl/rtl.h，是引擎与 target 的契约；
 * 这里放"只对 RISC-V 有意义"的组合操作 —— 主要是 32 位字运算（*W 指令）。
 * 这样通用 RTL 层不会出现 addw 这类 ISA 概念（NEMU 也是把 *w 系列放在 ISA
 * 侧）。
 *
 * ⚠ 几个容易写错的地方（都有单测覆盖，见 tests/unit/test_rv64w.c）：
 *   - *W 的移位量是 rs2[4:0]（& 31），不是 64 位指令的 & 63；
 *     直接复用 rtl_sll（&63）在 rs2 = 32 时会得到完全错误的结果。
 *   - sraw/sraiw 不能复用 64 位算术右移后截断：
 *     64 位 ASR 的符号位来自 bit63，而 32 位 ASR 的符号位来自
 * bit31，两者不等价。
 *   - 左移/逻辑右移则相反：低 32 位只取决于低 32 位，可以先算 64 位再截断。
 */

/* ---------------- 寄存器-寄存器（R 型） ---------------- */

static inline void rtl_addw(rtlreg_t* d, const rtlreg_t* s1,
                            const rtlreg_t* s2) {
  rtl_add(d, s1, s2);
  *d = ext_s32(*d); /* 结果按 32 位截断并符号扩展 */
}

static inline void rtl_subw(rtlreg_t* d, const rtlreg_t* s1,
                            const rtlreg_t* s2) {
  rtl_sub(d, s1, s2);
  *d = ext_s32(*d);
}

static inline void rtl_sllw(rtlreg_t* d, const rtlreg_t* s1,
                            const rtlreg_t* s2) {
  const int shamt = (int)(*s2 & 31);
  *d = ext_s32((word_t)((uint32_t)*s1 << shamt));
}

static inline void rtl_srlw(rtlreg_t* d, const rtlreg_t* s1,
                            const rtlreg_t* s2) {
  const int shamt = (int)(*s2 & 31);
  *d = ext_s32((word_t)((uint32_t)*s1 >> shamt));
}

static inline void rtl_sraw(rtlreg_t* d, const rtlreg_t* s1,
                            const rtlreg_t* s2) {
  const int shamt = (int)(*s2 & 31);
  *d = ext_s32((word_t)((sword_t)(int32_t)*s1 >> shamt));
}

/* ---------------- 立即数（I 型） ---------------- */

static inline void rtl_addiw(rtlreg_t* d, const rtlreg_t* s1, sword_t imm) {
  rtl_addi(d, s1, imm);
  *d = ext_s32(*d);
}

static inline void rtl_slliw(rtlreg_t* d, const rtlreg_t* s1, int shamt) {
  *d = ext_s32((word_t)((uint32_t)*s1 << (shamt & 31)));
}

static inline void rtl_srliw(rtlreg_t* d, const rtlreg_t* s1, int shamt) {
  *d = ext_s32((word_t)((uint32_t)*s1 >> (shamt & 31)));
}

static inline void rtl_sraiw(rtlreg_t* d, const rtlreg_t* s1, int shamt) {
  *d = ext_s32((word_t)((sword_t)(int32_t)*s1 >> (shamt & 31)));
}

#endif /* RVISS_RISCV64_RTL_H */
