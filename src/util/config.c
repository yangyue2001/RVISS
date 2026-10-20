#include <common.h>
#include <getopt.h>
#include <util/config.h>
#include <util/log.h>

/*
 * 命令行解析。
 * 只处理"本次运行的覆盖"，板级默认值在 src/machine/boards 下的板级文件里
 * （由 Kconfig 决定）。
 */

RuntimeConfig rt_cfg;

void config_print(void) {
  Log("config: max_instr=%lu max_events=%lu batch=%d trace=%d trace_file=%s "
      "mem_override=%s",
      rt_cfg.max_instr, rt_cfg.max_events, rt_cfg.batch, rt_cfg.log_trace,
      rt_cfg.trace_file ? rt_cfg.trace_file : "-",
      rt_cfg.ram_size_override ? "yes" : "no");
}

static void usage(const char* prog) {
  printf("用法: %s [选项] [镜像]\n\n", prog);
  printf("  -b, --batch              批处理模式（不进交互调试器）\n");
  printf("  -n, --max-instr N        最多 N 次指令尝试（含异常，0=不限）\n");
  printf("      --max-events N       最多 N 个执行事件（含中断，0=不限）\n");
  printf("      --uart-stdin         将批处理 stdin 接到客户机串口\n");
  printf("      --uart-record FILE   记录串口输入的执行事件与字节\n");
  printf("      --uart-replay FILE   按记录事件重放串口输入\n");
  printf("  -m, --memory SIZE        RAM 大小覆盖，如 128M / 1G\n");
  printf("  -l, --log FILE           日志输出到文件（默认 stdout）\n");
  printf(
      "  -L, --log-level N        日志等级 0=ERROR..4=TRACE（默认 2=INFO）\n");
  printf("  -t, --trace              打开指令 trace（等级自动设为 TRACE）\n");
  printf(
      "      --trace-file FILE   写入 RVTR 二进制执行事件（需 "
      "CONFIG_TRACE_STREAM）\n");
  printf("      --checkpoint-in FILE  执行前恢复 checkpoint\n");
  printf("      --checkpoint-out FILE 执行后保存 checkpoint\n");
  printf("  -h, --help               显示本帮助\n");
}

/* 解析 256M / 1G / 0x... 这类大小 */
static size_t parse_size(const char* s) {
  char* end = NULL;
  unsigned long long v = strtoull(s, &end, 0);
  if (end != NULL && *end != '\0') {
    switch (*end) {
      case 'k':
      case 'K':
        v <<= 10;
        break;
      case 'm':
      case 'M':
        v <<= 20;
        break;
      case 'g':
      case 'G':
        v <<= 30;
        break;
      default:
        panic("无法解析大小 '%s'", s);
    }
  }
  return (size_t)v;
}

void config_parse(int argc, char* argv[]) {
  rt_cfg = (RuntimeConfig){
      .ram_size_override = 0,
      .max_instr = 0,
      .batch = false,
      .log_trace = false,
      .log_file = NULL,
      .log_level = LOG_INFO,
      .trace_file = NULL,
      .image = NULL,
      .checkpoint_in = NULL,
      .checkpoint_out = NULL,
  };

  enum {
    OPT_CPT_IN = 1000,
    OPT_CPT_OUT,
    OPT_MAX_EVENTS,
    OPT_UART_STDIN,
    OPT_UART_RECORD,
    OPT_UART_REPLAY,
    OPT_TRACE_FILE
  };

  static const struct option table[] = {
      {"uart-stdin", no_argument, NULL, OPT_UART_STDIN},
      {"uart-record", required_argument, NULL, OPT_UART_RECORD},
      {"uart-replay", required_argument, NULL, OPT_UART_REPLAY},
      {"batch", no_argument, NULL, 'b'},
      {"max-instr", required_argument, NULL, 'n'},
      {"max-events", required_argument, NULL, OPT_MAX_EVENTS},
      {"memory", required_argument, NULL, 'm'},
      {"log", required_argument, NULL, 'l'},
      {"log-level", required_argument, NULL, 'L'},
      {"trace", no_argument, NULL, 't'},
      {"trace-file", required_argument, NULL, OPT_TRACE_FILE},
      {"checkpoint-in", required_argument, NULL, OPT_CPT_IN},
      {"checkpoint-out", required_argument, NULL, OPT_CPT_OUT},
      {"help", no_argument, NULL, 'h'},
      {0, 0, 0, 0},
  };

  int o;
  while ((o = getopt_long(argc, argv, "-bn:m:l:L:th", table, NULL)) != -1) {
    switch (o) {
      case OPT_UART_STDIN:
        rt_cfg.uart_stdin = true;
        break;
      case OPT_UART_RECORD:
        rt_cfg.uart_record = optarg;
        break;
      case OPT_UART_REPLAY:
        rt_cfg.uart_replay = optarg;
        break;
      case 'b':
        rt_cfg.batch = true;
        break;
      case 'n':
        rt_cfg.max_instr = strtoull(optarg, NULL, 0);
        break;
      case OPT_MAX_EVENTS:
        rt_cfg.max_events = strtoull(optarg, NULL, 0);
        break;
      case 'm':
        rt_cfg.ram_size_override = parse_size(optarg);
        break;
      case 'l':
        rt_cfg.log_file = optarg;
        break;
      case 'L':
        rt_cfg.log_level = (LogLevel)atoi(optarg);
        break;
      case 't':
        rt_cfg.log_trace = true;
        rt_cfg.log_level = LOG_TRACE;
        break;
      case OPT_TRACE_FILE:
        rt_cfg.trace_file = optarg;
        break;
      case OPT_CPT_IN:
        rt_cfg.checkpoint_in = optarg;
        break;
      case OPT_CPT_OUT:
        rt_cfg.checkpoint_out = optarg;
        break;
      case 1:
        rt_cfg.image = optarg;
        break; /* 位置参数：镜像 */
      case 'h':
        usage(argv[0]);
        exit(0);
      default:
        usage(argv[0]);
        exit(1);
    }
  }
  if (rt_cfg.uart_stdin && !rt_cfg.batch) {
    fprintf(stderr, "--uart-stdin requires --batch\n");
    exit(1);
  }
  if ((rt_cfg.uart_replay &&
       (!rt_cfg.batch || rt_cfg.uart_stdin || rt_cfg.uart_record)) ||
      (rt_cfg.uart_record && !rt_cfg.uart_stdin)) {
    fprintf(stderr,
            "UART replay requires batch and exclusive input; recording "
            "requires --uart-stdin\n");
    exit(1);
  }
}