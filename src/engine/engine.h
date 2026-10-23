#ifndef RVISS_ENGINE_ENGINE_H
#define RVISS_ENGINE_ENGINE_H

#include <cpu/step.h>

void engine_init(void); /* 译码表；若已配置镜像则装入 RAM。不复位机器 */
bool engine_step(StepResult *result); /* 一个事件；暂停/预算耗尽时 false，无事件 */
/* 至多 n 次指令尝试，含同步异常，不含独立中断。 */
uint64_t engine_exec(uint64_t n);

#endif /* RVISS_ENGINE_ENGINE_H */
