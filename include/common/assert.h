#ifndef RVISS_COMMON_ASSERT_H
#define RVISS_COMMON_ASSERT_H

#include <stdio.h>
#include <stdlib.h>

/*
 * 不可恢复的内部错误（bug）。
 * 客户机的异常（ecall/缺页…）不走这里，走 target/trap。
 *
 * 实现放在 src/util/panic.c；标 noreturn 让编译器能正确做控制流分析
 * （否则 mmu.c 这类 "panic 后不返回" 的函数会报 control reaches end of non-void function）。
 */

#if defined(__GNUC__)
__attribute__((noreturn, format(printf, 1, 2)))
#endif
void rviss_panic(const char *fmt, ...);

#define panic(...) rviss_panic(__VA_ARGS__)

#define Assert(cond, ...)          \
  do {                             \
    if (!(cond)) rviss_panic(__VA_ARGS__); \
  } while (0)

#endif /* RVISS_COMMON_ASSERT_H */
