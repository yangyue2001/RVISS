#ifndef RVISS_UTIL_CONFIG_H
#define RVISS_UTIL_CONFIG_H

#include <common.h>
#include <util/log.h> /* RuntimeConfig 里用到 LogLevel */

/*
 * 运行期配置（命令行）。
 *
 * 与 Kconfig 的分工：
 *   - 板级默认值（RAM 基址/大小、复位向量、设备清单）来自 Kconfig（编译期）
 *   - 命令行只做"本次运行的覆盖"，例如 --memory 临时改 RAM 大小
 * 这样一套二进制能跑多种跑法，而编译期又保留板子的完整定义。
 */

typedef struct {
  size_t ram_size_override; /* 0 = 用板级（Kconfig）默认值 */

  uint64_t max_instr; /* 指令尝试上限，含同步异常；0 = 不限 */
  uint64_t max_events; /* 指令/陷阱/中断事件总上限；0 = 不限 */
  bool uart_stdin; /* explicit stdin ownership, batch only */
  const char *uart_record, *uart_replay;
  bool batch;         /* 批处理：不进交互调试器 */

  bool log_trace;     /* -t：打开指令 trace（还需编译期 CONFIG_TRACE_INST*） */
  const char *log_file;
  LogLevel log_level;

  const char *image;          /* 位置参数：镜像路径 */
  const char *checkpoint_in;  /* --checkpoint-in  */
  const char *checkpoint_out; /* --checkpoint-out */
} RuntimeConfig;

extern RuntimeConfig rt_cfg;

void config_parse(int argc, char *argv[]); /* 失败时打印 usage 并退出 */
void config_print(void);

#endif /* RVISS_UTIL_CONFIG_H */
