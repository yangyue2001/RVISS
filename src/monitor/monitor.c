#include <common.h>
#include <cpu/cpu.h>
#include <host/host.h>
#include <monitor/monitor.h>
#include <util/config.h>
#include <util/log.h>
#include <util/state.h>

#include "../engine/engine.h"
#include "../machine/machine.h"

/*
 * monitor：参数 → 主机日志 → 板卡 → 引擎。
 * 批处理模式（-b）直接跑到底；否则进 ui.c 的交互调试器。
 * 日志是主机诊断。machine_init 复位 CPU 并登记外设。
 * 仿真器复位和镜像在 engine_init。
 */

void ui_mainloop(void);

static void host_log_init(void) {
  log_init(rt_cfg.log_file);
  log_set_level(rt_cfg.log_level);
  config_print();
}

void monitor_init(int argc, char* argv[]) {
  config_parse(argc, argv);
  host_log_init();

  MachineConfig cfg = machine_minimal;
  if (rt_cfg.ram_size_override != 0) cfg.ram_size = rt_cfg.ram_size_override;
  machine_init(&cfg);
  engine_init();
}

static void print_summary(uint64_t us) {
  Log("cpu pc = " FMT_WORD " priv = %s satp = " FMT_WORD, cpu.pc,
      cpu_priv_name(cpu.priv), cpu.satp);
  Log("stop reason: %s, pc = " FMT_WORD ", halt_code = %d",
      sim.state == SIM_HALTED
          ? "HALTED"
          : (sim.state == SIM_STOPPED ? "STOPPED" : "ABORTED"),
      sim.halt_pc, sim.halt_code);
  Log("retired=%lu attempts=%lu events=%lu logical_ticks=%lu", sim.instret,
      sim.attempts, sim.event_seq, sim.logical_ticks);
  Log("idle advances=%lu ticks=%lu", sim.idle_advances, sim.idle_ticks);
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
  print_summary(t1 - t0);
  host_uart_input_close();
  log_close();
  return sim_exit_code();
}
