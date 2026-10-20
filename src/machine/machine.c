#include <common.h>
#include <cpu/cpu.h>
#include <cpu/interrupt.h>
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

/* step.h 仍引用已删除的 MemAccess，这里只取执行入口。 */
void riscv_take_interrupt(StepResult* result, word_t cause);
void riscv_execute(StepResult* result);

/*
 * 板卡只保存已注册的不透明指针，并负责 MMIO 基址和中断线。
 * 外设结构体在各自的 .c 里。仿真器状态和 CPU 复位不在这里。
 */

#define MACHINE_DEV_MAX 8

static MachineConfig active;

#ifdef CONFIG_HAS_CLINT
static Clint* clints[MACHINE_DEV_MAX];
static int nr_clints;
#endif
#ifdef CONFIG_HAS_PLIC
static Plic* plics[MACHINE_DEV_MAX];
static int nr_plics;
#endif
#ifdef CONFIG_HAS_UART16550
static Uart16550* uarts[MACHINE_DEV_MAX];
static int nr_uarts;
static IrqLine uart_lines[MACHINE_UART_MAX];
static bool uart_host_rx;
#endif
#ifdef CONFIG_HAS_TEST_FINISHER
static TestFinisher* finishers[MACHINE_DEV_MAX];
static int nr_finishers;
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
  if (nr_uarts == 0) return;
  Uart16550* uart = uarts[0];
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
  if (nr_plics > 0) plic_set_source(plics[0], source, level);
}
#endif
#endif

const MachineConfig* machine_get(void) { return &active; }

#ifdef CONFIG_HAS_CLINT
static word_t machine_read_time(void* opaque) { return clint_mtime(opaque); }

Clint* machine_add_clint(paddr_t base, IrqLine* timer_irq,
                         IrqLine* software_irq) {
  if (nr_clints >= MACHINE_DEV_MAX) return NULL;
  Clint* clint = clint_create(timer_irq, software_irq);
  if (clint == NULL) return NULL;
  MemObject mmio;
  clint_mmio(clint, &mmio);
  if (!mmio_map(base, CLINT_MMIO_SIZE, &mmio)) {
    clint_release(clint);
    return NULL;
  }
  device_set_time_reader(machine_read_time, clint);
  clint_reset(clint);
  clints[nr_clints++] = clint;
  return clint;
}
#endif

#ifdef CONFIG_HAS_PLIC
Plic* machine_add_plic(paddr_t base, IrqLine* machine_external,
                       IrqLine* supervisor_external) {
  if (nr_plics >= MACHINE_DEV_MAX) return NULL;
  Plic* plic = plic_create(machine_external, supervisor_external);
  if (plic == NULL) return NULL;
  MemObject mmio;
  plic_mmio(plic, &mmio);
  if (!mmio_map(base, PLIC_MMIO_SIZE, &mmio)) {
    plic_release(plic);
    return NULL;
  }
  plic_reset(plic);
  plics[nr_plics++] = plic;
  return plic;
}
#endif

#ifdef CONFIG_HAS_UART16550
Uart16550* machine_add_uart(paddr_t base, IrqLine* irq,
                            void (*transmit)(void* opaque, uint8_t byte),
                            void* transmit_opaque) {
  if (nr_uarts >= MACHINE_DEV_MAX) return NULL;
  Uart16550* uart = uart16550_create(irq, transmit, transmit_opaque);
  if (uart == NULL) return NULL;
  MemObject mmio;
  uart16550_mmio(uart, &mmio);
  if (!mmio_map(base, UART16550_MMIO_SIZE, &mmio)) {
    uart16550_release(uart);
    return NULL;
  }
  uart16550_reset(uart);
  uarts[nr_uarts++] = uart;
  return uart;
}
#endif

#ifdef CONFIG_HAS_TEST_FINISHER
TestFinisher* machine_add_finisher(paddr_t base,
                                   void (*finish)(void* opaque, int code),
                                   void* opaque) {
  if (nr_finishers >= MACHINE_DEV_MAX) return NULL;
  TestFinisher* finisher = test_finisher_create(finish, opaque);
  if (finisher == NULL) return NULL;
  MemObject mmio;
  test_finisher_mmio(finisher, &mmio);
  if (!mmio_map(base, TEST_FINISHER_MMIO_SIZE, &mmio)) {
    test_finisher_release(finisher);
    return NULL;
  }
  finishers[nr_finishers++] = finisher;
  return finisher;
}
#endif

static void machine_register(const MachineConfig* cfg) {
#ifdef CONFIG_HAS_CLINT
  Assert(machine_add_clint(cfg->clint_base, cpu_irq_input(INT_MTI),
                           cpu_irq_input(INT_MSI)) != NULL,
         "CLINT 注册失败");
#endif
#ifdef CONFIG_HAS_PLIC
  Assert(machine_add_plic(cfg->plic_base, cpu_irq_input(INT_MEI),
                          cpu_irq_input(INT_SEI)) != NULL,
         "PLIC 注册失败");
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
    if (nr_plics > 0 && source != 0 && source < PLIC_NR_SOURCES) {
      uart_lines[i].set = uart_irq;
      uart_lines[i].ctx = (void*)(uintptr_t)source;
      line = &uart_lines[i];
    }
#endif
    Assert(
        machine_add_uart(cfg->uart[i].base, line, uart_transmit, NULL) != NULL,
        "UART 注册失败");
  }
  if (cfg->nr_uart > 0) uart_host_rx = true;
#endif
#ifdef CONFIG_HAS_TEST_FINISHER
  Assert(machine_add_finisher(cfg->test_base, finish_guest, NULL) != NULL,
         "test finisher 注册失败");
#endif
  (void)cfg;
}

void machine_init(const MachineConfig* cfg) {
  active = *cfg;
  Log("machine: %s", active.name);
  machine_register(&active);
}

void machine_reset(void) {
#ifdef CONFIG_HAS_CLINT
  for (int i = 0; i < nr_clints; i++) clint_reset(clints[i]);
#endif
#ifdef CONFIG_HAS_UART16550
  for (int i = 0; i < nr_uarts; i++) uart16550_reset(uarts[i]);
#endif
#ifdef CONFIG_HAS_PLIC
  for (int i = 0; i < nr_plics; i++) plic_reset(plics[i]);
#endif
}

void machine_advance(uint64_t logical_ticks) {
#ifdef CONFIG_HAS_CLINT
  for (int i = 0; i < nr_clints; i++) clint_advance(clints[i], logical_ticks);
#endif
#ifdef CONFIG_HAS_UART16550
  for (int i = 0; i < nr_uarts; i++) uart16550_advance(uarts[i], logical_ticks);
  if (uart_host_rx) uart_host_poll();
#endif
}

uint64_t machine_next_deadline(void) {
  uint64_t next = UINT64_MAX;
#ifdef CONFIG_HAS_CLINT
  for (int i = 0; i < nr_clints; i++) {
    const uint64_t at = clint_deadline(clints[i]);
    if (at < next) next = at;
  }
#endif
#ifdef CONFIG_HAS_UART16550
  for (int i = 0; i < nr_uarts; i++) {
    const uint64_t at = uart16550_deadline(uarts[i]);
    if (at < next) next = at;
  }
#endif
  return next;
}

void machine_update(StepResult* result) {
#ifdef CONFIG_HAS_CLINT
  for (int i = 0; i < nr_clints; i++) clint_update(clints[i]);
#endif
#ifdef CONFIG_HAS_UART16550
  for (int i = 0; i < nr_uarts; i++) uart16550_update(uarts[i]);
#endif
#ifdef CONFIG_HAS_PLIC
  for (int i = 0; i < nr_plics; i++) plic_update(plics[i]);
#endif

  riscv_step(result);
}
