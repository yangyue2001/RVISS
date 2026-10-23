#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <memory/image.h>
#include <util/config.h>
#include <util/log.h>
#include <util/state.h>

#include "../../machine/machine.h"
#include "../engine.h"

void engine_init(void) {
  sim_reset();
  riscv_decode_init();
  if (rt_cfg.image == NULL) return;
  const paddr_t end = image_load(rt_cfg.image, machine_get()->ram_base);
  Assert(end != 0, "加载镜像失败: %s", rt_cfg.image);
  Log("image loaded, end = " FMT_PADDR, end);
}

bool engine_step(StepResult* result) {
  if (!sim_running()) return false;
  sim.logical_ticks++;
  machine_advance(sim.logical_ticks);
  machine_update(result);
  sim.event_seq++;
  return sim_running();
}

uint64_t engine_exec(uint64_t n) {
  uint64_t stepped = 0;
  while (stepped < n && sim_running()) {
    if (rt_cfg.max_events != 0 && sim.event_seq >= rt_cfg.max_events) break;
    StepResult result;
    if (!engine_step(&result)) break;
    stepped++;
  }
  if (sim_running()) {
    sim.halt_pc = cpu.pc;
    sim.state = SIM_STOPPED;
  }
  return stepped;
}
