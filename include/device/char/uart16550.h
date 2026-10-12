#ifndef RVISS_DEVICE_CHAR_UART16550_H
#define RVISS_DEVICE_CHAR_UART16550_H
#include <device/device.h>

typedef struct {
  MmioRegion mmio;
  IrqLine *irq;
  void (*transmit)(void *opaque, uint8_t byte);
  void *transmit_opaque;
  uint8_t dll, dlm, ier, fcr, lcr, mcr, scr, msr;
  uint8_t fifo[16], head, count;
  bool overrun, thre_pending, timeout;
  uint64_t now, rx_activity;
} Uart16550State;
void uart16550_init(Uart16550State *s);
void uart16550_reset(Uart16550State *s);
bool uart16550_receive(Uart16550State *s, uint8_t byte);
unsigned uart16550_capacity(const Uart16550State *s);
uint64_t uart16550_next_tick(const Uart16550State *s);
#endif
