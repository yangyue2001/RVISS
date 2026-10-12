#ifndef RVISS_UTIL_STATE_H
#define RVISS_UTIL_STATE_H

#include <common.h>

/* 仿真器整体的运行状态（客户机状态在 target 的 CPU_state 里） */

typedef enum {
  SIM_RUNNING = 0,
  SIM_STOPPED, /* 用户暂停/debugger 返回 */
  SIM_HALTED,  /* 程序正常结束（nemu_trap） */
  SIM_ABORTED, /* 内部错误 */
} sim_state_t;

typedef struct {
  sim_state_t state;
  vaddr_t halt_pc;
  int halt_code;      /* nemu_trap 的 a0：0 = HIT GOOD TRAP */
  uint64_t instret;   /* 累计正常退休，不受客户机计数器写入影响 */
  uint64_t attempts;  /* 指令尝试：含同步异常，不含独立中断事件 */
  uint64_t event_seq; /* 每个执行边界的累计序号 */
  uint64_t logical_ticks; /* 平台时间源，与客户机 cycle/instret 独立 */
  uint64_t idle_ticks, idle_advances; /* deterministic WFI time advances */
  uint64_t max_instr; /* 尝试上限；0 表示不限 */
  uint64_t max_events; /* 独立事件上限；0 表示不限 */
} SimState;

extern SimState sim;

static inline bool sim_running(void) { return sim.state == SIM_RUNNING; }

void sim_halt(vaddr_t pc, int code);
void sim_reset(void);
int sim_exit_code(void);

#endif /* RVISS_UTIL_STATE_H */
