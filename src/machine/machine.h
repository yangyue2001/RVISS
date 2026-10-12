#ifndef RVISS_MACHINE_MACHINE_H
#define RVISS_MACHINE_MACHINE_H

#include <common.h>

/*
 * 板级描述：一台"机器"= 内存图 + 设备清单 + 复位地址。
 * 换板子只改 boards 目录下的板级描述文件，内核/设备/ISA 代码不动。
 */

typedef struct {
  const char *name;

  paddr_t ram_base;
  size_t  ram_size;
  vaddr_t reset_pc;

  paddr_t clint_base;
  paddr_t uart_base;
  paddr_t plic_base;
} MachineConfig;

extern const MachineConfig machine_minimal;

/* 分配内存、实例化设备、复位 CPU */
void machine_init(const MachineConfig *cfg);

/* 复位整机（CPU + 设备）；供将来的 reset 命令 / checkpoint 恢复使用 */
void machine_reset(void);

/* 当前生效的机器配置（供 monitor 查 RAM 基址等） */
const MachineConfig *machine_get(void);

#endif /* RVISS_MACHINE_MACHINE_H */
