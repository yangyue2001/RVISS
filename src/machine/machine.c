#include <common.h>
#include <cpu/cpu.h>
#include <cpu/interrupt.h>
#include <cpu/step.h>
#include <device/device.h>
#include <host/host.h>
#include <memory/paddr.h>
#include <trap.h>
#include <util/config.h>
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
#ifdef CONFIG_HAS_TEST_FINISHER
#include <device/misc/test_finisher.h>
#endif

#include "machine.h"

/*
 * 板卡 = CPU + 一堆外设。外设 init 分配、复位，再返回 MemObject。
 * 中断线和回调由这里写进 ctx，再按基址映射。
 * 先复位 CPU，再登记外设，避免外设复位拉高的中断被清掉。
 * 仿真器复位和镜像不在这里。
 */

static MachineConfig active;

#ifdef CONFIG_HAS_PLIC
static Plic* board_plic;
#endif
#ifdef CONFIG_HAS_UART16550
static Uart16550* console_uart;
static IrqLine uart_lines[MACHINE_UART_MAX];
#endif

#ifdef CONFIG_HAS_TEST_FINISHER
static void finish_guest(void* opaque, int code) {
  (void)opaque;
  sim_halt(cpu.pc, code);
}
#endif

#ifdef CONFIG_HAS_UART16550
static void uart_transmit(void* opaque, uint8_t byte) {
  (void)opaque;
  putchar(byte);
  fflush(stdout);
}

static void uart_host_poll(void) {
  Uart16550* uart = console_uart;
  Assert(uart != NULL, "控制台 UART 为空");
  if ((!rt_cfg.uart_stdin && !host_uart_replaying()) ||
      uart16550_loopback(uart))
    return;
  if (!host_uart_replaying() && sim.event_seq % 1024) return;
  uint8_t bytes[16];
  int count =
      host_uart_input(sim.event_seq, bytes, uart16550_rx_capacity(uart));
  if (count < 0) {
    sim.state = SIM_ABORTED;
    Loge("UART host input failed");
    return;
  }
  for (int i = 0; i < count; i++) uart16550_receive(uart, bytes[i]);
}

#ifdef CONFIG_HAS_PLIC
static void uart_irq(void* opaque, bool level) {
  unsigned source = (unsigned)(uintptr_t)opaque;
  Assert(board_plic != NULL, "UART 中断时 PLIC 为空");
  plic_set_source(board_plic, source, level);
}
#endif
#endif

const MachineConfig* machine_get(void) { return &active; }

static bool map_device(paddr_t base, paddr_t size, MemObject* object) {
  Assert(object != NULL && object->ctx != NULL, "设备没有实例");
  return mem_region_add(base, size, object);
}

static void machine_register(const MachineConfig* cfg) {
  Assert(mem_init(cfg->ram_base, cfg->ram_size), "RAM 初始化失败");
  Assert(mem_install_ram(cfg->ram_base, cfg->ram_size), "RAM 登记失败");
#ifdef CONFIG_HAS_CLINT
  {
    MemObject object = clint_init();
    Clint* clint = object.ctx;
    clint->mtimer_irq[0] = cpu_irq_input(INT_MTI);
    clint->msip_irq[0] = cpu_irq_input(INT_MSI);
    Assert(map_device(cfg->clint_base, CLINT_MMIO_SIZE, &object),
           "CLINT 注册失败");
  }
#endif
#ifdef CONFIG_HAS_PLIC
  {
    MemObject object = plic_init();
    Plic* plic = object.ctx;
    plic->external_irq[0] = cpu_irq_input(INT_MEI);
    plic->external_irq[1] = cpu_irq_input(INT_SEI);
    Assert(map_device(cfg->plic_base, PLIC_MMIO_SIZE, &object),
           "PLIC 注册失败");
    board_plic = plic;
  }
#endif
#ifdef CONFIG_HAS_UART16550
  Assert(host_uart_input_init(rt_cfg.uart_record, rt_cfg.uart_replay),
         "UART input initialization failed");
  Assert(cfg->nr_uart >= 0 && cfg->nr_uart <= MACHINE_UART_MAX,
         "UART 数量超过板卡上限");
  for (int i = 0; i < cfg->nr_uart; i++) {
    IrqLine* line = NULL;
#ifdef CONFIG_HAS_PLIC
    unsigned source = cfg->uart[i].plic_source;
    if (board_plic != NULL && source != 0 && source < PLIC_NR_SOURCES) {
      uart_lines[i].set = uart_irq;
      uart_lines[i].ctx = (void*)(uintptr_t)source;
      line = &uart_lines[i];
    }
#endif
    MemObject object = uart16550_init();
    Uart16550* uart = object.ctx;
    uart->irq = line;
    uart->transmit = uart_transmit;
    uart->transmit_opaque = NULL;
    Assert(map_device(cfg->uart[i].base, UART16550_MMIO_SIZE, &object),
           "UART 注册失败");
    if (console_uart == NULL) console_uart = uart;
  }
#endif
#ifdef CONFIG_HAS_TEST_FINISHER
  {
    MemObject object = test_finisher_init();
    TestFinisher* finisher = object.ctx;
    finisher->finish = finish_guest;
    finisher->finish_opaque = NULL;
    Assert(map_device(cfg->test_base, TEST_FINISHER_MMIO_SIZE, &object),
           "test finisher 注册失败");
  }
#endif
}

void machine_init(const MachineConfig* cfg) {
  active = *cfg;
  Log("machine: %s", active.name);
  cpu_reset(cfg->reset_pc);
  machine_register(&active);
}

void machine_advance(uint64_t logical_ticks) {
  for (int i = 0; i < mem_region_count(); i++) {
    const MemObject* object = &mem_region_at(i)->object;
    if (object->advance != NULL) object->advance(object->ctx, logical_ticks);
  }
#ifdef CONFIG_HAS_UART16550
  if (console_uart != NULL) uart_host_poll();
#endif
}

void machine_update(StepResult* result) {
  for (int i = 0; i < mem_region_count(); i++) {
    const MemObject* object = &mem_region_at(i)->object;
    if (object->update != NULL) object->update(object->ctx);
  }
  riscv_step(result);
}
