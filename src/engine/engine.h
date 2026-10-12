#ifndef RVISS_ENGINE_ENGINE_H
#define RVISS_ENGINE_ENGINE_H

#include <cpu/step.h>

void engine_init(void); /* 初始化译码及观察记录；不复位机器 */
bool engine_step(StepResult *result); /* 一个事件；暂停/预算耗尽时 false，无事件 */
/* legacy adapter：至多 n 次指令尝试，含同步异常，不含独立中断。 */
uint64_t engine_exec(uint64_t n);
/* 外部 ref 注入的 trap 同样经过事件/时间计数；不消耗指令尝试。 */
bool engine_raise_trap(word_t cause);
void engine_flush(void);

void engine_set_history(bool enabled);
size_t engine_recent(StepResult *out, size_t capacity); /* 最近事件，按序复制 */
void engine_dump_recent(void);

#endif /* RVISS_ENGINE_ENGINE_H */
