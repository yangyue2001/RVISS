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

bool riscv_pmp_check(paddr_t addr, word_t len, MemAccessType type,
                     Priv privilege);

#endif
