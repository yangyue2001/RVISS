#ifndef RVISS_DIFFTEST_H
#define RVISS_DIFFTEST_H

/*
 * 给外部 difftest 链接或 dlopen 的 ref ABI，形状参考 NEMU：
 * init、memcpy、regcpy、exec，再加上由调用方注入的中断和同步异常。
 *
 * 这不是内部 CpuState 的内存布局，也不能当作 XiangShan 的寄存器镜像。
 * 库里只有 CPU 和 RAM，不映射 CLINT/PLIC/UART，也不会自己采样异步中断。
 * 复位 PC 和 RAM 范围来自编译这份库时的 CONFIG_MBASE、CONFIG_MSIZE、
 * CONFIG_PC_RESET_OFFSET。
 *
 * direction：DIFFTEST_TO_DUT 从 ref 读出，DIFFTEST_TO_REF 写进 ref。
 * difftest_exec(n) 的 n 是指令尝试次数，含同步异常，不含这里注入的陷阱。
 * 注入不看 mie 和 mstatus 的全局使能；委托仍按当前特权级和 medeleg/mideleg。
 * 调用前用 regcpy 把特权级和委托位对齐到对方。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DIFFTEST_TO_DUT = 0, DIFFTEST_TO_REF = 1 };

#define RVISS_REF_API __attribute__((visibility("default")))

/* 字段顺序和宽度是 ABI。sstatus/sie/sip 不是独立存储，不在这里重复。 */
typedef struct {
  uint64_t gpr[32];
  uint64_t fpr[32];
  uint64_t fcsr;
  uint64_t pc;
  uint64_t priv;
  uint64_t mstatus;
  uint64_t medeleg;
  uint64_t mideleg;
  uint64_t mie;
  uint64_t mip;
  uint64_t mtvec;
  uint64_t mscratch;
  uint64_t mepc;
  uint64_t mcause;
  uint64_t mtval;
  uint64_t mcounteren;
  uint64_t mcountinhibit;
  uint64_t mcycle;
  uint64_t minstret;
  uint64_t mhartid;
  uint64_t stvec;
  uint64_t scounteren;
  uint64_t sscratch;
  uint64_t sepc;
  uint64_t scause;
  uint64_t stval;
  uint64_t satp;
  uint64_t pmpcfg[16];
  uint64_t pmpaddr[16];
  uint64_t lr_valid;
  uint64_t lr_addr;
  uint64_t lr_len;
  uint64_t wfi;
} RvissRefState;

RVISS_REF_API uint64_t difftest_state_size(void);
RVISS_REF_API uint64_t difftest_ram_base(void);
RVISS_REF_API uint64_t difftest_ram_size(void);

/* 建 RAM、复位 CPU、进入可执行状态。可以再次调用，内存和寄存器都会清掉。 */
RVISS_REF_API void difftest_init(void);
/* 只拷 RAM。区间必须整个落在 RAM 内；n 为 0 时不访问 buf。 */
RVISS_REF_API void difftest_memcpy(uint64_t addr, void* buf, size_t n,
                                   int direction);
/* 整份 RvissRefState。x0 固定为 0，mepc/sepc 的 bit0 清掉，fcsr 只留低 8 位，
 * pmpaddr 只留 [53:0]。导出时 mstatus.SD 按 FS/VS/XS 重算，其它 CSR 按可见读值。 */
RVISS_REF_API void difftest_regcpy(void* dut, int direction);
/* 至多 n 次取指执行。WFI 睡下后本次剩余尝试不再执行。 */
RVISS_REF_API void difftest_exec(uint64_t n);
/* cause 就是 mcause：bit63 为 1 表示中断。原因码必须小于 64。epc 用当前 pc。 */
RVISS_REF_API void difftest_raise_trap(uint64_t cause, uint64_t tval);

#ifdef __cplusplus
}
#endif

#endif /* RVISS_DIFFTEST_H */
