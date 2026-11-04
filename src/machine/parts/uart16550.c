#include <device/char/uart16550.h>
#include <device/intc/plic.h>
#include <host/host.h>
#include <util/config.h>
#include <util/log.h>
#include <util/state.h>

#include "../machine.h"

/*
 * 16550 的板级部分。可以有多路。
 * map 只分配并映射。connect 再把发送回调和 PLIC 源接上。
 * 第一路是控制台，poll 从宿主读入。
 */

static struct {
  Uart16550* uart;
  unsigned source;
  IrqLine line;
} slots[MACHINE_NODE_MAX];
static int nr_slots;
static Uart16550* console_uart;
static Plic* wired_plic;

static void uart_transmit(void* opaque, uint8_t byte) {
  (void)opaque;
  putchar(byte);
  fflush(stdout);
}

static void uart_irq(void* opaque, bool level) {
  unsigned source = (unsigned)(uintptr_t)opaque;
  Assert(wired_plic != NULL, "UART 中断时 PLIC 为空");
  plic_set_source(wired_plic, source, level);
}

static void uart_map(const MachineNode* node) {
  if (nr_slots == 0) {
    Assert(host_uart_input_init(rt_cfg.uart_record, rt_cfg.uart_replay),
           "UART input initialization failed");
  }
  Assert(nr_slots < MACHINE_NODE_MAX, "UART 数量超过板卡上限");
  MemObject object = uart16550_init();
  Uart16550* uart = object.ctx;
  machine_map(node->base, UART16550_MMIO_SIZE, &object);
  slots[nr_slots].uart = uart;
  slots[nr_slots].source = node->irq;
  nr_slots++;
  if (console_uart == NULL) console_uart = uart;
}

static void uart_connect(void) {
  wired_plic = machine_device("plic");
  for (int i = 0; i < nr_slots; i++) {
    Uart16550* uart = slots[i].uart;
    uart->transmit = uart_transmit;
    uart->transmit_opaque = NULL;
    unsigned source = slots[i].source;
    if (wired_plic == NULL || plic_set_source == NULL || source == 0 ||
        source >= PLIC_NR_SOURCES) {
      continue;
    }
    slots[i].line.set = uart_irq;
    slots[i].line.ctx = (void*)(uintptr_t)source;
    uart->irq = &slots[i].line;
  }
}

static void uart_poll(void) {
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

static void uart_register(void) __attribute__((constructor));
static void uart_register(void) {
  static const BoardPart part = {
      .name = "uart16550",
      .map = uart_map,
      .connect = uart_connect,
      .poll = uart_poll,
  };
  machine_add_part(&part);
}
