#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <trace/trace.h>
#include <util/log.h>
#include <util/state.h>

#include "../../machine/machine.h"
#include "../engine.h"

/* target 提交架构事件；引擎统一处理预算、计数、设备时间和只读观察。 */
#define RECENT_COUNT 16
static StepResult recent[RECENT_COUNT];
static size_t recent_next, recent_count;
static bool history_enabled = true;

void engine_init(void) {
  riscv_decode_init();
  recent_next = recent_count = 0;
}

void engine_set_history(bool enabled) {
  history_enabled = enabled;
  recent_next = recent_count = 0;
}

size_t engine_recent(StepResult* out, size_t capacity) {
  if (sim.event_seq == 0 || !history_enabled) return 0;
  const size_t count = capacity < recent_count ? capacity : recent_count;
  for (size_t i = 0; i < count; i++) {
    out[i] = recent[(recent_next + RECENT_COUNT - count + i) % RECENT_COUNT];
  }
  return count;
}

#if defined(CONFIG_DEBUG) || defined(CONFIG_TRACE_INST) || \
    defined(CONFIG_TRACE_INST_DASM)
static const char* outcome_name(StepOutcome outcome) {
  static const char* const names[] = {"retired",   "synchronous_trap",
                                      "interrupt", "halted",
                                      "stopped",   "host_error"};
  return names[outcome];
}
#endif

void engine_dump_recent(void) {
#ifdef CONFIG_DEBUG
  StepResult events[RECENT_COUNT];
  const size_t count = engine_recent(events, RECENT_COUNT);
  for (size_t i = 0; i < count; i++) {
    const StepResult* r = &events[i];
    Log("event=%lu retired=%lu %s pc=" FMT_WORD " -> " FMT_WORD
        " raw=%08x bytes=%u valid=%d cause=" FMT_WORD " tval=" FMT_WORD,
        r->event_seq, r->retired_total, outcome_name(r->outcome), r->pc_before,
        r->pc_after, r->raw_instruction, r->raw_bytes, r->instruction_valid,
        r->trap.cause, r->trap.tval);
  }
#endif
}

static void stop_budget(void) {
  sim.state = SIM_STOPPED;
  sim.halt_pc = cpu.pc;
  Log("execution budget reached: attempts=%lu events=%lu", sim.attempts,
      sim.event_seq);
}

static bool can_step(void) {
  bool ret = true;
  if (!sim_running()) return false;
  if (sim.max_instr && sim.instret >= sim.max_instr) {
    stop_budget();
    return false;
  }
  return true;
}

static void complete_event(StepResult* r) {}

bool engine_step(StepResult* result) {
  if (!can_step()) return false;
  machine_update(result);
  complete_event(result);
  return true;
}

uint64_t engine_exec(uint64_t n) {
  uint64_t attempted = 0, events = 0;
  const uint64_t event_limit =
      n > (UINT64_MAX - 1) / 2 ? UINT64_MAX : 2 * n + 1;
  while (attempted < n && events < event_limit) {
    StepResult r;
    if (!engine_step(&r)) break;
    events++;
    if (r.instruction_attempted) attempted++;
  }
  if (events == event_limit && attempted < n && sim_running()) stop_budget();
  return attempted;
}
