#ifndef RVISS_CPU_DECODE_H
#define RVISS_CPU_DECODE_H

#include <common.h>
#include <cpu/exec.h>
#include <memory/vaddr.h>
/*
 * 译码结果 + 指令元信息表。
 * 这是"引擎"和"target"共享的少量数据结构之一（指令执行接口）。
 */

/* 一个操作数：要么是一个寄存器指针，要么是一个立即数 */
typedef struct {
  uint8_t ilen; /* 2 或 4 */
  bool illegal;
  EHelper exec; /* 执行入口 */
  ExecOperands operands;
} Decode;

/*
 * 指令元信息：一条指令一行，由各扩展目录下的 instr.def 经 X-macro 生成。
 * pattern 是 32 个字符的位模式（'0'/'1'/'?'），如
 *   "0000000 ????? ????? 000 ????? 01100 11"   (add)
 * 这是"一条指令只描述一遍"的落点：译码表、名字、执行函数全从这里来。
 */
typedef struct {
  const char* name;
  const char* pattern;
  uint64_t key, mask; /* pattern 编译后（decode 初始化时生成） */
  uint8_t shift;
  char fmt; /* 操作数格式：R/I/X/S/B/U/J/N */
  EHelper exec;
} InstrEntry;

/* target/riscv64/decode.c 提供 */
bool riscv_decode(Decode* s); /* false：非法编码或取指故障，见 trap_pending */
void riscv_decode_init(void); /* 预编译所有 pattern */
void riscv_exec_trap(Decode* s, word_t cause, vaddr_t tval);
bool riscv_jump(Decode* s, vaddr_t target);
bool riscv_expand_compressed(Decode* s);

#endif /* RVISS_CPU_DECODE_H */
