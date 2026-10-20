#ifndef RVISS_RISCV64_MMU_H
#define RVISS_RISCV64_MMU_H

#include <cpu/cpu.h>
#include <memory/vaddr.h>

static inline bool satp_mode_is_bare(reg_t satp_mode) {
  return satp_mode == SATP64_MODE_BARE;
}

static inline bool satp_mode_is_sv39(reg_t satp_mode) {
  return satp_mode == SATP64_MODE_SV39;
}

static inline bool satp_mode_is_sv48(reg_t satp_mode) {
  return satp_mode == SATP64_MODE_SV48;
}

static inline bool satp_mode_is_sv57(reg_t satp_mode) {
  return satp_mode == SATP64_MODE_SV57;
}
#endif
