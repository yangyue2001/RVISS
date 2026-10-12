#include <common.h> /* 提供 rviss_panic 声明 */

#include <stdarg.h>
#include <stdio.h>

/* 内部错误的统一出口（声明在 include/common/assert.h） */

void rviss_panic(const char *fmt, ...) {
  fflush(stdout);
  fprintf(stderr, "\033[1;31m[panic] ");
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\033[0m\n");
  abort();
}
