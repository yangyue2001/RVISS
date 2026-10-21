#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <util/config.h>
#include <util/state.h>

#include "../../machine/machine.h"
#include "../engine.h"

void engine_init(void) { riscv_decode_init(); }

void engine_set_history(bool enabled) { (void)enabled; }

size_t engine_recent(StepResult* out, size_t capacity) {
  (void)out;
  (void)capacity;
  return 0;
}

void engine_dump_recent(void) {}

bool engine_raise_trap(word_t cause) {
  (void)cause;
  return false;
}

void engine_flush(void) {}

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
