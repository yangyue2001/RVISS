#ifndef RVISS_CPU_STEP_H
#define RVISS_CPU_STEP_H

#include <common.h>
#include <memory/vaddr.h>

typedef enum {
  STEP_RETIRED,
  STEP_SYNCHRONOUS_TRAP,
  STEP_INTERRUPT,
  STEP_HALTED,
  STEP_STOPPED,
  STEP_HOST_ERROR,
} StepOutcome;

/* 一次已完成的执行边界。引擎补入序号/退休总数，观察者只能读取副本。
 * halted 可包含成功退休的自定义退出指令；trap/interrupt 不退休。
 */
typedef struct StepResult {
} StepResult;

/* target 执行一个事件；计数、设备时间和预算由引擎管理。 */
void riscv_step(StepResult* result);
void riscv_take_interrupt(StepResult* result, word_t cause);
void riscv_execute(StepResult* result);
word_t riscv_query_intr(void);
void riscv_update_counters(const StepResult* result);
bool riscv_wait_resume(
    void); /* any individually enabled IRQ, global enables ignored */

#endif /* RVISS_CPU_STEP_H */
