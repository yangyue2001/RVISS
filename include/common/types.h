#ifndef RVISS_COMMON_TYPES_H
#define RVISS_COMMON_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * RVISS 只支持 RV64，所以这里不做 ISA 位宽抽象（对比 NEMU 的
 * MUXDEF(CONFIG_ISA64,...)）。 如果哪天要支持 RV32，改这一处即可。
 */

typedef uint64_t word_t; /* 通用无符号字（寄存器宽度） */
typedef int64_t sword_t; /* 通用有符号字 */
typedef word_t reg_t;    /* RTL 层寄存器类型（引擎与 target 的契约类型） */
typedef word_t freg_t;
typedef word_t vaddr_t;    /* 客户机虚拟地址 */
typedef uint64_t paddr_t;  /* 客户机物理地址 */
typedef uint32_t ioaddr_t; /* 端口 IO 地址（保留，RV64 基本不用） */

#define FMT_WORD "0x%016lx"
#define FMT_PADDR "0x%016lx"

#define ARRLEN(a) (sizeof(a) / sizeof((a)[0]))

#endif /* RVISS_COMMON_TYPES_H */
