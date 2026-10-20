#include <device/char/uart16550.h>

#define UART_MAX 8

struct Uart16550 {
  IrqLine* irq;
  void (*transmit)(void* opaque, uint8_t byte);
  void* transmit_opaque;
  uint8_t dll, dlm, ier, fcr, lcr, mcr, scr, msr;
  uint8_t fifo[16], head, count;
  bool overrun, thre_pending, timeout;
  uint64_t now, rx_activity;
};

static Uart16550 pool[UART_MAX];
static bool used[UART_MAX];

static Uart16550* uart_alloc(void) {
  for (int i = 0; i < UART_MAX; i++) {
    if (!used[i]) {
      used[i] = true;
      memset(&pool[i], 0, sizeof(pool[i]));
      return &pool[i];
    }
  }
  return NULL;
}

static bool uart_owned(const Uart16550* uart) {
  return uart != NULL && uart >= pool && uart < pool + UART_MAX;
}

static unsigned trigger(const Uart16550* uart) {
  static const unsigned levels[] = {1, 4, 8, 14};
  return (uart->fcr & 1) ? levels[uart->fcr >> 6] : 1;
}

unsigned uart16550_rx_capacity(const Uart16550* uart) {
  if (uart == NULL) return 0;
  return ((uart->fcr & 1) ? 16 : 1) - uart->count;
}

bool uart16550_loopback(const Uart16550* uart) {
  return uart != NULL && (uart->mcr & 0x10) != 0;
}

static unsigned interrupt_id(const Uart16550* uart) {
  if ((uart->ier & 4) && uart->overrun) return 6;
  if ((uart->ier & 1) && uart->count >= trigger(uart)) return 4;
  if ((uart->ier & 1) && uart->count && uart->timeout) return 12;
  if ((uart->ier & 2) && uart->thre_pending) return 2;
  if ((uart->ier & 8) && (uart->msr & 15)) return 0;
  return 1;
}

static void uart_drive(Uart16550* uart) {
  /* 0 是 modem status 中断，1 才是没有中断。 */
  if (interrupt_id(uart) != 1)
    irq_raise(uart->irq);
  else
    irq_lower(uart->irq);
}

static void activity(Uart16550* uart) {
  uart->rx_activity = uart->now;
  uart->timeout = false;
}

uint64_t uart16550_deadline(const Uart16550* uart) {
  if (uart == NULL || !(uart->fcr & 1) || !uart->count || uart->timeout)
    return UINT64_MAX;
  unsigned divisor = uart->dll | ((unsigned)uart->dlm << 8);
  if (divisor == 0) divisor = 1;
  /* 半 bit 帧长：起始 + 5..8 数据 + 可选校验 + 停止位。 */
  unsigned frame = 2 * (1 + 5 + (uart->lcr & 3) + ((uart->lcr & 8) != 0));
  frame += (uart->lcr & 4) ? ((uart->lcr & 3) ? 4 : 3) : 2;
  const uint64_t numerator =
      UINT64_C(10000000) * 128 * 4 * frame * 16 * divisor;
  const uint64_t delay = (numerator + 7372800 - 1) / 7372800;
  return uart->rx_activity > UINT64_MAX - delay ? UINT64_MAX
                                                : uart->rx_activity + delay;
}

void uart16550_advance(Uart16550* uart, uint64_t logical_ticks) {
  if (uart == NULL) return;
  uart->now = logical_ticks;
  if (!uart->timeout && logical_ticks >= uart16550_deadline(uart) &&
      uart->count && (uart->fcr & 1)) {
    uart->timeout = true;
    uart_drive(uart);
  }
}

void uart16550_update(Uart16550* uart) {
  if (uart != NULL) uart_drive(uart);
}

bool uart16550_receive(Uart16550* uart, uint8_t byte) {
  if (uart == NULL) return false;
  if (uart16550_rx_capacity(uart) == 0) {
    uart->overrun = true;
    if (!(uart->fcr & 1)) {
      uart->fifo[uart->head] = byte;
      activity(uart);
    }
    uart_drive(uart);
    return false;
  }
  uart->fifo[(uart->head + uart->count) % 16] = byte;
  uart->count++;
  activity(uart);
  uart_drive(uart);
  return true;
}

static bool uart_accepts(paddr_t offset, int len) {
  return offset < 8 && len == 1;
}

static bool uart_read(void* ctx, paddr_t offset, int len, word_t* data) {
  Uart16550* uart = ctx;
  if (!uart_accepts(offset, len)) return false;
  switch (offset) {
    case 0:
      if (uart->lcr & 0x80) {
        *data = uart->dll;
        return true;
      }
      if (uart->count != 0) {
        uint8_t value = uart->fifo[uart->head];
        uart->head = (uart->head + 1) % 16;
        uart->count--;
        activity(uart);
        uart_drive(uart);
        *data = value;
        return true;
      }
      *data = 0;
      return true;
    case 1:
      *data = (uart->lcr & 0x80) ? uart->dlm : uart->ier;
      return true;
    case 2: {
      unsigned id = interrupt_id(uart);
      *data = id | ((uart->fcr & 1) ? 0xc0 : 0);
      if (id == 2) {
        uart->thre_pending = false;
        uart_drive(uart);
      }
      return true;
    }
    case 3:
      *data = uart->lcr;
      return true;
    case 4:
      *data = uart->mcr;
      return true;
    case 5:
      *data = 0x60 | (uart->count != 0) | (uart->overrun ? 2 : 0);
      uart->overrun = false;
      uart_drive(uart);
      return true;
    case 6: {
      uint8_t value = uart->msr;
      uart->msr &= 0xf0;
      uart_drive(uart);
      *data = value;
      return true;
    }
    case 7:
      *data = uart->scr;
      return true;
    default:
      return false;
  }
}

static bool uart_write(void* ctx, paddr_t offset, int len, word_t data) {
  Uart16550* uart = ctx;
  uint8_t value = (uint8_t)data;
  if (!uart_accepts(offset, len)) return false;
  switch (offset) {
    case 0:
      if (uart->lcr & 0x80) {
        uart->dll = value;
      } else {
        /* 功能模型即时发送：返回时保持寄存器和移位寄存器都空。 */
        uart->thre_pending = false;
        if (uart->mcr & 0x10)
          uart16550_receive(uart, value);
        else if (uart->transmit)
          uart->transmit(uart->transmit_opaque, value);
        uart->thre_pending = true;
      }
      break;
    case 1:
      if (uart->lcr & 0x80) {
        uart->dlm = value;
      } else {
        value &= 15;
        if ((value ^ uart->ier) & 2) uart->thre_pending = (value & 2) != 0;
        uart->ier = value;
      }
      break;
    case 2:
      if (!(value & 1)) value = 0;
      if (((value ^ uart->fcr) & 1) || (value & 2)) {
        uart->count = 0;
        uart->head = 0;
        activity(uart);
      }
      if (((value ^ uart->fcr) & 1) || (value & 4)) uart->thre_pending = true;
      uart->fcr = value & 0xc9;
      break;
    case 3:
      uart->lcr = value;
      break;
    case 4: {
      uart->mcr = value & 31;
      uint8_t status =
          (uart->mcr & 0x10)
              ? (uint8_t)(((uart->mcr & 2) << 3) | ((uart->mcr & 1) << 5) |
                          ((uart->mcr & 4) << 4) | ((uart->mcr & 8) << 4))
              : 0xb0;
      uint8_t changed = (status ^ uart->msr) & 0xf0;
      uint8_t delta = (changed >> 4) & 0xb;
      if ((changed & 0x40) && !(status & 0x40)) delta |= 4;
      uart->msr = status | (uart->msr & 15) | delta;
      break;
    }
    case 7:
      uart->scr = value;
      break;
    default:
      break;
  }
  uart_drive(uart);
  return true;
}

Uart16550* uart16550_create(IrqLine* irq,
                            void (*transmit)(void* opaque, uint8_t byte),
                            void* transmit_opaque) {
  Uart16550* uart = uart_alloc();
  if (uart == NULL) return NULL;
  uart->irq = irq;
  uart->transmit = transmit;
  uart->transmit_opaque = transmit_opaque;
  return uart;
}

void uart16550_release(Uart16550* uart) {
  if (!uart_owned(uart)) return;
  used[uart - pool] = false;
}

void uart16550_mmio(Uart16550* uart, MemObject* mmio) {
  *mmio = (MemObject){.name = "uart16550",
                      .ctx = uart,
                      .read = uart_read,
                      .write = uart_write};
}

void uart16550_reset(Uart16550* uart) {
  if (uart == NULL) return;
  uart->dll = 1;
  uart->dlm = 0;
  uart->ier = 0;
  uart->fcr = 0;
  uart->lcr = 0;
  uart->mcr = 0;
  uart->scr = 0;
  uart->msr = 0xb0;
  uart->head = 0;
  uart->count = 0;
  memset(uart->fifo, 0, sizeof(uart->fifo));
  uart->overrun = false;
  uart->thre_pending = false;
  uart->timeout = false;
  uart->now = 0;
  uart->rx_activity = 0;
  uart_drive(uart);
}
