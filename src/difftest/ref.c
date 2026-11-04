#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <cpu/step.h>
#include <difftest.h>
#include <memory/paddr.h>
#include <util/state.h>

#include "../machine/machine.h"
#include "csr.h"
#include "trap.h"

/*
 * 外部 difftest 看到的是一份扁平寄存器镜像。
 * 写入不走 CSR 指令的 WARL：对方要的是把已有体系结构状态对齐过来。
 * 执行只调用 riscv_execute，不走 machine_update，因此设备节拍和
 * 异步中断都不会自己发生。
 */

static bool ref_ready;

static void ref_require(void) { Assert(ref_ready, "difftest_init 还没有调用"); }

static int ref_direction(int direction) {
  Assert(direction == DIFFTEST_TO_DUT || direction == DIFFTEST_TO_REF,
         "difftest direction 只能是 %d 或 %d", DIFFTEST_TO_DUT,
         DIFFTEST_TO_REF);
  return direction;
}

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
  out->lr_valid = cpu.reservation_valid ? 1 : 0;
  out->lr_addr = cpu.reservation_addr;
  out->lr_len = cpu.reservation_len;
  out->wfi = cpu.wfi_sleep ? 1 : 0;
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
  if (in->lr_valid == 0) {
    cpu.reservation_valid = false;
    cpu.reservation_addr = 0;
    cpu.reservation_len = 0;
  } else {
    cpu.reservation_valid = true;
    cpu.reservation_addr = in->lr_addr;
    cpu.reservation_len = (uint8_t)in->lr_len;
  }
  cpu.wfi_sleep = in->wfi != 0;
  cpu.hw_seip = false;
}

static void ref_take(word_t cause, word_t tval) {
  cpu.wfi_sleep = false;
  cpu.pc = riscv_raise_trap(cause, cpu.pc, tval);
}

uint64_t difftest_state_size(void) { return sizeof(RvissRefState); }

uint64_t difftest_ram_base(void) {
  ref_require();
  return mem_base;
}

uint64_t difftest_ram_size(void) {
  ref_require();
  return mem_size;
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
  ref_ready = true;
}

void difftest_memcpy(uint64_t addr, void* buf, size_t n, int direction) {
  ref_require();
  direction = ref_direction(direction);
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

void difftest_regcpy(void* dut, int direction) {
  ref_require();
  Assert(dut != NULL, "difftest_regcpy 缓冲区为空");
  direction = ref_direction(direction);
  if (direction == DIFFTEST_TO_REF) {
    ref_import(dut);
  } else {
    ref_export(dut);
  }
}

void difftest_exec(uint64_t n) {
  ref_require();
  for (uint64_t stepped = 0; stepped < n; stepped++) {
    if (!sim_running() || cpu.wfi_sleep) return;
    riscv_execute();
  }
}

void difftest_raise_trap(uint64_t cause, uint64_t tval) {
  ref_require();
  Assert((cause & ~MCAUSE_INTERRUPT) < 64,
         "difftest_raise_trap 原因码越界: %lu", (unsigned long)cause);
  ref_take(cause, tval);
}
