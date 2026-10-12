#include <common.h>
#include <util/log.h>

#include "csr.h"
#include "intr.h"
#include "cpu_state.h"

/*
 * 陷阱入口。
 * 当前只实现"M 模式接管一切"（骨架）。M3 会把委派（medeleg/mideleg）加进来，
 * 那时这里变成"选目标特权级"的统一入口，但函数签名不变。
 */

vaddr_t riscv_raise_trap(word_t cause, vaddr_t epc, vaddr_t tval) {
  const bool is_intr = (cause & IRQ_BIT) != 0;
  const word_t code = cause & ~IRQ_BIT;

  /* TODO(M3)：按 medeleg/mideleg 决定进 M 还是 S */
  cpu.mepc = epc;
  cpu.mcause = cause;
  cpu.mtval = tval;

  /* mstatus：MPP ← 当前特权级，MPIE ← MIE，MIE ← 0 */
  cpu.mstatus = (cpu.mstatus & ~MSTATUS_MPP) | ((word_t)cpu.priv << 11);
  cpu.mstatus = (cpu.mstatus & ~MSTATUS_MPIE) |
                ((cpu.mstatus & MSTATUS_MIE) ? MSTATUS_MPIE : 0);
  cpu.mstatus &= ~MSTATUS_MIE;
  cpu.priv = PRV_M;

  /* mtvec：MODE=0 直接模式；MODE=1 向量模式（只有中断偏移） */
  vaddr_t target = cpu.mtvec & ~0x3ull;
  if ((cpu.mtvec & 0x3ull) == 1 && is_intr) {
    target += 4 * code;
  }

  LogTrap("trap: %s=%lu epc=" FMT_WORD " -> " FMT_WORD " (priv -> M)",
          is_intr ? "intr" : "exc", code, epc, target);
  return target;
}

/* mret：M 模式返回，恢复特权级与中断使能 */
vaddr_t riscv_mret(void) {
  const word_t mpp = BITS(cpu.mstatus, 12, 11);

  cpu.mstatus = (cpu.mstatus & ~MSTATUS_MIE) |
                ((cpu.mstatus & MSTATUS_MPIE) ? MSTATUS_MIE : 0);
  cpu.mstatus |= MSTATUS_MPIE;      /* MPIE <- 1 */
  cpu.mstatus &= ~MSTATUS_MPP;      /* MPP <- U */
  cpu.priv = (uint8_t)mpp;

  LogTrap("mret -> pc=" FMT_WORD " priv=%s", cpu.mepc, cpu_priv_name(cpu.priv));
  return cpu.mepc;
}

/* 公共入口：给 difftest/monitor 注入陷阱用（见 include/cpu/cpu.h） */
vaddr_t cpu_raise_trap(word_t cause, vaddr_t epc) {
  return riscv_raise_trap(cause, epc, 0);
}

/* sret：S 模式返回 */
vaddr_t riscv_sret(void) {
  const word_t spp = BITS(cpu.mstatus, 8, 8);

  cpu.mstatus = (cpu.mstatus & ~MSTATUS_SIE) |
                ((cpu.mstatus & MSTATUS_SPIE) ? MSTATUS_SIE : 0);
  cpu.mstatus |= MSTATUS_SPIE;
  cpu.mstatus &= ~MSTATUS_SPP;
  cpu.priv = (uint8_t)spp;

  LogTrap("sret -> pc=" FMT_WORD " priv=%s", cpu.sepc, cpu_priv_name(cpu.priv));
  return cpu.sepc;
}
