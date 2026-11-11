#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <cpu/step.h>
#include <difftest.h>
#include <memory/paddr.h>
#include <util/state.h>

#include "../../machine/machine.h"
#include "csr.h"
#include "trap.h"

/*
 * 外部 difftest 看到的是一份扁平寄存器镜像。
 * 写入不走 CSR 指令的 WARL：对方要的是把已有体系结构状态对齐过来。
 * 执行只调用 riscv_execute，不走 machine_update，因此设备节拍和
 * 异步中断都不会自己发生。
 */

static word_t ref_peek(word_t addr) {
  word_t value = 0;
  if (!csr_peek(&cpu, addr, &value)) return 0;
  return value;
}

static void ref_export(RvissRefState* out) {
  memset(out, 0, sizeof(*out));
  for (int i = 0; i < 32; i++) {
    out->gpr[i] = cpu.gpr[i];
    out->fpr[i] = cpu.fpr[i];
  }
  out->gpr[0] = 0;
  out->fcsr = ref_peek(CSR_FCSR);
  out->pc = cpu.pc;
  out->priv = (uint64_t)cpu.priv;
  out->mstatus = ref_peek(CSR_MSTATUS);
  out->medeleg = ref_peek(CSR_MEDELEG);
  out->mideleg = ref_peek(CSR_MIDELEG);
  out->mie = ref_peek(CSR_MIE);
  out->mip = ref_peek(CSR_MIP);
  out->mtvec = ref_peek(CSR_MTVEC);
  out->mscratch = ref_peek(CSR_MSCRATCH);
  out->mepc = ref_peek(CSR_MEPC);
  out->mcause = ref_peek(CSR_MCAUSE);
  out->mtval = ref_peek(CSR_MTVAL);
  out->mcounteren = ref_peek(CSR_MCOUNTEREN);
  out->mcountinhibit = ref_peek(CSR_MCOUNTINHIBIT);
  out->mcycle = ref_peek(CSR_MCYCLE);
  out->minstret = ref_peek(CSR_MINSTRET);
  out->mhartid = ref_peek(CSR_MHARTID);
  out->stvec = ref_peek(CSR_STVEC);
  out->scounteren = ref_peek(CSR_SCOUNTEREN);
  out->sscratch = ref_peek(CSR_SSCRATCH);
  out->sepc = ref_peek(CSR_SEPC);
  out->scause = ref_peek(CSR_SCAUSE);
  out->stval = ref_peek(CSR_STVAL);
  out->satp = ref_peek(CSR_SATP);
  for (int i = 0; i < CPU_PMP_ENTRIES; i++) {
    if ((unsigned)i >= cpu.pmp_count) break;
    out->pmpcfg[i] = cpu.pmpcfg[i];
    out->pmpaddr[i] = cpu.pmpaddr[i];
  }
}

static void ref_import(const RvissRefState* in) {
  Assert(in->priv == PRV_U || in->priv == PRV_S || in->priv == PRV_M,
         "difftest priv 只能是 U/S/M，收到 %lu", (unsigned long)in->priv);
  for (int i = 0; i < 32; i++) {
    cpu.gpr[i] = in->gpr[i];
    cpu.fpr[i] = in->fpr[i];
  }
  cpu.gpr[0] = 0;
  cpu.fcsr = in->fcsr & (FCSR_FFLAGS | FCSR_FRM);
  cpu.pc = in->pc;
  cpu.priv = (Priv)in->priv;
  cpu.mstatus = in->mstatus;
  cpu.medeleg = in->medeleg;
  cpu.mideleg = in->mideleg;
  cpu.mie = in->mie;
  cpu.mip = in->mip;
  cpu.mtvec = in->mtvec;
  cpu.mscratch = in->mscratch;
  cpu.mepc = in->mepc & CSR_EPC_MASK;
  cpu.mcause = in->mcause;
  cpu.mtval = in->mtval;
  cpu.mcounteren = in->mcounteren;
  cpu.mcountinhibit = in->mcountinhibit;
  cpu.mcycle = in->mcycle;
  cpu.minstret = in->minstret;
  cpu.mhartid = in->mhartid;
  cpu.stvec = in->stvec;
  cpu.scounteren = in->scounteren;
  cpu.sscratch = in->sscratch;
  cpu.sepc = in->sepc & CSR_EPC_MASK;
  cpu.scause = in->scause;
  cpu.stval = in->stval;
  cpu.satp = in->satp;
  for (int i = 0; i < CPU_PMP_ENTRIES; i++) {
    if ((unsigned)i >= cpu.pmp_count) break;
    cpu.pmpcfg[i] = (uint8_t)in->pmpcfg[i];
    cpu.pmpaddr[i] = in->pmpaddr[i] & PMPADDR_ADDR_RV64;
  }
}

static void ref_take(word_t cause, word_t tval) {
  cpu.wfi_sleep = false;
  cpu.pc = riscv_raise_trap(cause, cpu.pc, tval);
}

void difftest_init(void) {
  MachineConfig cfg = {
      .name = "ref",
      .ram_base = CONFIG_MBASE,
      .ram_size = CONFIG_MSIZE,
      .reset_pc = CONFIG_MBASE + CONFIG_PC_RESET_OFFSET,
      .nr_node = 0,
  };
  sim_reset();
  riscv_decode_init();
  machine_init(&cfg);
}

void difftest_memcpy(uint64_t addr, void* buf, size_t n, bool direction) {
  if (n == 0) return;
  Assert(buf != NULL, "difftest_memcpy 缓冲区为空");
  Assert(mem_in_ram(addr, n),
         "difftest_memcpy 越出 RAM: " FMT_PADDR " 长度 %zu", addr, n);
  uint8_t* host = mem_host_ptr(addr);
  if (direction == DIFFTEST_TO_REF) {
    memcpy(host, buf, n);
  } else {
    memcpy(buf, host, n);
  }
}

void difftest_regcpy(void* dut, bool direction) {
  Assert(dut != NULL, "difftest_regcpy 缓冲区为空");
  if (direction == DIFFTEST_TO_REF) {
    ref_import(dut);
  } else {
    ref_export(dut);
  }
}

void difftest_exec(uint64_t n) {
  for (uint64_t stepped = 0; stepped < n; stepped++) {
    if (!sim_running() || cpu.wfi_sleep) return;
    riscv_execute();
  }
}

void difftest_raise_trap(uint64_t cause, uint64_t tval) {
  Assert((cause & ~MCAUSE_INTERRUPT) < 64,
         "difftest_raise_trap 原因码越界: %lu", (unsigned long)cause);
  ref_take(cause, tval);
}
