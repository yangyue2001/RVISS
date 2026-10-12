#include <common.h>
#include <cpu/cpu.h>
#include <cpu/interrupt.h>
#include <device/device.h>
#include <memory/paddr.h>
#include <util/log.h>
#include <util/state.h>

#ifdef CONFIG_HAS_CLINT
#include <device/timer/clint.h>
#endif
#ifdef CONFIG_HAS_UART16550
#include <device/char/uart16550.h>
#endif
#ifdef CONFIG_HAS_PLIC
#include <device/intc/plic.h>
#endif

#include "machine.h"

/*
 * 机器装配（板级代码）—— 唯一知道"这台机器有什么设备、各自映射在哪、
 * 中断线怎么接"的地方。对应 QEMU 里 hw/riscv/virt.c 这类板级文件：
 *   - 设备自己不知道地址（mmio_map 由板子调用）
 *   - 设备自己不知道中断线接到哪（cpu_irq_input 由板子调用）
 *   - 设备实例的存储也在板子（static 实例），设备不再用文件级全局状态
 */

static MachineConfig active;

#ifdef CONFIG_HAS_CLINT
static ClintState clint;
#endif
#ifdef CONFIG_HAS_UART16550
static Uart16550State uart;
#endif
#ifdef CONFIG_HAS_PLIC
static PlicState plic;
#endif

const MachineConfig *machine_get(void) {
  return &active;
}

/* 建实例 → 映射地址 → 接中断线 */
static void machine_build(const MachineConfig *cfg) {
  (void)cfg; /* 设备全关时本函数为空 */

#ifdef CONFIG_HAS_CLINT
  clint_init(&clint);
  device_set_time_reader(clint_time,&clint);
  Assert(mmio_map(&clint.mmio, cfg->clint_base), "CLINT 地址映射失败");
#endif
#ifdef CONFIG_HAS_UART16550
  uart16550_init(&uart);
  Assert(mmio_map(&uart.mmio, cfg->uart_base), "UART 地址映射失败");
#endif
#ifdef CONFIG_HAS_PLIC
  plic_init(&plic);
  Assert(mmio_map(&plic.mmio, cfg->plic_base), "PLIC 地址映射失败");
#endif

  /* 中断接线：设备的"输出线"接到 CPU 的某个 mip 位 —— 只有板子知道这个映射 */
#ifdef CONFIG_HAS_CLINT
  clint.mtimer_irq = cpu_irq_input(IRQ_MTIP);
  clint.msip_irq = cpu_irq_input(IRQ_MSIP);
#endif
#ifdef CONFIG_HAS_PLIC
  plic.meip_irq = cpu_irq_input(IRQ_MEIP);
#endif
}

static void machine_reset_devices(void) {
#ifdef CONFIG_HAS_CLINT
  clint_reset(&clint);
#endif
#ifdef CONFIG_HAS_UART16550
  uart16550_reset(&uart);
#endif
#ifdef CONFIG_HAS_PLIC
  plic_reset(&plic);
#endif
}

void machine_init(const MachineConfig *cfg) {
  sim_reset();
  active = *cfg;
  Log("machine: %s", active.name);

  if (!mem_init(active.ram_base, active.ram_size)) {
    panic("内存初始化失败");
  }

  bus_init(active.ram_base, active.ram_size); /* 总线知道 RAM 范围，用于映射校验 */
  cpu_reset(active.reset_pc);                 /* 含设备中断线电平快照的清零 */

  machine_build(&active);
  machine_reset_devices(); /* 设备状态清零，并保证所有输出线为低 */
}

/* 复位整机：CPU + 全部设备（供将来 reset 命令 / checkpoint 恢复用） */
void machine_reset(void) {
  sim_reset();
  cpu_reset(active.reset_pc);
  machine_reset_devices();
}
