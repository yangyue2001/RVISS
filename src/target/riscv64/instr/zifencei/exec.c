#include <cpu/exec.h>

ExecResult exec_fence_i(ExecContext* ctx) {
  (void)ctx;
  return exec_ok();
}
