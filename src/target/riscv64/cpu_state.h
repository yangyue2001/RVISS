#ifndef RVISS_RISCV64_CPU_STATE_H
#define RVISS_RISCV64_CPU_STATE_H

/*
 * target 侧的转发头：体系结构状态的真身在 include/cpu/cpu.h
 * （因为引擎也要用 pc）。target 内部代码统一 include 本头，保持
 * "target 代码只认自己的头"的书写习惯。
 */

#include <cpu/cpu.h>

#endif /* RVISS_RISCV64_CPU_STATE_H */
