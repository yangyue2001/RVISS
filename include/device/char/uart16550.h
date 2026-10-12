#ifndef RVISS_DEVICE_CHAR_UART16550_H
#define RVISS_DEVICE_CHAR_UART16550_H

#include <common.h>
#include <device/device.h>

/*
 * 16550 UART（最小实现）：
 *   offset 0  写 = THR（输出到宿主 stdout），读 = RBR（暂无输入）
 *   offset 5  读 = LSR（固定"发送就绪 | 无数据"）
 * 固件轮询 LSR 再写 THR 就能打印。
 * TODO(M4)：中断模式、FIFO、从文件/管道输入。
 */

typedef struct {
  MmioRegion mmio;
  /* 目前无额外状态；将来加 FIFO/中断时放这里 */
} Uart16550State;

void uart16550_init(Uart16550State *s);
void uart16550_reset(Uart16550State *s);

#endif /* RVISS_DEVICE_CHAR_UART16550_H */
