#include <common.h>
#include <util/log.h>

#include <cpu/cpu.h>
#include "csr.h"
#include "trap.h"

/* trap 目标只由 ISA 决定；设备不读取 CPU 的私有 CSR。 */
vaddr_t riscv_raise_trap(word_t cause, vaddr_t epc, vaddr_t tval) {
  bool is_intr = (cause & IRQ_BIT) != 0;
  word_t code = cause & ~IRQ_BIT;
  word_t delegation = is_intr ? cpu.mideleg : cpu.medeleg;
  bool to_s = cpu.priv <= PRV_S && code < 64 && ((delegation >> code) & 1);
  word_t status = csr_status_value(cpu.mstatus);
  word_t vector;
  if (to_s) {
    cpu.sepc = csr_epc_storage_value(epc);
    cpu.scause = cause;
    cpu.stval = tval;
    status = (status & ~(MSTATUS_SPP | MSTATUS_SPIE)) |
             (cpu.priv == PRV_S ? MSTATUS_SPP : 0) |
             ((status & MSTATUS_SIE) ? MSTATUS_SPIE : 0);
    status &= ~MSTATUS_SIE;
    vector = cpu.stvec;
    cpu.priv = PRV_S;
  } else {
    cpu.mepc = csr_epc_storage_value(epc);
    cpu.mcause = cause;
    cpu.mtval = tval;
    status = (status & ~(MSTATUS_MPP | MSTATUS_MPIE)) |
             ((word_t)cpu.priv << 11) |
             ((status & MSTATUS_MIE) ? MSTATUS_MPIE : 0);
    status &= ~MSTATUS_MIE;
    vector = cpu.mtvec;
    cpu.priv = PRV_M;
  }
  cpu.mstatus = csr_status_value(status);
  vaddr_t target = vector & ~3ull;
  if ((vector & 3) == 1 && is_intr) target += 4 * code;
  LogTrap("trap: %s=%lu epc=" FMT_WORD " -> " FMT_WORD " (priv -> %s)",
          is_intr ? "intr" : "exc", code, epc, target, cpu_priv_name(cpu.priv));
  return target;
}

/* 调用方先检查指令权限，返回路径读 epc 的当前 IALIGN 视图。 */
vaddr_t riscv_mret(void) {
  word_t status = csr_status_value(cpu.mstatus);
  uint8_t previous = BITS(status, 12, 11);
  vaddr_t target = csr_epc_read_value(cpu.mepc);
  status = (status & ~(MSTATUS_MIE | MSTATUS_MPP)) |
           ((status & MSTATUS_MPIE) ? MSTATUS_MIE : 0) | MSTATUS_MPIE;
  if (previous != PRV_M) status &= ~MSTATUS_MPRV;
  cpu.mstatus = csr_status_value(status);
  cpu.priv = previous;
  return target;
}

vaddr_t riscv_sret(void) {
  word_t status = csr_status_value(cpu.mstatus);
  uint8_t previous = (status & MSTATUS_SPP) ? PRV_S : PRV_U;
  vaddr_t target = csr_epc_read_value(cpu.sepc);
  status = (status & ~(MSTATUS_SIE | MSTATUS_SPP | MSTATUS_MPRV)) |
           ((status & MSTATUS_SPIE) ? MSTATUS_SIE : 0) | MSTATUS_SPIE;
  cpu.mstatus = csr_status_value(status);
  cpu.priv = previous;
  return target;
}

vaddr_t cpu_raise_trap(word_t cause, vaddr_t epc) {
  return riscv_raise_trap(cause, epc, 0);
}
