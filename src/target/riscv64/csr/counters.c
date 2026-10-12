#include <cpu/cpu.h>
#include <cpu/step.h>

/* 客户机计数器由 ISA 管理，独立于引擎累计退休/事件/时间。 */
void riscv_update_counters(const StepResult *r) {
  if (!r->counter_write_cycle) cpu.cycle++;
  if (r->retired && !r->counter_write_instret) cpu.instret++;
}
