#ifndef RVISS_DEVICE_CHAR_UART16550_H
#define RVISS_DEVICE_CHAR_UART16550_H

#include <device/device.h>

/*
 * 16550 的 CPU 访问只有 MMIO。实例在 uart16550.c 的固定池里，
 * 可以同时存在多个。宿主收发是板级边：TX 回调把字节送出，
 * receive 把宿主字节送入。状态结构体不对外，也不保存映射。
 */

typedef struct Uart16550 Uart16550;

Uart16550* uart16550_create(IrqLine* irq,
                            void (*transmit)(void* opaque, uint8_t byte),
                            void* transmit_opaque);
void uart16550_release(Uart16550* uart);
void uart16550_reset(Uart16550* uart);
#define UART16550_MMIO_SIZE UINT64_C(0x100)

void uart16550_mmio(Uart16550* uart, MemObject* mmio);
void uart16550_advance(Uart16550* uart, uint64_t logical_ticks);
void uart16550_update(Uart16550* uart);
uint64_t uart16550_deadline(const Uart16550* uart);
bool uart16550_receive(Uart16550* uart, uint8_t byte);
unsigned uart16550_rx_capacity(const Uart16550* uart);
bool uart16550_loopback(const Uart16550* uart);

#endif /* RVISS_DEVICE_CHAR_UART16550_H */
