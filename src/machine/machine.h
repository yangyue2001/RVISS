#ifndef RVISS_MACHINE_MACHINE_H
#define RVISS_MACHINE_MACHINE_H

#include <common.h>

/*
 * 板级描述：一台机器 = 内存图 + 设备清单 + 复位地址。
 * 外设都是 MemObject。machine_init 先复位 CPU，再登记外设并接线。
 * 不复位仿真器，也不装镜像。
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

/* 复位 CPU，再按配置登记外设并接线。不复位仿真器。 */
void machine_init(const MachineConfig* cfg);
const MachineConfig* machine_get(void);

/* 按登记顺序调用 MemObject.advance，再处理板级宿主输入。 */
void machine_advance(uint64_t logical_ticks);

/*
 * 一步的板卡入口：按登记顺序调用 MemObject.update，再查询中断。
 * 有可响应中断则进入中断，否则执行一条指令。
 */
typedef struct StepResult StepResult;
void machine_update(StepResult* result);

#endif /* RVISS_MACHINE_MACHINE_H */
