#ifndef RVISS_CPU_CSR_LIST_H
#define RVISS_CPU_CSR_LIST_H

#include <common.h>

#include "csr_machine.h"
#include "csr_supervisor.h"
#include "csr_user.h"
/*
 * ★ CSR 清单：唯一事实来源 ★
 *
 * 这里只生成地址常量。存储字段在 CPU_state；哪个地址真正可访问、
 * 读写成什么样，由 src/target/riscv64/csr/csr.c 的 RVISS_CSR_IMPL 决定。
 * 目录里有名字不等于客户机可以访问。
 *
 * 特权级和整寄存器只读不写进实现清单：csr[9:8] 是最低特权，
 * csr[11:10]==3 是只读。别名（sstatus/sie/sip）不在这里生成第二份存储，
 * 见 docs/plans/t028_csr_descriptor.md。
 */

/* ---------------- CSR 地址（privileged spec Vol.II 的 CSR 表）
 * ---------------- */
#define CSR_ENUM(name, addr) CSR_##name = addr,
#define CSRS_All(f) CSRS_Unprivileged(f) CSRS_Supervisor(f) CSRS_Machine(f)

enum { MAP(CSRS_All, CSR_ENUM) };

/* 调用方使用规范里的大写地址名；目录宏的名字是小写，避免和存储字段绑死。 */
#define CSR_FFLAGS CSR_fflags
#define CSR_FRM CSR_frm
#define CSR_FCSR CSR_fcsr
#define CSR_SSTATUS CSR_sstatus
#define CSR_SIE CSR_sie
#define CSR_STVEC CSR_stvec
#define CSR_SCOUNTEREN CSR_scounteren
#define CSR_SSCRATCH CSR_sscratch
#define CSR_SEPC CSR_sepc
#define CSR_SCAUSE CSR_scause
#define CSR_STVAL CSR_stval
#define CSR_SIP CSR_sip
#define CSR_SATP CSR_satp
#define CSR_MVENDORID CSR_mvendorid
#define CSR_MARCHID CSR_marchid
#define CSR_MIMPID CSR_mimpid
#define CSR_MHARTID CSR_mhartid
#define CSR_MCONFIGPTR CSR_mconfigptr
#define CSR_MSTATUS CSR_mstatus
#define CSR_MISA CSR_misa
#define CSR_MEDELEG CSR_medeleg
#define CSR_MIDELEG CSR_mideleg
#define CSR_MIE CSR_mie
#define CSR_MTVEC CSR_mtvec
#define CSR_MCOUNTEREN CSR_mcounteren
#define CSR_MSCRATCH CSR_mscratch
#define CSR_MEPC CSR_mepc
#define CSR_MCAUSE CSR_mcause
#define CSR_MTVAL CSR_mtval
#define CSR_MIP CSR_mip
#define CSR_MCYCLE CSR_mcycle
#define CSR_MINSTRET CSR_minstret
#define CSR_MCOUNTINHIBIT CSR_mcountinhibit
#define CSR_CYCLE CSR_cycle
#define CSR_TIME CSR_time
#define CSR_INSTRET CSR_instret
#define CSR_MHPMCOUNTER3 CSR_mhpmcounter3
#define CSR_MHPMEVENT3 CSR_mhpmevent3
#define CSR_PMPCFG0 CSR_pmpcfg0
#define CSR_PMPADDR0 CSR_pmpaddr0

#endif /* RVISS_CPU_CSR_LIST_H */
