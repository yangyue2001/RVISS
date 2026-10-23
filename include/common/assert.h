#ifndef RVISS_COMMON_ASSERT_H
#define RVISS_COMMON_ASSERT_H

#include <stdio.h>
#include <stdlib.h>

/*
 * 不可恢复的内部错误（bug）。
 * 客户机的异常（ecall/缺页…）不走这里，走 target/trap。
 *
 * 实现放在 src/util/panic.c；标 noreturn 让编译器能正确做控制流分析
 * （否则 mmu.c 这类 "panic 后不返回" 的函数会报 control reaches end of non-void
 * function）。
 */

#if defined(__GNUC__)
__attribute__((noreturn, format(printf, 1, 2)))
#endif
void rviss_panic(const char *fmt, ...);

#define panic(...) rviss_panic(__VA_ARGS__)

/*
 * 打开时就是 if (!条件) panic。
 * 关掉后不检查、不生成分支。条件仍求值，因此 Assert(mem_init()) 里的
 * 初始化还会执行；没有副作用的条件在 -O2 及更高优化下没有指令。
 * 格式参数不求值。没有条件、已经在失败分支里的停机仍用 panic()。
 */
#ifdef CONFIG_ASSERT
#define Assert(cond, ...)                  \
  do {                                     \
    if (!(cond)) rviss_panic(__VA_ARGS__); \
  } while (0)
#else
#define Assert(cond, ...) ((void)(cond))
#endif

#endif /* RVISS_COMMON_ASSERT_H */
