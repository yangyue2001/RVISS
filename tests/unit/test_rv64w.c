#include <common.h>
#include <cpu/cpu.h>
#include <cpu/decode.h>

#include "test.h"

/*
 * RV64I 的 32 位字运算（*W）单元测试。
 *
 * 这一组指令有三个经典坑，本测试逐个覆盖：
 *   ① 移位量是 rs2[4:0]（&31），不是 64 位指令的 &63 —— rs2=32 时必须等价于不移位
 *   ② sraw/sraiw 不能复用 64 位算术右移：符号位来源不同（bit31 vs bit63）
 *   ③ 结果必须符号扩展到 64 位（不是零扩展）
 */

/* exec_* 由 decode.c 经 instr.def 声明；这里按定义原样声明以便直接调用 */
void exec_addw(Decode *s);
void exec_subw(Decode *s);
void exec_sllw(Decode *s);
void exec_srlw(Decode *s);
void exec_sraw(Decode *s);
void exec_addiw(Decode *s);
void exec_slliw(Decode *s);
void exec_srliw(Decode *s);
void exec_sraiw(Decode *s);

static Decode d;

#define DST (cpu.gpr[10])

static void set_rr(word_t a, word_t b) {
  memset(&d, 0, sizeof(d));
  d.dest.preg = &cpu.gpr[10];
  d.src1.preg = &cpu.gpr[11];
  d.src2.preg = &cpu.gpr[12];
  cpu.gpr[11] = a;
  cpu.gpr[12] = b;
  DST = 0xdeadbeef; /* 便于发现"没写结果"的情况 */
}

static void set_ri(word_t a, sword_t imm) {
  set_rr(a, 0);
  d.src2.imm = imm;
}

int main(void) {
  cpu_reset(0x80000000ull);

  /* addw：32 位溢出后符号扩展 */
  set_rr(0x7fffffff, 1);
  exec_addw(&d);
  CHECK_EQ(DST, 0xffffffff80000000ull);

  /* addw：高 32 位必须被丢弃 */
  set_rr(0x0000000100000001ull, 1);
  exec_addw(&d);
  CHECK_EQ(DST, 2);

  /* subw：0 - 1 = 0xFFFFFFFF → 符号扩展为 -1 */
  set_rr(0x0000000100000000ull, 1);
  exec_subw(&d);
  CHECK_EQ(DST, 0xffffffffffffffffull);

  /* sllw：rs2 = 32 → 移位量取低 5 位 = 0，结果应等于 rs1（不是 0！） */
  set_rr(1, 32);
  exec_sllw(&d);
  CHECK_EQ(DST, 1);

  /* sllw：0x80000000 << 1 = 0（32 位内） */
  set_rr(0x80000000ull, 1);
  exec_sllw(&d);
  CHECK_EQ(DST, 0);

  /* srlw：逻辑右移只看低 32 位，结果为正 */
  set_rr(0xffffffff80000000ull, 1);
  exec_srlw(&d);
  CHECK_EQ(DST, 0x0000000040000000ull);

  /* srlw：rs2 = 32 → 不移位，结果是低 32 位的符号扩展 */
  set_rr(0xffffffff80000000ull, 32);
  exec_srlw(&d);
  CHECK_EQ(DST, 0xffffffff80000000ull);

  /* sraw：0x80000000 按 32 位看是 -2^31，算术右移 1 位 = 0xC0000000（负数）
   * （若错用 64 位 ASR 会得到正的 0x40000000）*/
  set_rr(0x0000000080000000ull, 1);
  exec_sraw(&d);
  CHECK_EQ(DST, 0xffffffffc0000000ull);

  /* sraw：rs2 = 63 → 移位量 31，全 1 */
  set_rr(0x0000000080000000ull, 63);
  exec_sraw(&d);
  CHECK_EQ(DST, 0xffffffffffffffffull);

  /* addiw */
  set_ri(0x7fffffff, 1);
  exec_addiw(&d);
  CHECK_EQ(DST, 0xffffffff80000000ull);

  /* slliw / srliw / sraiw */
  set_ri(0x80000000ull, 1);
  exec_slliw(&d);
  CHECK_EQ(DST, 0);

  set_ri(0xffffffff80000000ull, 1);
  exec_srliw(&d);
  CHECK_EQ(DST, 0x0000000040000000ull);

  set_ri(0x0000000080000000ull, 1);
  exec_sraiw(&d);
  CHECK_EQ(DST, 0xffffffffc0000000ull);

  TEST_MAIN_END();
}
