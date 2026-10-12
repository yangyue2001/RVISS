#ifndef RVISS_HOST_HOST_H
#define RVISS_HOST_HOST_H

#include <common.h>

/*
 * 宿主环境抽象：所有直接调用操作系统的地方都收敛到这里。
 * 目的：① 换平台只改这一层；② 保证客户机执行是确定性的（时间来源集中可控）。
 */

uint64_t host_time_us(void);

void *host_mmap_anon(size_t size); /* 匿名映射，失败返回 NULL */
int   host_munmap(void *addr, size_t size);

/* 读整个文件到堆内存（调用方 free）；*size 返回字节数 */
bool host_read_file(const char *path, void **buf, size_t *size);

#endif /* RVISS_HOST_HOST_H */
