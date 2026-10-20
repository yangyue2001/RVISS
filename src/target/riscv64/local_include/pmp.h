#ifndef RVISS_RISCV64_PMP_H
#define RVISS_RISCV64_PMP_H

#include <common.h>
#include <memory/vaddr.h>

typedef enum {
  PMP_READ = 1 << 0,
  PMP_WRITE = 1 << 1,
  PMP_EXEC = 1 << 2,
  PMP_AMATCH_SHIFT = 3,
  PMP_AMATCH = (3 << PMP_AMATCH_SHIFT),
  PMP_LOCK = 1 << 7
} PmpPriv;

typedef enum {
  PMP_AMATCH_OFF,  /* Null (off)                            */
  PMP_AMATCH_TOR,  /* Top of Range                          */
  PMP_AMATCH_NA4,  /* Naturally aligned four-byte region    */
  PMP_AMATCH_NAPOT /* Naturally aligned power-of-two region */
} PmpAm;

void riscv_pmp_reset(void);
/* 初始化硬件 profile；客户机不能调用此配置接口。 */
void riscv_pmp_set_regions(unsigned count);
bool riscv_pmp_csr(word_t addr);
word_t riscv_pmp_read(word_t addr);
void riscv_pmp_write(word_t addr, word_t value);
bool riscv_pmp_check(paddr_t addr, word_t len, MemAccessType type, Priv privilege);

#endif
