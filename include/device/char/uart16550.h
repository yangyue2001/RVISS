#ifndef RVISS_DEVICE_CHAR_UART16550_H
#define RVISS_DEVICE_CHAR_UART16550_H

#include <device/device.h>

/*
 * 16550。可以有多路。uart16550_init 分配一路并复位。
 * 板卡随后填写 irq 和 transmit。设备发中断时只 raise irq，不知道对端。
 * 基址不在这里。receive 是宿主把字节送进来，不是 MMIO。
 */

#define UART16550_MMIO_SIZE UINT64_C(0x100)

typedef struct Uart16550 {
  IrqLine* irq;
  void (*transmit)(void* opaque, uint8_t byte);
  void* transmit_opaque;
  uint8_t dll, dlm, ier, fcr, lcr, mcr, scr, msr;
  uint8_t fifo[16], head, count;
  bool overrun, thre_pending, timeout;
  uint64_t now, rx_activity;
} Uart16550;

MemObject uart16550_init(void);

bool uart16550_receive(Uart16550* uart, uint8_t byte);
unsigned uart16550_rx_capacity(const Uart16550* uart);
bool uart16550_loopback(const Uart16550* uart);

#endif /* RVISS_DEVICE_CHAR_UART16550_H */
