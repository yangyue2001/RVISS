#include <cpu/exec.h>

#include "csr.h"

ExecResult exec_mret(ExecContext* ctx) {
  CpuState* hart = ctx->hart;
  if (hart->priv != PRV_M) return exec_trap(EXCP_ILLEGAL_INST, ctx->raw_instr);

  word_t status = csr_status_value(hart->mstatus);
  const Priv previous = (Priv)BITS(status, 12, 11);
  status = xret_restore_xie(status, MSTATUS_MIE, MSTATUS_MPIE);
  status = (status & ~MSTATUS_MPP) | MSTATUS_MPP_U;
  if (previous != PRV_M) status &= ~MSTATUS_MPRV;
  hart->mstatus = csr_status_value(status);
  hart->priv = previous;
  ctx->npc = csr_epc_read_value(hart->mepc);
  return exec_ok();
}

ExecResult exec_wfi(ExecContext* ctx) {
  const CpuState* hart = ctx->hart;
  if (hart->priv == PRV_U ||
      (hart->priv != PRV_M && (hart->mstatus & MSTATUS_TW) != 0)) {
    return exec_trap(EXCP_ILLEGAL_INST, ctx->raw_instr);
  }
  return exec_ok();
}
