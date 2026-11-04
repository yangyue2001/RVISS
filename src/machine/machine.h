#ifndef RVISS_MACHINE_MACHINE_H
#define RVISS_MACHINE_MACHINE_H

#include <common.h>
#include <device/device.h>

/*
 * 板级描述：一台机器 = 内存图 + 设备清单 + 复位地址。
 * 清单只写名字、基址和中断源。设备自己登记 map 和 connect。
 * machine_init 先复位 CPU，再按清单映射，最后把中断线接上。
 * 不复位仿真器，也不装镜像。
 */

#define MACHINE_NODE_MAX 16
#define MACHINE_PART_MAX 16

typedef struct {
  const char* name;
  paddr_t base;
  unsigned irq; /* PLIC 源号。0 表示这路不接 PLIC */
} MachineNode;

typedef struct MachineConfig {
  const char* name;

  paddr_t ram_base;
  size_t ram_size;
  vaddr_t reset_pc;

  MachineNode node[MACHINE_NODE_MAX];
  int nr_node;
} MachineConfig;

/*
 * 一种设备一份。map 只分配并映射；connect 在全部映射之后接线。
 * poll 可为空，只给需要看宿主输入的设备。
 */
typedef struct BoardPart {
  const char* name;
  void (*map)(const MachineNode* node);
  void (*connect)(void);
  void (*poll)(void);
} BoardPart;

extern const MachineConfig machine_minimal;

/* 设备文件在加载时调用。名字要和板级清单里的 name 一致。 */
void machine_add_part(const BoardPart* part);

/* 复位 CPU，再按清单映射并接线。不复位仿真器。 */
void machine_init(const MachineConfig* cfg);
const MachineConfig* machine_get(void);

/* 部件在 map 里调用。失败直接停机。 */
void machine_map(paddr_t base, paddr_t size, const MemObject* object);

/* 按 MemObject 名字找已经映射的实例。没有则返回 NULL。 */
void* machine_device(const char* name);

/* 按登记顺序调用 MemObject.advance，再处理板级宿主输入。 */
void machine_advance(uint64_t logical_ticks);

/*
 * 一步的板卡入口：按登记顺序调用 MemObject.update，再查询中断。
 * 有可响应中断则进入中断，否则执行一条指令。
 */
typedef struct StepResult StepResult;
void machine_update(StepResult* result);

#endif /* RVISS_MACHINE_MACHINE_H */
