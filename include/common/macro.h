#ifndef RVISS_COMMON_MACRO_H
#define RVISS_COMMON_MACRO_H

/* 字符串化 / 拼接：用于 X-macro 表驱动代码生成 */
#define STR_TEMP(x) #x
#define STR(x) STR_TEMP(x)

#define CONCAT_TEMP(x, y) x##y
#define CONCAT(x, y) CONCAT_TEMP(x, y)

#define CHOOSE2nd(a, b, ...) b
#define MUX_WITH_COMMA(contain_comma, a, b) CHOOSE2nd(contain_comma a, b)
#define MUX_MACRO_PROPERTY(p, macro, a, b) \
  MUX_WITH_COMMA(concat(p, macro), a, b)
// define some selection functions based on the properties of BOOLEAN macro
#define MUXDEF(macro, X, Y) MUX_MACRO_PROPERTY(__P_DEF_, macro, X, Y)
#define MUXNDEF(macro, X, Y) MUX_MACRO_PROPERTY(__P_DEF_, macro, Y, X)
#define MUXONE(macro, X, Y) MUX_MACRO_PROPERTY(__P_ONE_, macro, X, Y)
#define MUXZERO(macro, X, Y) MUX_MACRO_PROPERTY(__P_ZERO_, macro, X, Y)

// simplification for conditional compilation
#define __IGNORE(...)
#define __KEEP(...) __VA_ARGS__
// keep the code if a boolean macro is defined
#define IFDEF(macro, ...) MUXDEF(macro, __KEEP, __IGNORE)(__VA_ARGS__)
// keep the code if a boolean macro is undefined
#define IFNDEF(macro, ...) MUXNDEF(macro, __KEEP, __IGNORE)(__VA_ARGS__)
// keep the code if a boolean macro is defined to 1
#define IFONE(macro, ...) MUXONE(macro, __KEEP, __IGNORE)(__VA_ARGS__)
// keep the code if a boolean macro is defined to 0
#define IFZERO(macro, ...) MUXZERO(macro, __KEEP, __IGNORE)(__VA_ARGS__)

/* 分支预测提示 */
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define MAP(c, f) c(f)

/* 左闭右闭 [lo, hi] */
#define IN_RANGE_CC(x, lo, hi) ((x) >= (lo) && (x) <= (hi))
/* 左闭右开 [lo, hi) */
#define IN_RANGE_CO(x, lo, hi) ((x) >= (lo) && (x) < (hi))
/* 左开右闭 (lo, hi] */
#define IN_RANGE_OC(x, lo, hi) ((x) > (lo) && (x) <= (hi))
/* 左开右开 (lo, hi) */
#define IN_RANGE_OO(x, lo, hi) ((x) > (lo) && (x) < (hi))
#endif /* RVISS_COMMON_MACRO_H */
