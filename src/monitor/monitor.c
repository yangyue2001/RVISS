#include <checkpoint/checkpoint.h>
#include <common.h>
#include <host/host.h>
#include <memory/image.h>
#include <memory/paddr.h>
#include <monitor/monitor.h>
#include <util/config.h>
#include <util/log.h>
#include <util/state.h>
#include <trace/trace.h>

#include "../engine/engine.h"
#include "../machine/machine.h"

/*
 * monitor：把 "解析参数 → 建机器 → 装镜像 → 跑 → 报告" 串起来。
 * 批处理模式（-b）直接跑到底；否则进 ui.c 的交互调试器。
 */

void ui_mainloop(void);

void monitor_init(int argc, char* argv[]) {
  config_parse(argc, argv);
  log_init(rt_cfg.log_file);
  log_set_level(rt_cfg.log_level);
  config_print();
#ifdef CONFIG_TRACE_STREAM
  if (!trace_init(rt_cfg.trace_file)) panic("初始化 trace 失败");
#else
  if (rt_cfg.trace_file != NULL)
    Logw("--trace-file requires CONFIG_TRACE_STREAM; trace disabled");
#endif

  /* 板级默认来自 Kconfig（boards 下的板级文件），命令行只做本次覆盖 */
  MachineConfig cfg = machine_minimal;
  if (rt_cfg.ram_size_override != 0) {
    cfg.ram_size = rt_cfg.ram_size_override;
  }
  machine_init(&cfg);
  engine_init();

  if (rt_cfg.log_trace) {
#if !defined(CONFIG_TRACE_INST) && !defined(CONFIG_TRACE_INST_DASM)
    Logw(
        "-t/--trace 已给出，但编译时未开启 CONFIG_TRACE_INST*，不会有指令流输出"
        "（重新 make debug_defconfig 或 make menuconfig 打开）");
#endif
  }

  if (rt_cfg.checkpoint_in != NULL) {
    if (!cpt_load(rt_cfg.checkpoint_in)) {
      panic("恢复 checkpoint 失败: %s", rt_cfg.checkpoint_in);
    }
  }

  if (rt_cfg.image != NULL) {
    const paddr_t end = image_load(rt_cfg.image, machine_get()->ram_base);
    if (end == 0) {
      panic("加载镜像失败: %s", rt_cfg.image);
    }
    Log("image loaded, end = " FMT_PADDR, end);
  } else {
    Log("未指定镜像：内存为空，可用交互模式手工检查");
  }

  sim.state = SIM_RUNNING;
  sim.max_instr = rt_cfg.max_instr;
  sim.max_events = rt_cfg.max_events;
}

static void print_summary(uint64_t us) {
  Log("stop reason: %s, pc = " FMT_WORD ", halt_code = %d",
      sim.state == SIM_HALTED
          ? "HALTED"
          : (sim.state == SIM_STOPPED ? "STOPPED" : "ABORTED"),
      sim.halt_pc, sim.halt_code);
  Log("retired=%lu attempts=%lu events=%lu logical_ticks=%lu",
      sim.instret, sim.attempts, sim.event_seq, sim.logical_ticks);
  Log("idle advances=%lu ticks=%lu", sim.idle_advances, sim.idle_ticks);
  if (sim.state == SIM_ABORTED || sim.state == SIM_STOPPED) engine_dump_recent();
  if (us > 0) {
    Log("host time = %lu us, speed = %llu instr/s", us,
        (unsigned long long)(sim.instret * 1000000ull / us));
  }

  if (sim.state == SIM_HALTED) {
    Log("%s at pc = " FMT_WORD,
        sim.halt_code == 0 ? "\033[1;32mHIT GOOD TRAP\033[0m"
                           : "\033[1;31mHIT BAD TRAP\033[0m",
        sim.halt_pc);
  }
}

int monitor_run(void) {
  const uint64_t t0 = host_time_us();

  if (rt_cfg.batch) {
    engine_exec(UINT64_MAX);
  } else {
    ui_mainloop();
  }

  const uint64_t t1 = host_time_us();

  if (rt_cfg.checkpoint_out != NULL) {
    if (!cpt_save(rt_cfg.checkpoint_out)) {
      Loge("保存 checkpoint 失败: %s", rt_cfg.checkpoint_out);
    }
  }

  print_summary(t1 - t0);
#ifdef CONFIG_TRACE_STREAM
  if (!trace_close()) Loge("关闭 trace 文件失败");
#endif
  host_uart_input_close();
  log_close();
  return sim_exit_code();
}
