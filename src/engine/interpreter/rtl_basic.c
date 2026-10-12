#include <common.h>
#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <memory/vaddr.h>
#include <rtl/rtl.h>

/*
 * RTL 原语的解释器实现（引擎侧）。
 * 这些函数就是"客户机指令在宿主上的执行方式"——将来写 JIT 引擎时，
 * 换的是这个文件，target 里的语义描述不动。
 */

void rtl_li(rtlreg_t* d, word_t imm) { *d = imm; }
void rtl_mv(rtlreg_t* d, const rtlreg_t* s1) { *d = *s1; }

void rtl_add(rtlreg_t* d, const rtlreg_t* s1, const rtlreg_t* s2) {
  *d = *s1 + *s2;
}
void rtl_sub(rtlreg_t* d, const rtlreg_t* s1, const rtlreg_t* s2) {
  *d = *s1 - *s2;
}
void rtl_and(rtlreg_t* d, const rtlreg_t* s1, const rtlreg_t* s2) {
  *d = *s1 & *s2;
}
void rtl_or(rtlreg_t* d, const rtlreg_t* s1, const rtlreg_t* s2) {
  *d = *s1 | *s2;
}
void rtl_xor(rtlreg_t* d, const rtlreg_t* s1, const rtlreg_t* s2) {
  *d = *s1 ^ *s2;
}

/* RV64 移位：移位量只取低 6 位 */
void rtl_sll(rtlreg_t* d, const rtlreg_t* s1, const rtlreg_t* s2) {
  *d = *s1 << (*s2 & 63);
}
void rtl_srl(rtlreg_t* d, const rtlreg_t* s1, const rtlreg_t* s2) {
  *d = *s1 >> (*s2 & 63);
}
void rtl_sra(rtlreg_t* d, const rtlreg_t* s1, const rtlreg_t* s2) {
  *d = (word_t)((sword_t)*s1 >> (*s2 & 63));
}

void rtl_slt(rtlreg_t* d, const rtlreg_t* s1, const rtlreg_t* s2) {
  *d = ((sword_t)*s1 < (sword_t)*s2) ? 1 : 0;
}
void rtl_sltu(rtlreg_t* d, const rtlreg_t* s1, const rtlreg_t* s2) {
  *d = (*s1 < *s2) ? 1 : 0;
}

void rtl_addi(rtlreg_t* d, const rtlreg_t* s1, sword_t imm) {
  *d = *s1 + (word_t)imm;
}
void rtl_andi(rtlreg_t* d, const rtlreg_t* s1, sword_t imm) {
  *d = *s1 & (word_t)imm;
}
void rtl_ori(rtlreg_t* d, const rtlreg_t* s1, sword_t imm) {
  *d = *s1 | (word_t)imm;
}
void rtl_xori(rtlreg_t* d, const rtlreg_t* s1, sword_t imm) {
  *d = *s1 ^ (word_t)imm;
}
void rtl_slti(rtlreg_t* d, const rtlreg_t* s1, sword_t imm) {
  *d = ((sword_t)*s1 < imm) ? 1 : 0;
}
void rtl_sltui(rtlreg_t* d, const rtlreg_t* s1, sword_t imm) {
  *d = (*s1 < (word_t)imm) ? 1 : 0;
}

void rtl_slli(rtlreg_t* d, const rtlreg_t* s1, int shamt) {
  *d = *s1 << (shamt & 63);
}
void rtl_srli(rtlreg_t* d, const rtlreg_t* s1, int shamt) {
  *d = *s1 >> (shamt & 63);
}
void rtl_srai(rtlreg_t* d, const rtlreg_t* s1, int shamt) {
  *d = (word_t)((sword_t)*s1 >> (shamt & 63));
}

static bool record_access(Decode *s, MemAccess access) {
  s->memory = access;
  if (access.status == MEM_HOST_ERROR) s->host_error = true;
  if (access.status == MEM_FAULT) {
    riscv_exec_trap(s, access.cause, access.tval);
  }
  return access.status == MEM_OK;
}

bool rtl_lm(Decode* s, const rtlreg_t* addr, sword_t offset, int len, word_t *data) {
  MemAccess access = vaddr_read(*addr + (word_t)offset, len);
  if (!record_access(s, access)) return false;
  *data = access.data;
  return true;
}

bool rtl_sm(Decode* s, const rtlreg_t* addr, sword_t offset, int len,
            const rtlreg_t* src) {
  return record_access(s, vaddr_write(*addr + (word_t)offset, len, *src));
}

bool rtl_j(Decode* s, vaddr_t target) {
  return riscv_jump(s, target);
}
