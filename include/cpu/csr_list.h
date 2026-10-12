#ifndef RVISS_CPU_CSR_LIST_H
#define RVISS_CPU_CSR_LIST_H

#include <common.h>
#include <cpu/csr_machine.h> /* MSTATUS_* 位定义；地址/存储清单仍由本头维护 */
#include <cpu/interrupt.h> /* SIE/SIP 掩码要用 IRQ_* 中断编号 */

/*
 * ★ CSR 清单：唯一事实来源 ★
 *
 * 一份清单生成两处（将来还会生成 difftest/checkpoint 的批量复制清单）：
 *   1. CPU_state 的存储成员         —— include/cpu/cpu.h
 *   2. CSR 访问表（地址/掩码/名字） —— src/target/riscv64/csr/csr_access.c
 *
 * 于是"加一个 CSR" = 在本文件里加两行（地址常量 + 清单一行），其余全自动。
 * 对照 NEMU：加一个 CSR 要改 CSRS 列表宏、写位域 union、在 csr_read/csr_write
 * 两个 switch 各加一个 case（三处）；这里只有一处。
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
enum {
  /* S 模式 */
  CSR_SSTATUS = 0x100,
  CSR_SIE = 0x104,
  CSR_STVEC = 0x105,
  CSR_SCOUNTEREN = 0x106,
  CSR_SSCRATCH = 0x140,
  CSR_SEPC = 0x141,
  CSR_SCAUSE = 0x142,
  CSR_STVAL = 0x143,
  CSR_SIP = 0x144,
  CSR_SATP = 0x180,

  /* M 模式 */
  CSR_MSTATUS = 0x300,
  CSR_MISA = 0x301,
  CSR_MEDELEG = 0x302,
  CSR_MIDELEG = 0x303,
  CSR_MIE = 0x304,
  CSR_MTVEC = 0x305,
  CSR_MCOUNTEREN = 0x306,
  CSR_MSCRATCH = 0x340,
  CSR_MEPC = 0x341,
  CSR_MCAUSE = 0x342,
  CSR_MTVAL = 0x343,
  CSR_MIP = 0x344,

  /* 只读信息段（0xf1x：csr[11:10]==3，会被自动判为只读） */
  CSR_MVENDORID = 0xf11,
  CSR_MARCHID = 0xf12,
  CSR_MIMPID = 0xf13,
  CSR_MHARTID = 0xf14,
  CSR_MCONFIGPTR = 0xf15,
};

#define CSR_ENUM(name, addr) CSR_##name = addr,

/* ---------------- 视图掩码 ---------------- */
#define CSR_MASK_ALL (~0ull)
#define SSTATUS_MASK \
  (MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP | MSTATUS_SUM | MSTATUS_MXR)
#define SIE_MASK ((1ull << IRQ_SSIP) | (1ull << IRQ_STIP) | (1ull << IRQ_SEIP))
#define SIP_MASK SIE_MASK

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
 * 加一个 CSR 的完整步骤（只在本文件内）：
 *   ① 在上面 enum 里加地址常量
 *   ② 在 RVISS_CSRS_OWNED 里加一行
 * 存储成员、访问表条目、日志名、特权/只读判定全部自动获得。
 */

#define RVISS_CSRS_OWNED(f)                                                   \
  f(mstatus, CSR_MSTATUS, CSR_MASK_ALL) f(misa, CSR_MISA, CSR_MASK_ALL) f(    \
      medeleg, CSR_MEDELEG, CSR_MASK_ALL)                                     \
      f(mideleg, CSR_MIDELEG, CSR_MASK_ALL) f(mie, CSR_MIE, CSR_MASK_ALL) f(  \
          mip, CSR_MIP, CSR_MASK_ALL) f(mtvec, CSR_MTVEC, CSR_MASK_ALL)       \
          f(mepc, CSR_MEPC, CSR_MASK_ALL) f(mcause, CSR_MCAUSE, CSR_MASK_ALL) \
              f(mtval, CSR_MTVAL, CSR_MASK_ALL) f(mscratch, CSR_MSCRATCH,     \
                                                  CSR_MASK_ALL)               \
                  f(mcounteren, CSR_MCOUNTEREN, CSR_MASK_ALL) f(              \
                      mvendorid, CSR_MVENDORID, CSR_MASK_ALL)                 \
                      f(marchid, CSR_MARCHID, CSR_MASK_ALL) f(                \
                          mimpid, CSR_MIMPID, CSR_MASK_ALL)                   \
                          f(mhartid, CSR_MHARTID, CSR_MASK_ALL) f(            \
                              mconfigptr, CSR_MCONFIGPTR, CSR_MASK_ALL)       \
                              f(stvec, CSR_STVEC, CSR_MASK_ALL) f(            \
                                  sepc, CSR_SEPC, CSR_MASK_ALL)               \
                                  f(scause, CSR_SCAUSE, CSR_MASK_ALL) f(      \
                                      stval, CSR_STVAL, CSR_MASK_ALL)         \
                                      f(sscratch, CSR_SSCRATCH, CSR_MASK_ALL) \
                                          f(satp, CSR_SATP, CSR_MASK_ALL)     \
                                              f(scounteren, CSR_SCOUNTEREN,   \
                                                CSR_MASK_ALL)

#define RVISS_CSRS_VIEW(f)                       \
  f(sstatus, CSR_SSTATUS, mstatus, SSTATUS_MASK) \
      f(sie, CSR_SIE, mie, SIE_MASK) f(sip, CSR_SIP, mip, SIP_MASK)

#endif /* RVISS_CPU_CSR_LIST_H */
