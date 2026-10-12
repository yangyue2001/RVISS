#ifndef RVISS_RISCV64_PMP_H
#define RVISS_RISCV64_PMP_H

#include <common.h>

enum { PMP_R=1, PMP_W=2, PMP_X=4, PMP_A=0x18, PMP_L=0x80,
       PMP_OFF=0, PMP_TOR=0x08, PMP_NA4=0x10, PMP_NAPOT=0x18 };
void riscv_pmp_reset(void);
/* 初始化硬件 profile；客户机不能调用此配置接口。 */
void riscv_pmp_set_regions(unsigned count);
bool riscv_pmp_csr(word_t addr);
word_t riscv_pmp_read(word_t addr);
void riscv_pmp_write(word_t addr, word_t value);
bool riscv_pmp_check(paddr_t addr, int len, int type, uint8_t privilege);

#endif
