#include <cpu/exec.h>

#include "csr.h"

ExecResult exec_sret(ExecContext* ctx) {
  CpuState* hart = ctx->hart;
  if (hart->priv < PRV_S ||
      (hart->priv == PRV_S && (hart->mstatus & MSTATUS_TSR) != 0)) {
    return exec_trap(EXCP_ILLEGAL_INST, ctx->raw_instr);
  }

  word_t status = hart->mstatus;
  const Priv previous = (status & MSTATUS_SPP) != 0 ? PRV_S : PRV_U;
  status = xret_restore_xie(status, MSTATUS_SIE, MSTATUS_SPIE);
  status &= ~(MSTATUS_SPP | MSTATUS_MPRV);
  hart->mstatus = status;
  hart->priv = previous;
  ctx->npc = hart->sepc;
  return exec_ok();
}

ExecResult exec_sfence_vma(ExecContext* ctx) {
  const CpuState* hart = ctx->hart;
  if (hart->priv < PRV_S ||
      (hart->priv == PRV_S && (hart->mstatus & MSTATUS_TVM) != 0)) {
    return exec_trap(EXCP_ILLEGAL_INST, ctx->raw_instr);
  }
  /* 页表每次访问都重新走，没有需要失效的缓存。 */
  return exec_ok();
}
