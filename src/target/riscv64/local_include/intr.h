#ifndef RVISS_RISCV64_INTR_H
#define RVISS_RISCV64_INTR_H

#include <common.h>
#include <cpu/interrupt.h> /* 异常/中断编号是公共约定，见该头 */

/* target 私有的陷阱接口 */

/* trap/trap.c：统一陷阱入口，返回陷阱处理例程地址 */
vaddr_t riscv_raise_trap(word_t cause, vaddr_t epc, vaddr_t tval);

/* trap/intr.c：查询当前可响应的最高优先级中断；无则返回 0 */
word_t riscv_query_intr(void);

/* trap/intr.c：清空设备中断线的电平快照（cpu_reset 时调用） */
void riscv_intr_reset(void);

/* 特权返回指令 */
vaddr_t riscv_mret(void);
vaddr_t riscv_sret(void);

#endif /* RVISS_RISCV64_INTR_H */
