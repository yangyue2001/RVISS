#include <common.h>
#include "test.h"

/* 基础位操作单元测试 */

int main(void) {
  /* BITS(x, hi, lo) */
  CHECK_EQ(BITS(0xdeadbeefull, 31, 0), 0xdeadbeefull);
  CHECK_EQ(BITS(0xdeadbeefull, 15, 8), 0xbeull);
  CHECK_EQ(BITS(0xdeadbeefdeadbeefull, 63, 32), 0xdeadbeefull);
  CHECK_EQ(BITS(0xdeadbeefull, 63, 32), 0);
  CHECK_EQ(BITS(0x0000000080000000ull, 31, 31), 1);
  CHECK_EQ(BITS(0xffffffffffffffffull, 12, 11), 3);

  /* BIT(x, n) */
  CHECK_EQ(BIT(0x8000000000000001ull, 63), 1);
  CHECK_EQ(BIT(0x8000000000000001ull, 0), 1);
  CHECK_EQ(BIT(0x8000000000000001ull, 1), 0);

  /* SEXT(x, len) */
  CHECK_EQ(SEXT(0x7ff, 12), 0x7ff);
  CHECK_EQ(SEXT(0x800, 12), (sword_t)(int64_t)-2048);
  CHECK_EQ(SEXT(1, 1), (sword_t)(int64_t)-1);
  CHECK_EQ(SEXT(0xfffff, 20), (sword_t)(int64_t)-1);

  TEST_MAIN_END();
}
