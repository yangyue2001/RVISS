#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <cpu/interrupt.h>
#include <device/device.h>
#include <util/log.h>
#include <util/state.h>

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

void engine_flush(void) { /* 当前解释器无译码缓存。 */ }

void engine_set_history(bool enabled) {
  history_enabled = enabled;
  recent_next = recent_count = 0;
}

size_t engine_recent(StepResult *out, size_t capacity) {
  if (sim.event_seq == 0 || !history_enabled) return 0;
  const size_t count = capacity < recent_count ? capacity : recent_count;
  for (size_t i = 0; i < count; i++) {
    out[i] = recent[(recent_next + RECENT_COUNT - count + i) % RECENT_COUNT];
  }
  return count;
}

#if defined(CONFIG_DEBUG) || defined(CONFIG_TRACE_INST) || defined(CONFIG_TRACE_INST_DASM)
static const char *outcome_name(StepOutcome outcome) {
  static const char *const names[] = {
      "retired", "synchronous_trap", "interrupt", "halted", "stopped", "host_error"};
  return names[outcome];
}
#endif

void engine_dump_recent(void) {
#ifdef CONFIG_DEBUG
  StepResult events[RECENT_COUNT];
  const size_t count = engine_recent(events, RECENT_COUNT);
  for (size_t i = 0; i < count; i++) {
    const StepResult *r = &events[i];
    Log("event=%lu retired=%lu %s pc=" FMT_WORD " -> " FMT_WORD
        " raw=%08x bytes=%u valid=%d cause=" FMT_WORD " tval=" FMT_WORD,
        r->event_seq, r->retired_total, outcome_name(r->outcome),
        r->pc_before, r->pc_after, r->raw_instruction, r->raw_bytes,
        r->instruction_valid, r->trap.cause, r->trap.tval);
  }
#endif
}

static void stop_budget(void) {
  sim.state = SIM_STOPPED;
  sim.halt_pc = cpu.pc;
  Log("execution budget reached: attempts=%lu events=%lu", sim.attempts, sim.event_seq);
}

static bool can_step(void) {
  if (!sim_running()) return false;
  if ((sim.max_instr && sim.attempts >= sim.max_instr) ||
      (sim.max_events && sim.event_seq >= sim.max_events)) {
    stop_budget();
    return false;
  }
  return true;
}

static void complete_event(StepResult *r) {
  sim.event_seq++;
  sim.logical_ticks++;
  riscv_update_counters(r);
  if (r->instruction_attempted) sim.attempts++;
  if (r->retired) {
    sim.instret++;
  }
  if (r->outcome == STEP_HOST_ERROR) {
    sim.state = SIM_ABORTED;
    sim.halt_pc = r->pc_before;
    Loge("unsupported host execution path at " FMT_WORD
         ", access=%d addr=" FMT_WORD, r->pc_before, r->memory.type, r->memory.vaddr);
  }
  devices_tick(sim.logical_ticks);
  cpu_sync_irq_inputs();
  r->event_seq = sim.event_seq;
  r->retired_total = sim.instret;

  if (history_enabled) {
    if (sim.event_seq == 1) recent_next = recent_count = 0;
    recent[recent_next] = *r;
    recent_next = (recent_next + 1) % RECENT_COUNT;
    if (recent_count < RECENT_COUNT) recent_count++;
  }
#if defined(CONFIG_TRACE_INST_DASM) || defined(CONFIG_TRACE_INST)
  LogInstr("event=%lu %s (%s) " FMT_WORD ": %08x bytes=%u valid=%d %s",
           r->event_seq, outcome_name(r->outcome), cpu_priv_name(r->privilege_before),
           r->pc_before, r->raw_instruction, r->raw_bytes, r->instruction_valid,
           r->operation ? r->operation : "-");
#endif
  /* 当前事件的 outcome 保留；下一步才看到预算导致的 STOPPED。 */
  if (sim_running() &&
      ((sim.max_instr && sim.attempts >= sim.max_instr) ||
       (sim.max_events && sim.event_seq >= sim.max_events))) stop_budget();
}

bool engine_step(StepResult *result) {
  *result = (StepResult){.outcome = STEP_STOPPED,
                         .pc_before = cpu.pc, .pc_after = cpu.pc,
                         .privilege_before = cpu.priv, .privilege_after = cpu.priv,
                         .event_seq = sim.event_seq, .retired_total = sim.instret};
  if (!can_step()) return false;
  riscv_step(result);
  complete_event(result);
  return true;
}

bool engine_raise_trap(word_t cause) {
  if (!can_step()) return false;
  StepResult r = {.outcome = (cause & IRQ_BIT) ? STEP_INTERRUPT : STEP_SYNCHRONOUS_TRAP,
                  .pc_before = cpu.pc, .privilege_before = cpu.priv};
  r.trap.valid = true;
  r.trap.cause = cause;
  r.trap.epc = cpu.pc;
  cpu.pc = cpu_raise_trap(cause, cpu.pc);
  r.pc_after = cpu.pc;
  r.privilege_after = cpu.priv;
  complete_event(&r);
  return true;
}

uint64_t engine_exec(uint64_t n) {
  uint64_t attempted = 0, events = 0;
  const uint64_t event_limit = n > (UINT64_MAX - 1) / 2 ? UINT64_MAX : 2 * n + 1;
  while (attempted < n && events < event_limit) {
    StepResult r;
    if (!engine_step(&r)) break;
    events++;
    if (r.instruction_attempted) attempted++;
  }
  if (events == event_limit && attempted < n && sim_running()) stop_budget();
  return attempted;
}
