#ifndef RVISS_RTL_RTL_H
#define RVISS_RTL_RTL_H

#include <common.h>

/*
 * RV64 指令使用的运算/访存原语；执行事件契约见 cpu/step.h。
 *
 * 约定：
 *   - 寄存器一律传指针（dest 传要写入的寄存器，src 传只读寄存器）
 *   - 立即数传值
 *   - 访存原语把 Decode* 带上，因为出错时要能记录 pc/tval
 */

struct Decode;
typedef struct Decode Decode;

/* ---------- 算术 / 逻辑 ---------- */
void rtl_li(rtlreg_t *d, word_t imm);
void rtl_mv(rtlreg_t *d, const rtlreg_t *s1);
void rtl_add(rtlreg_t *d, const rtlreg_t *s1, const rtlreg_t *s2);
void rtl_sub(rtlreg_t *d, const rtlreg_t *s1, const rtlreg_t *s2);
void rtl_and(rtlreg_t *d, const rtlreg_t *s1, const rtlreg_t *s2);
void rtl_or (rtlreg_t *d, const rtlreg_t *s1, const rtlreg_t *s2);
void rtl_xor(rtlreg_t *d, const rtlreg_t *s1, const rtlreg_t *s2);
void rtl_sll(rtlreg_t *d, const rtlreg_t *s1, const rtlreg_t *s2);
void rtl_srl(rtlreg_t *d, const rtlreg_t *s1, const rtlreg_t *s2);
void rtl_sra(rtlreg_t *d, const rtlreg_t *s1, const rtlreg_t *s2);
void rtl_slt(rtlreg_t *d, const rtlreg_t *s1, const rtlreg_t *s2);
void rtl_sltu(rtlreg_t *d, const rtlreg_t *s1, const rtlreg_t *s2);

/* 立即数形式（I 型指令用） */
void rtl_addi(rtlreg_t *d, const rtlreg_t *s1, sword_t imm);
void rtl_andi(rtlreg_t *d, const rtlreg_t *s1, sword_t imm);
void rtl_ori (rtlreg_t *d, const rtlreg_t *s1, sword_t imm);
void rtl_xori(rtlreg_t *d, const rtlreg_t *s1, sword_t imm);
void rtl_slti(rtlreg_t *d, const rtlreg_t *s1, sword_t imm);
void rtl_sltui(rtlreg_t *d, const rtlreg_t *s1, sword_t imm);
void rtl_slli(rtlreg_t *d, const rtlreg_t *s1, int shamt);
void rtl_srli(rtlreg_t *d, const rtlreg_t *s1, int shamt);
void rtl_srai(rtlreg_t *d, const rtlreg_t *s1, int shamt);

/* ---------- 访存 ---------- */
/* 成功才提交读结果；失败记录在 Decode。扩展属于 target 的语义。 */
bool rtl_lm(Decode *s, const rtlreg_t *addr, sword_t offset, int len, word_t *data);
bool rtl_sm(Decode *s, const rtlreg_t *addr, sword_t offset, int len, const rtlreg_t *src);

/* ---------- 控制流 ---------- */
/* target 在调用 exec 前置 pc=snpc；对齐检查通过后才提交跳转。 */
bool rtl_j(Decode *s, vaddr_t target);

#endif /* RVISS_RTL_RTL_H */
