#include <common.h>
#include <stdarg.h>
#include <util/log.h>
/*
 * 分类 + 分级日志实现。
 * 日志目标是文件（--log）或 stdout；trace 类日志量大，建议输出到文件。
 */

static FILE* log_fp;
static LogLevel min_level = LOG_INFO;
static bool cat_enabled[LOG_CAT_NR];

static const char* level_name[LOG_LEVEL_NR] = {
    "ERROR", "WARN", "INFO", "DEBUG", "TRACE",
};

static const char* category_name[LOG_CAT_NR] = {
    "general", "instr", "mem", "mmu", "trap", "dev", "diff",
};

void log_init(const char* path) {
  if (path != NULL) {
    log_fp = fopen(path, "w");
    Assert(log_fp != NULL, "无法打开日志文件 %s", path);
  } else {
    log_fp = stdout;
  }
  for (int i = 0; i < LOG_CAT_NR; i++) {
    cat_enabled[i] = true;
  }
}

void log_close(void) {
  if (log_fp != NULL && log_fp != stdout) {
    fclose(log_fp);
  }
  log_fp = NULL;
}

void log_set_level(LogLevel level) { min_level = level; }

void log_set_category(LogCategory cat, bool enable) {
  if (cat >= 0 && cat < LOG_CAT_NR) {
    cat_enabled[cat] = enable;
  }
}

void log_printf(LogCategory cat, LogLevel level, const char* fmt, ...) {
  if (log_fp == NULL || level > min_level || !cat_enabled[cat]) {
    return;
  }

  fprintf(log_fp, "[%-5s %-7s] ", level_name[level], category_name[cat]);

  va_list ap;
  va_start(ap, fmt);
  vfprintf(log_fp, fmt, ap);
  va_end(ap);

  fputc('\n', log_fp);
  fflush(log_fp);
}
