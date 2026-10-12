#ifndef RVISS_MEMORY_IMAGE_H
#define RVISS_MEMORY_IMAGE_H

#include <common.h>

/*
 * 镜像加载：从文件路径探测格式（裸二进制 / ELF64），装入物理内存。
 * 返回实际写入的最大地址（0 表示失败）。
 */
paddr_t image_load(const char *path, paddr_t load_addr);

/* 单独暴露给单元测试/上层使用 */
paddr_t image_load_raw(const char *path, paddr_t load_addr);
paddr_t image_load_elf(const char *path, paddr_t load_addr);

#endif /* RVISS_MEMORY_IMAGE_H */
