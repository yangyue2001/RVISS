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
typedef enum { PRV_U = 0, PRV_S = 1, PRV_M = 3 } Priv;

/* 当前 profile 实现 16 个 PMP entry。0 表示这次运行不启用 PMP。 */
enum { CPU_PMP_ENTRIES = 16 };

typedef struct {
  reg_t gpr[32];
  freg_t fpr[32];
  word_t fcsr;
  vaddr_t pc;
  /* ---- 运行状态 ---- */
  Priv priv; /* 当前特权级 */
  reg_t satp;
  reg_t mstatus;
  reg_t misa;
  reg_t mhartid;
  reg_t medeleg;
  reg_t mideleg;
  reg_t mie;
  reg_t mip;
  reg_t mip_software;
  reg_t mtvec;
  reg_t mscratch;
  reg_t mepc;
  reg_t mcause;
  reg_t mtval;
  reg_t mcounteren;
  reg_t mcountinhibit;
  reg_t mcycle;
  reg_t minstret;
  reg_t stvec;
  reg_t scounteren;
  reg_t sscratch;
  reg_t sepc;
  reg_t scause;
  reg_t stval;
  paddr_t reservation_addr;
  uint8_t reservation_len;
  bool reservation_valid;
  /* CSR 描述符还没接管 PMP。访问检查和 pmpcfg/pmpaddr 读写都使用这里。 */
  uint8_t pmp_count;
  uint8_t pmpcfg[CPU_PMP_ENTRIES];
  word_t pmpaddr[CPU_PMP_ENTRIES];
} CpuState;

extern CpuState cpu;

/* 实现于 src/target/riscv64/cpu.c */
void cpu_reset(vaddr_t reset_pc);
const char* cpu_priv_name(Priv priv);

/* target 提供的公共入口：注入一次陷阱（difftest 注入异常、monitor 调试用）。
 * 实现于 src/target/riscv64/trap/trap.c。 */
vaddr_t cpu_raise_trap(word_t cause, vaddr_t epc);

#endif /* RVISS_CPU_CPU_H */
