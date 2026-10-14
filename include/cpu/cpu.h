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
  reg_t gpr[32];
  freg_t fpr[32];
  vaddr_t pc;
#define RVISS_CSR_MEMBER(_n, _a, _m) word_t _n;
  /* 只有当前 RVISS profile 真正实现的 CSR 才进入架构状态。 */
  RVISS_CSRS_OWNED(RVISS_CSR_MEMBER)
#undef RVISS_CSR_MEMBER


  /* ---- 运行状态 ---- */
  uint8_t priv; /* 当前特权级 */
  /* 以下是 CSR 访问副作用所需的实现状态，不是客户机可见 CSR。 */
  paddr_t reservation_addr;
  uint8_t reservation_length;
  bool reservation_valid;
  uint8_t pmp_count, pmpcfg[16];
  word_t pmpaddr[16];
  bool sv39_supported;
  word_t mip_software;
} CPU_state;

extern CPU_state cpu;

/* 实现于 src/target/riscv64/cpu.c */
void cpu_reset(vaddr_t reset_pc);
const char* cpu_priv_name(uint8_t priv);

/* target 提供的公共入口：注入一次陷阱（difftest 注入异常、monitor 调试用）。
 * 实现于 src/target/riscv64/trap/trap.c。 */
vaddr_t cpu_raise_trap(word_t cause, vaddr_t epc);

#endif /* RVISS_CPU_CPU_H */
