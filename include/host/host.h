#ifndef RVISS_HOST_HOST_H
#define RVISS_HOST_HOST_H

#include <common.h>

/*
 * 宿主环境抽象：所有直接调用操作系统的地方都收敛到这里。
 * 目的：① 换平台只改这一层；② 保证客户机执行是确定性的（时间来源集中可控）。
 */

uint64_t host_time_us(void);
/* Nonblocking pipe/terminal input; 0=no data/EOF, -1=host error. */
int host_stdin_read(uint8_t *bytes, unsigned capacity);
bool host_uart_input_init(const char *record, const char *replay);
int host_uart_input(uint64_t event, uint8_t *bytes, unsigned capacity);
bool host_uart_replaying(void);
void host_uart_input_close(void);

void *host_mmap_anon(size_t size); /* 匿名映射，失败返回 NULL */
int   host_munmap(void *addr, size_t size);

/* 宿主访问客户机物理内存。失败不产生客户机异常。 */
bool host_mem_try_read(paddr_t addr, int len, word_t *data, bool *mmio);
bool host_mem_try_write(paddr_t addr, int len, word_t data, bool *mmio);
/* 没有区域应答时返回 0。调试器使用；客户机访问不能用它。 */
word_t host_mem_read(paddr_t addr, int len);
bool host_mem_write(paddr_t addr, int len, word_t data);

/* 读整个文件到堆内存（调用方 free）；*size 返回字节数 */
bool host_read_file(const char *path, void **buf, size_t *size);

#endif /* RVISS_HOST_HOST_H */
