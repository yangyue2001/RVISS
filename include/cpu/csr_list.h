#ifndef RVISS_CPU_CSR_LIST_H
#define RVISS_CPU_CSR_LIST_H

#include <common.h>
#include <cpu/csr_machine.h> /* MSTATUS_* 位定义；地址/存储清单仍由本头维护 */
#include <cpu/interrupt.h>   /* SIE/SIP 掩码要用 IRQ_* 中断编号 */

#include "csr_machine.h"
#include "csr_supervisor.h"
#include "csr_user.h"
/*
 * ★ CSR 清单：唯一事实来源 ★
 *
 * 一份清单生成两处（将来还会生成 difftest/checkpoint 的批量复制清单）：
 *   1. CPU_state 的存储成员         —— include/cpu/cpu.h
 *   2. CSR 访问表（地址/掩码/名字） —— src/target/riscv64/csr/csr_access.c
 *
 * 清单统一维护地址和存储；WARL、别名与副作用在访问实现中显式处理。
 *
 * 两条设计决定（都学自 NEMU 里更稳的做法）：
 *   - **特权级与只读属性不写进表**：RISC-V 把它们编码在 CSR 地址位里
 *     （csr[9:8] = 访问所需最低特权级；csr[11:10] == 3 表示只读）。
 *     由 csr_access.c 从地址推导 —— 数据里没有"可以填错"的策略字段。
 *   - **清单驱动一切**：不会出现"表里加了、别的清单忘了"这类脱节。
 *
 * "视图"CSR：sstatus/sie/sip 没有自己的存储 —— 它们是 mstatus/mie/mip
 * 在 S 模式视角下的投影，用掩码表达（见 RVISS_CSRS_VIEW）。
 *
 * 本文件放在 include/cpu/（公共头）而不是 target 私有头：CPU_state 的定义
 * 需要它；且 RVISS 只支持 RV64，不存在 ISA 中立性的顾虑。
 */

/* ---------------- CSR 地址（privileged spec Vol.II 的 CSR 表）
 * ---------------- */
#define CSR_ENUM(name, addr) CSR_##name = addr,
#define CSRS_All(f) CSRS_Unprivileged(f) CSRS_Supervisor(f) CSRS_Machine(f)
enum { MAP(CSRS_All, CSR_ENUM) };

/*
 * 访问实现和现有测试使用规范中常见的全大写 CSR 常量；新的分组清单使用
 * 小写字段名以便直接生成 CPU_state 成员。保留这一层别名，避免把字段成员
 * 命名和 CSR 地址常量命名强行绑定。
 */
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

/* ---------------- 视图掩码 ---------------- */
#define CSR_MASK_ALL (~0ull)
#define SSTATUS_MASK                                                     \
  (MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP | MSTATUS_FS | MSTATUS_SUM | \
   MSTATUS_MXR | MSTATUS_UXL | MSTATUS_SD)
#define SSTATUS_WRITABLE_MASK                                            \
  (MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP | MSTATUS_FS | MSTATUS_SUM | \
   MSTATUS_MXR)
#define MSTATUS_WRITABLE_MASK                                         \
  (SSTATUS_WRITABLE_MASK | MSTATUS_MIE | MSTATUS_MPIE | MSTATUS_MPP | \
   MSTATUS_MPRV | MSTATUS_TVM | MSTATUS_TW | MSTATUS_TSR)
#define SIE_MASK ((1ull << IRQ_SSIP) | (1ull << IRQ_STIP) | (1ull << IRQ_SEIP))
#define SIP_MASK SIE_MASK
#define MIE_MASK (SIE_MASK | MIP_MSIP | MIP_MTIP | MIP_MEIP)
#define MEDELEG_MASK \
  UINT64_C(0xb3ff) /* 0..9,12,13,15；M ECALL/保留位不可委派 */

/* ---------------- ★ 清单 ★ ----------------
 *
 * RVISS_CSRS_OWNED(f): 有独立存储的 CSR —— f(名字, 地址, 可见掩码)
 *   "名字"同时是 CPU_state 的成员名（cpu.<名字>）、表项名和日志名。
 *
 * RVISS_CSRS_VIEW(f):  视图 CSR —— f(名字, 地址, 存储所在字段, 可见掩码)
 *   sstatus/sie/sip 读写的是 mstatus/mie/mip，只是可见位被掩码限制。
 *
 * 顺序 = CPU_state 里成员的顺序（改动顺序会改变结构体布局）。
 *
 * 新增 CSR 元信息：
 *   ① 在上面 enum 里加地址常量
 *   ② 在 RVISS_CSRS_OWNED 里加一行
 * 存储成员与表条目自动生成；非平凡规则还须实现访问处理和测试。
 */

#define RVISS_CSRS_OWNED(f)                                                    \
  f(cycle, CSR_MCYCLE, CSR_MASK_ALL) f(instret, CSR_MINSTRET, CSR_MASK_ALL) f( \
      mstatus, CSR_MSTATUS, CSR_MASK_ALL) f(misa, CSR_MISA, CSR_MASK_ALL)      \
      f(medeleg, CSR_MEDELEG, CSR_MASK_ALL) f(                                 \
          mideleg, CSR_MIDELEG, CSR_MASK_ALL) f(mie, CSR_MIE, CSR_MASK_ALL)    \
          f(mip, CSR_MIP, CSR_MASK_ALL) f(mtvec, CSR_MTVEC, CSR_MASK_ALL) f(   \
              mepc, CSR_MEPC, CSR_MASK_ALL)                                    \
              f(mcause, CSR_MCAUSE, CSR_MASK_ALL) f(                           \
                  mtval, CSR_MTVAL, CSR_MASK_ALL) f(mscratch, CSR_MSCRATCH,    \
                                                    CSR_MASK_ALL)              \
                  f(mcounteren, CSR_MCOUNTEREN, CSR_MASK_ALL) f(               \
                      mvendorid, CSR_MVENDORID, CSR_MASK_ALL)                  \
                      f(marchid, CSR_MARCHID, CSR_MASK_ALL) f(                 \
                          mimpid, CSR_MIMPID, CSR_MASK_ALL)                    \
                          f(mhartid, CSR_MHARTID, CSR_MASK_ALL) f(             \
                              mconfigptr, CSR_MCONFIGPTR, CSR_MASK_ALL)        \
                              f(stvec, CSR_STVEC, CSR_MASK_ALL) f(             \
                                  sepc, CSR_SEPC, CSR_MASK_ALL)                \
                                  f(scause, CSR_SCAUSE, CSR_MASK_ALL) f(       \
                                      stval, CSR_STVAL, CSR_MASK_ALL)          \
                                      f(sscratch, CSR_SSCRATCH, CSR_MASK_ALL)  \
                                          f(satp, CSR_SATP, CSR_MASK_ALL)      \
                                              f(scounteren, CSR_SCOUNTEREN,    \
                                                CSR_MASK_ALL)                  \
                                                  f(fcsr, CSR_FCSR, 0xff)

#define RVISS_CSRS_COMPUTED(f)                                                \
  f(time, CSR_TIME) f(mcountinhibit, CSR_MCOUNTINHIBIT) f(fflags, CSR_FFLAGS) \
      f(frm, CSR_FRM)

#define RVISS_HPM_INDICES(f)                                                   \
  f(3) f(4) f(5) f(6) f(7) f(8) f(9) f(10) f(11) f(12) f(13) f(14) f(15) f(16) \
      f(17) f(18) f(19) f(20) f(21) f(22) f(23) f(24) f(25) f(26) f(27) f(28)  \
          f(29) f(30) f(31)

#define RVISS_PMP_INDICES(f)                                                  \
  f(0) f(1) f(2) f(3) f(4) f(5) f(6) f(7) f(8) f(9) f(10) f(11) f(12) f(13)   \
      f(14) f(15) f(16) f(17) f(18) f(19) f(20) f(21) f(22) f(23) f(24) f(25) \
          f(26) f(27) f(28) f(29) f(30) f(31) f(32) f(33) f(34) f(35) f(36)   \
              f(37) f(38) f(39) f(40) f(41) f(42) f(43) f(44) f(45) f(46)     \
                  f(47) f(48) f(49) f(50) f(51) f(52) f(53) f(54) f(55) f(56) \
                      f(57) f(58) f(59) f(60) f(61) f(62) f(63)

#define RVISS_PMPCFG_INDICES(f) f(0) f(2) f(4) f(6) f(8) f(10) f(12) f(14)

#define RVISS_CSRS_VIEW(f)                                \
  f(cycle_view, CSR_CYCLE, cycle, CSR_MASK_ALL)           \
      f(instret_view, CSR_INSTRET, instret, CSR_MASK_ALL) \
          f(sstatus, CSR_SSTATUS, mstatus, SSTATUS_MASK)  \
              f(sie, CSR_SIE, mie, SIE_MASK) f(sip, CSR_SIP, mip, SIP_MASK)

#endif /* RVISS_CPU_CSR_LIST_H */
