#include <common.h>
#include <util/state.h>

/* 仿真器整体状态 */

SimState sim;

void sim_reset(void) { sim = (SimState){.state = SIM_RUNNING}; }

void sim_halt(vaddr_t pc, int code) {
  sim.state = SIM_HALTED;
  sim.halt_pc = pc;
  sim.halt_code = code;
}

int sim_exit_code(void) {
  switch (sim.state) {
    case SIM_HALTED:
      return sim.halt_code != 0 ? 1 : 0; /* 0 = HIT GOOD TRAP */
    case SIM_ABORTED:
      return 2;
    default:
      return 0;
  }
}
