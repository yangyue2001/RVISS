#include <common.h>
#include <rtl/rtl.h>

/*
 * 浮点后端占位。
 *
 * M5 实现 F/D 时二选一：
 *   - softfloat：位精确，便于 difftest，速度慢（推荐先做这个）
 *   - host FPU：快，但需要仔细处理 NaN/舍入模式/flag
 * 接口从 include/rtl/rtl-fp.h 暴露（待建），target 侧 exec 只依赖该接口。
 */

/* 目前无实现，见 docs/implementation_plan.md 的 M5。 */
