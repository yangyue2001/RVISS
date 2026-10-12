#ifndef RVISS_RISCV64_MMU_H
#define RVISS_RISCV64_MMU_H

#include <memory/vaddr.h>

void riscv_mmu_set_sv39(bool supported);
void riscv_mmu_reset(void);
word_t riscv_satp_value(word_t value);
bool riscv_mmu_translate(MemAccess *access, uint8_t privilege);

#endif
