#ifndef RVISS_MACHINE_MACHINE_H
#define RVISS_MACHINE_MACHINE_H

#include <common.h>
#include <device/irq.h>

/*
 * 板级描述：一台机器 = 内存图 + 设备清单 + 复位地址。
 * 外设实例不放在这里。machine_init 只注册外设并连接中断，
 * 不复位仿真器，也不复位 CPU。
 */

#define MACHINE_UART_MAX 4

typedef struct {
  paddr_t base;
  unsigned plic_source; /* 0 表示不接到 PLIC */
} MachineUart;

typedef struct {
  const char* name;

  paddr_t ram_base;
  size_t ram_size;
  vaddr_t reset_pc;

  paddr_t clint_base;
  paddr_t plic_base;
  paddr_t test_base;
  MachineUart uart[MACHINE_UART_MAX];
  int nr_uart;
} MachineConfig;

extern const MachineConfig machine_minimal;

/* 初始化内存图，按配置注册外设并连接中断线。 */
void machine_init(const MachineConfig* cfg);
/* 只复位已经注册的外设。 */
void machine_reset(void);
const MachineConfig* machine_get(void);

#ifdef CONFIG_HAS_CLINT
typedef struct Clint Clint;
Clint* machine_add_clint(paddr_t base, IrqLine* timer_irq, IrqLine* software_irq);
#endif
#ifdef CONFIG_HAS_PLIC
typedef struct Plic Plic;
Plic* machine_add_plic(paddr_t base, IrqLine* machine_external,
                       IrqLine* supervisor_external);
#endif
#ifdef CONFIG_HAS_UART16550
typedef struct Uart16550 Uart16550;
Uart16550* machine_add_uart(paddr_t base, IrqLine* irq,
                            void (*transmit)(void* opaque, uint8_t byte),
                            void* transmit_opaque);
#endif
#ifdef CONFIG_HAS_TEST_FINISHER
typedef struct TestFinisher TestFinisher;
TestFinisher* machine_add_finisher(paddr_t base,
                                   void (*finish)(void* opaque, int code),
                                   void* opaque);
#endif

/* 同一个逻辑时间推进全部已注册外设。 */
void machine_advance(uint64_t logical_ticks);

/*
 * 一步的板卡入口：刷新已注册的 UART/CLINT/PLIC，再查询中断。
 * 有可响应中断则进入中断，否则执行一条指令。
 */
typedef struct StepResult StepResult;
void machine_update(StepResult* result);

#endif /* RVISS_MACHINE_MACHINE_H */
