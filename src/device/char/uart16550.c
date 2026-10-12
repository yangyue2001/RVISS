#include <common.h>
#include <device/char/uart16550.h>
#include <util/log.h>

/*
 * 16550 UART（最小实现）。
 * 无状态设备的样板：所有状态放实例结构体（现在为空），回调经 opaque 取实例。
 * TODO(M4)：中断模式、FIFO、从文件/管道输入。
 */

#define UART_THR 0
#define UART_LSR 5
#define UART_SIZE 0x100

#define UART_LSR_DR   (1u << 0) /* data ready */
#define UART_LSR_THRE (1u << 5) /* transmit holding empty */

static word_t uart_read(void *opaque, paddr_t off, int len) {
  (void)opaque;
  (void)len;
  if (off == UART_LSR) return UART_LSR_THRE; /* 无输入：DR=0, THRE=1 */
  return 0;
}

static void uart_write(void *opaque, paddr_t off, int len, word_t data) {
  (void)opaque;
  (void)len;
  if (off == UART_THR) {
    putchar((int)(data & 0xffu));
    fflush(stdout);
  }
}

void uart16550_init(Uart16550State *s) {
  memset(s, 0, sizeof(*s));
  s->mmio = (MmioRegion){
      .name = "uart16550",
      .size = UART_SIZE,
      .read = uart_read,
      .write = uart_write,
      .opaque = s,
  };
}

void uart16550_reset(Uart16550State *s) {
  (void)s; /* 目前无状态；将来加 FIFO / 中断使能时在这里复位 */
}
