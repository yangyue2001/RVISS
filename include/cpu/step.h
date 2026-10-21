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

/* 取指、译码、执行一条指令。引擎交互还没定，这里不传结果。 */
void riscv_execute(void);
/* 先处理可响应中断，否则执行一条指令。result 暂不使用。 */
void riscv_step(StepResult* result);
word_t riscv_query_intr(void);
void riscv_update_counters(const StepResult* result);
bool riscv_wait_resume(
    void); /* any individually enabled IRQ, global enables ignored */

#endif /* RVISS_CPU_STEP_H */
