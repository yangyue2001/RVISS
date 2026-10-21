#include <cpu/cpu.h>
#include <cpu/step.h>

/* 客户机计数器由 ISA 管理。StepResult 目前不携带计数信息。 */
void riscv_update_counters(const StepResult* result) { (void)result; }
