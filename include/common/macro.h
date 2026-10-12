#ifndef RVISS_COMMON_MACRO_H
#define RVISS_COMMON_MACRO_H

/* 字符串化 / 拼接：用于 X-macro 表驱动代码生成 */
#define str_temp(x) #x
#define str(x) str_temp(x)

#define concat_temp(x, y) x##y
#define concat(x, y) concat_temp(x, y)

/* 分支预测提示 */
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define MAP(c, f) c(f)

#endif /* RVISS_COMMON_MACRO_H */
