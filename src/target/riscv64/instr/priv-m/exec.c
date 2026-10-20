#include <cpu/exec.h>

static ExecResult exec_mret(ExecContext* ctx) {
  CpuState* hart = ctx->hart;
  bool exception = hart->priv < PRV_M;
  if (!exception) {
    // mie <- mpie
    // mpie <- 1
    reg_t mstatus = hart->mstatus;
    mstatus = xret_restore_xie(mstatus, MSTATUS_MIE, MSTATUS_MPIE);
    // mpp <- u
    mstatus &= ~MSTATUS_MPP;
    mstatus |= MSTATUS_MPP_U;

    // mpp != m  clear mprv
  }

  return exception ? exec_trap(EXCP_ILLEGAL_INST, ctx->raw_instr) : exec_ok();
}