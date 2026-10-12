#include <common.h>
#include <cpu/cpu.h>
#include <cpu/interrupt.h>
#include <device/device.h>
#include <memory/paddr.h>
#include <util/log.h>
#include <util/state.h>
#include <util/config.h>
#include <host/host.h>
#ifdef CONFIG_HAS_TEST_FINISHER
#include <device/misc/test_finisher.h>
#endif

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

#ifdef CONFIG_HAS_TEST_FINISHER
static TestFinisherState finisher;
static void finish_guest(void *opaque, int code) {
  (void)opaque; sim_halt(cpu.pc, code);
}
#endif
#ifdef CONFIG_HAS_UART16550
static void uart_transmit(void *opaque, uint8_t byte) {
  (void)opaque; putchar(byte); fflush(stdout);
}
static void uart_poll(void *opaque, uint64_t ticks) {
  (void)opaque; (void)ticks;
  if ((!rt_cfg.uart_stdin && !host_uart_replaying()) || (uart.mcr & 0x10)) return;
  if (!host_uart_replaying() && sim.event_seq % 1024) return;
  uint8_t bytes[16];
  int n = host_uart_input(sim.event_seq, bytes, uart16550_capacity(&uart));
  if (n < 0) { sim.state=SIM_ABORTED; Loge("UART host input failed"); return; }
  for (int i=0; i<n; i++) uart16550_receive(&uart, bytes[i]);
}
#ifdef CONFIG_HAS_PLIC
static void uart_irq(void *opaque, bool level) { plic_set_source(opaque, 10, level); }
static IrqLine uart_output = {.set=uart_irq, .ctx=&plic};
#endif
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
  Assert(host_uart_input_init(rt_cfg.uart_record, rt_cfg.uart_replay), "UART input initialization failed");
  uart.transmit=uart_transmit;
  Assert(device_register_tick(uart_poll, NULL), "UART host input registration failed");
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
  plic.seip_irq = cpu_irq_input(IRQ_SEIP);
#ifdef CONFIG_HAS_UART16550
  uart.irq = &uart_output;
#endif
#endif
#ifdef CONFIG_HAS_TEST_FINISHER
  test_finisher_init(&finisher);
  finisher.finish=finish_guest;
  Assert(mmio_map(&finisher.mmio, cfg->test_base), "test finisher mapping failed");
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
