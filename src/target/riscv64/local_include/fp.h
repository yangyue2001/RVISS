#ifndef RVISS_RISCV64_FP_H
#define RVISS_RISCV64_FP_H

#include <cpu/decode.h>

bool riscv_fp_enabled(bool double_precision);
void riscv_fp_dirty(void);
void riscv_fp_execute(Decode *s);

#endif
