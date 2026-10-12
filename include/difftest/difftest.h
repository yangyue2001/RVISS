#ifndef RVISS_DIFFTEST_DIFFTEST_H
#define RVISS_DIFFTEST_DIFFTEST_H

#include <common.h>

/*
 * difftest：与 DUT（香山等）对拍。
 * 对外 ABI（difftest_init / difftest_exec / ...）声明在 include/rviss.h，
 * 由 src/difftest/difftest_api.c 实现；本头只放内部辅助接口。
 */

void difftest_ref_reset(void); /* ref 侧每次 difftest_init 后复位状态 */

#endif /* RVISS_DIFFTEST_DIFFTEST_H */
