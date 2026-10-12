#ifndef RVISS_UTIL_LOG_H
#define RVISS_UTIL_LOG_H

#include <stdbool.h>

/*
 * 分类 + 分级日志。
 * 对比 NEMU：它的 Logti/Logm/Logtr 是一堆编译期开关；这里改成"分类(category) + 等级(level)"
 * 两个运行时维度，调 trace 时不用重新编译。
 */

typedef enum {
  LOG_ERROR = 0,
  LOG_WARN,
  LOG_INFO,
  LOG_DEBUG,
  LOG_TRACE,
  LOG_LEVEL_NR
} LogLevel;

typedef enum {
  LOG_CAT_GENERAL = 0, /* 启动、统计、monitor */
  LOG_CAT_INSTR,       /* 指令流 trace（pc/字节/反汇编） */
  LOG_CAT_MEM,         /* 访存 trace */
  LOG_CAT_TRANSLATE,   /* 地址翻译/TLB trace */
  LOG_CAT_TRAP,        /* 异常与中断 */
  LOG_CAT_DEVICE,      /* 设备访问 */
  LOG_CAT_DIFFTEST,    /* difftest 交互 */
  LOG_CAT_NR
} LogCategory;

void log_init(const char *path); /* path == NULL 时输出到 stdout */
void log_close(void);
void log_set_level(LogLevel level);
void log_set_category(LogCategory cat, bool enable);
void log_printf(LogCategory cat, LogLevel level, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/*
 * 编译期开关（对应 Kconfig）：
 *   CONFIG_DEBUG       关掉后所有 Log 变成空操作，一行代码都不产生
 *   CONFIG_TRACE_*     逐类打开 trace 日志；未开的类在编译期消失
 * 运行时仍可用 --log-level / -t 做二次过滤。
 */

#ifdef CONFIG_DEBUG
#define Log(...)       log_printf(LOG_CAT_GENERAL,   LOG_INFO,  __VA_ARGS__)
#define Logw(...)      log_printf(LOG_CAT_GENERAL,   LOG_WARN,  __VA_ARGS__)
#define Loge(...)      log_printf(LOG_CAT_GENERAL,   LOG_ERROR, __VA_ARGS__)
#define LogTrap(...)   log_printf(LOG_CAT_TRAP,      LOG_DEBUG, __VA_ARGS__)
#define LogDevice(...) log_printf(LOG_CAT_DEVICE,    LOG_DEBUG, __VA_ARGS__)
#define LogDiff(...)   log_printf(LOG_CAT_DIFFTEST,  LOG_DEBUG, __VA_ARGS__)
#else /* !CONFIG_DEBUG */
#define Log(...)       ((void)0)
#define Logw(...)      ((void)0)
#define Loge(...)      ((void)0)
#define LogTrap(...)   ((void)0)
#define LogDevice(...) ((void)0)
#define LogDiff(...)   ((void)0)
#endif /* CONFIG_DEBUG */

#if defined(CONFIG_TRACE_INST) || defined(CONFIG_TRACE_INST_DASM)
#define LogInstr(...)  log_printf(LOG_CAT_INSTR, LOG_TRACE, __VA_ARGS__)
#else
#define LogInstr(...)  ((void)0)
#endif

#ifdef CONFIG_TRACE_MEM
#define LogMem(...)    log_printf(LOG_CAT_MEM, LOG_TRACE, __VA_ARGS__)
#else
#define LogMem(...)    ((void)0)
#endif

#ifdef CONFIG_TRACE_MMU
#define LogTlb(...)    log_printf(LOG_CAT_TRANSLATE, LOG_TRACE, __VA_ARGS__)
#else
#define LogTlb(...)    ((void)0)
#endif

#endif /* RVISS_UTIL_LOG_H */
