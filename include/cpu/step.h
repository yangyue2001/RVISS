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
typedef struct {
  uint64_t event_seq, retired_total;
  StepOutcome outcome;
  bool instruction_attempted, retired;
  vaddr_t pc_before, pc_after;
  uint8_t privilege_before, privilege_after;
  uint32_t raw_instruction;
  uint8_t instruction_length, raw_bytes;
  bool instruction_valid;
  const char *operation; /* 指向静态指令名；不是序列化格式 */
  bool register_write;
  uint8_t rd;
  word_t register_value;
  bool counter_write_cycle, counter_write_instret;
  MemAccess memory; /* 一条 I 指令最多一个显式数据访问；取指 fault 也可记录 */
  struct {
    bool valid;
    word_t cause;
    vaddr_t epc, tval;
  } trap;
} StepResult;

/* target 执行一个事件；计数、设备时间和预算由引擎管理。 */
void riscv_step(StepResult *result);
void riscv_update_counters(const StepResult *result);

#endif /* RVISS_CPU_STEP_H */
