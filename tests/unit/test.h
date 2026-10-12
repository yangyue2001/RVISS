#ifndef RVISS_TEST_H
#define RVISS_TEST_H

/* 极简单元测试框架：每个测试文件就是一个 main() */

#include <stdio.h>

static int test_total;
static int test_failed;

#define CHECK(cond)                                                     \
  do {                                                                  \
    test_total++;                                                       \
    if (!(cond)) {                                                      \
      test_failed++;                                                    \
      printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                   \
  } while (0)

#define CHECK_EQ(a, b)                                                  \
  do {                                                                  \
    test_total++;                                                       \
    const unsigned long long _a = (unsigned long long)(a);              \
    const unsigned long long _b = (unsigned long long)(b);              \
    if (_a != _b) {                                                     \
      test_failed++;                                                    \
      printf("  FAIL %s:%d: %s == %s (0x%llx vs 0x%llx)\n", __FILE__,   \
             __LINE__, #a, #b, _a, _b);                                 \
    }                                                                   \
  } while (0)

#define TEST_MAIN_END()                                                 \
  do {                                                                  \
    printf("%s: %d checks, %d failures\n", __FILE__, test_total,        \
           test_failed);                                                \
    return test_failed != 0;                                            \
  } while (0)

#endif /* RVISS_TEST_H */
