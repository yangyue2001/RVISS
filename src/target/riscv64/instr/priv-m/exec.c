#include <cpu/exec.h>
#include <cpu/step.h>

#include "csr.h"

ExecResult exec_mret(ExecContext* ctx) {
  CpuState* hart = ctx->hart;
  if (hart->priv != PRV_M) return exec_trap(EXCP_ILLEGAL_INST, ctx->raw_instr);

  word_t status = hart->mstatus;
  const Priv previous = (Priv)BITS(status, 12, 11);
  status = xret_restore_xie(status, MSTATUS_MIE, MSTATUS_MPIE);
  status = (status & ~MSTATUS_MPP) | MSTATUS_MPP_U;
  if (previous != PRV_M) status &= ~MSTATUS_MPRV;
  hart->mstatus = status;
  hart->priv = previous;
  ctx->npc = hart->mepc;
  return exec_ok();
}

ExecResult exec_wfi(ExecContext* ctx) {
  CpuState* hart = ctx->hart;
  if (hart->priv == PRV_U ||
      (hart->priv != PRV_M && (hart->mstatus & MSTATUS_TW) != 0)) {
    return exec_trap(EXCP_ILLEGAL_INST, ctx->raw_instr);
  }
  /* 已经有单独使能的中断时不等待。全局 MIE/SIE 不参与这次判断。 */
  if (!riscv_wait_resume()) hart->wfi_sleep = true;
  return exec_ok();
}
