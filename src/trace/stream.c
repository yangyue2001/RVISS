#include <cpu/step.h>
#include <trace/trace.h>

bool trace_init(const char* path) {
  (void)path;
  return true;
}

bool trace_close(void) { return true; }

void trace_emit_record(const StepResult* result) { (void)result; }
