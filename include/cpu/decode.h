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
  const char* fmt; /* 操作数格式名，如 "R"、"I"、"R4" */
  EHelper exec;
} InstrEntry;

/* 只匹配已经取到的 32 位编码。false 表示非法指令。 */
bool riscv_decode(uint32_t instr, Decode* s);
void riscv_decode_init(void);
/* 把 16 位压缩指令展开成等价的 32 位编码。失败是非法指令。 */
bool riscv_expand_compressed(uint16_t bits, uint32_t* instr);

#endif /* RVISS_CPU_DECODE_H */
