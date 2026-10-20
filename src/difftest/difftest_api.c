#include <common.h>
#include <cpu/cpu.h>
#include <memory/paddr.h>
#include <util/log.h>
#include <util/state.h>

#include "../engine/engine.h"

/*
 * NEMU 兼容的参考模型 ABI（符号表见 include/rviss.h）。
 * DUT（香山等）通过 dlopen 加载 librviss_ref.so 并调用这些符号。
 *
 * 方向约定：RVISS_DIFFTEST_TO_DUT(0) = ref → DUT；_TO_REF(1) = DUT → ref。
 * 骨架实现只保证 ABI 存在 + 基本语义；store-commit/guided-exec 等高级功能
 * 返回保守值并打日志（见 M7）。
 */

#include <rviss.h>
#include <stdarg.h>
#include <string.h>

void difftest_init(void) {
  LogDiff("difftest_init");
  sim.state = SIM_RUNNING;
}

void difftest_set_ramsize(size_t ram_size) {
  (void)ram_size; /* TODO(M7)：支持非默认 RAM 大小 */
  LogDiff("difftest_set_ramsize(%zu)（骨架忽略）", ram_size);
}

void difftest_set_mhartid(int hartid) { cpu.mhartid = (word_t)hartid; }

static void memcpy_common(uint64_t addr, void* buf, size_t n, bool direction) {
  if (direction == RVISS_DIFFTEST_TO_DUT) {
    memcpy(buf, mem_host_ptr((paddr_t)addr), n);
  } else {
    memcpy(mem_host_ptr((paddr_t)addr), buf, n);
  }
}

void difftest_memcpy(uint64_t addr, void* buf, size_t n, bool direction) {
  memcpy_common(addr, buf, n, direction);
}

void difftest_memcpy_init(uint64_t addr, void* buf, size_t n, bool direction) {
  memcpy_common(addr, buf, n, direction);
}

void difftest_regcpy(void* regs, bool direction, ...) {
  rviss_difftest_regs_t* r = regs;

  if (direction == RVISS_DIFFTEST_TO_DUT) {
    for (int i = 0; i < 32; i++) r->gpr[i] = (uint64_t)cpu.gpr[i];
    r->pc = cpu.pc;
    r->mode = cpu.priv;
    return;
  }

  for (int i = 0; i < 32; i++) cpu.gpr[i] = r->gpr[i];
  cpu.pc = r->pc;
  cpu.priv = (uint8_t)r->mode;
}

void difftest_csrcpy(void* csrs, bool direction) {
  rviss_difftest_regs_t* r = csrs;

  if (direction == RVISS_DIFFTEST_TO_DUT) {
    r->mstatus = cpu.mstatus;
    r->mepc = cpu.mepc;
    r->mcause = cpu.mcause;
    r->mtval = cpu.mtval;
    r->mtvec = cpu.mtvec;
    r->mie = cpu.mie;
    r->mip = cpu.mip;
    r->mscratch = cpu.mscratch;
    r->medeleg = cpu.medeleg;
    r->mideleg = cpu.mideleg;
    r->sepc = cpu.sepc;
    r->scause = cpu.scause;
    r->stval = cpu.stval;
    r->stvec = cpu.stvec;
    r->sscratch = cpu.sscratch;
    r->satp = cpu.satp;
    return;
  }

  cpu.mstatus = r->mstatus;
  cpu.mepc = r->mepc;
  cpu.mcause = r->mcause;
  cpu.mtval = r->mtval;
  cpu.mtvec = r->mtvec;
  cpu.mie = r->mie;
  cpu.mip = r->mip;
  cpu.mip_software = r->mip & SIE_MASK;
  cpu.mscratch = r->mscratch;
  cpu.medeleg = r->medeleg;
  cpu.mideleg = r->mideleg;
  cpu.sepc = r->sepc;
  cpu.scause = r->scause;
  cpu.stval = r->stval;
  cpu.stvec = r->stvec;
  cpu.sscratch = r->sscratch;
  cpu.satp = r->satp;
}

/* legacy n 是指令尝试，不是退休或执行事件数。完整 ABI 可用性另行验收。 */
void difftest_exec(uint64_t n) { engine_exec(n); }

void difftest_raise_intr(uint64_t cause) { engine_raise_trap((word_t)cause); }

void difftest_uarchstatus_sync(void* state) { (void)state; }

void difftest_uarchstatus_cpy(void* state, bool direction) {
  (void)state;
  (void)direction;
}

void difftest_display(void) {
  LogDiff("pc = " FMT_WORD " priv = %s", cpu.pc, cpu_priv_name(cpu.priv));
}

void isa_reg_display(void) { difftest_display(); }

void update_dynamic_config(void* config) {
  (void)config; /* TODO(M7)：NEMU 的动态配置（store-log 等） */
}

void difftest_guided_exec(void* guide) {
  (void)guide; /* TODO(M7)：DUT 指导执行（指定异常/跳转） */
}

int difftest_status(void) { return 0; /* 0 = 一致 */ }
