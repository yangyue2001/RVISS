#include "trap.h"

#include <common.h>
#include <cpu/cpu.h>
#include <util/log.h>

#include "csr.h"

/* trap 目标只由 ISA 决定；设备不读取 CPU 的私有 CSR。 */
vaddr_t riscv_raise_trap(word_t cause, vaddr_t epc, vaddr_t tval) {
  bool is_intr = (cause & MCAUSE_INTERRUPT) != 0;
  word_t code = cause & MCAUSE_CODE;
  word_t delegation = is_intr ? cpu.mideleg : cpu.medeleg;
  bool to_s = cpu.priv <= PRV_S && code < 64 && ((delegation >> code) & 1);
  word_t status = cpu.mstatus;
  word_t vector;
  if (to_s) {
    cpu.sepc = epc & CSR_EPC_MASK;
    cpu.scause = cause;
    cpu.stval = tval;
    status = (status & ~(MSTATUS_SPP | MSTATUS_SPIE)) |
             (cpu.priv == PRV_S ? MSTATUS_SPP : 0) |
             ((status & MSTATUS_SIE) ? MSTATUS_SPIE : 0);
    status &= ~MSTATUS_SIE;
    vector = cpu.stvec;
    cpu.priv = PRV_S;
  } else {
    cpu.mepc = epc & CSR_EPC_MASK;
    cpu.mcause = cause;
    cpu.mtval = tval;
    status = (status & ~(MSTATUS_MPP | MSTATUS_MPIE)) |
             ((word_t)cpu.priv << 11) |
             ((status & MSTATUS_MIE) ? MSTATUS_MPIE : 0);
    status &= ~MSTATUS_MIE;
    vector = cpu.mtvec;
    cpu.priv = PRV_M;
  }
  cpu.mstatus = status;
  vaddr_t target = vector & ~3ull;
  if ((vector & 3) == 1 && is_intr) target += 4 * code;
  LogTrap("trap: %s=%lu epc=" FMT_WORD " -> " FMT_WORD " (priv -> %s)",
          is_intr ? "intr" : "exc", code, epc, target, cpu_priv_name(cpu.priv));
  return target;
}
