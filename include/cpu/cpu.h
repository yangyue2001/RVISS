#ifndef RVISS_CPU_CPU_H
#define RVISS_CPU_CPU_H

#include <common.h>
#include <cpu/csr_list.h> /* CSR 清单：CPU_state 的 CSR 存储由它生成 */

/*
 * 体系结构状态。
 *
 * 为什么放在 include/ 而不是 target 私有头里：
 * 引擎需要读写 pc（调用 exec 前推进、跳转指令改写），所以 cpu 是
 * 引擎与 target 共享的数据结构——NEMU 也是这么做的（include/isa.h
 * 里 extern CPU_state cpu）。RVISS 只有 RV64，所以这里不搞 ISA 抽象。
 */

/* 特权级编码与 mstatus.MPP 一致：U=0, S=1, M=3 */
enum { PRV_U = 0, PRV_S = 1, PRV_M = 3 };

typedef struct {
  rtlreg_t gpr[32];
  vaddr_t  pc;

  /*
   * CSR 存储：由 include/cpu/csr_list.h 的清单生成（视图 CSR 不占存储）。
   * 「加一个 CSR」只需改那份清单一处 —— 存储成员、访问表、日志名一起有。
   */
#define RVISS_CSR_MEMBER(_n, _a, _m) word_t _n;
  RVISS_CSRS_OWNED(RVISS_CSR_MEMBER)
#undef RVISS_CSR_MEMBER

  /* ---- 运行状态 ---- */
  uint8_t  priv;    /* 当前特权级 */
  uint64_t instret; /* 客户机退休计数；当前尚未接入 counter CSR */
  uint64_t cycle;   /* 客户机逻辑周期；不驱动平台时间/预算 */
} CPU_state;

extern CPU_state cpu;

/* 实现于 src/target/riscv64/cpu.c */
void cpu_reset(vaddr_t reset_pc);
const char *cpu_priv_name(uint8_t priv);

/* target 提供的公共入口：注入一次陷阱（difftest 注入异常、monitor 调试用）。
 * 实现于 src/target/riscv64/trap/trap.c。 */
vaddr_t cpu_raise_trap(word_t cause, vaddr_t epc);

#endif /* RVISS_CPU_CPU_H */
