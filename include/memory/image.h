#ifndef RVISS_MEMORY_IMAGE_H
#define RVISS_MEMORY_IMAGE_H

#include <common.h>

/*
 * 镜像加载：从文件路径探测格式（裸二进制 / ELF64），装入物理内存。
 * 返回实际写入区间的末尾地址（不含该地址）；0 表示失败。
 * 拒绝坏 ELF 或越出 RAM 的镜像，失败不改 RAM；复位 PC 由板级决定。
 */
paddr_t image_load(const char *path, paddr_t load_addr);

/* Explicit format entry points retain the historical caller interface. */
paddr_t image_load_raw(const char *path, paddr_t load_addr);
paddr_t image_load_elf(const char *path, paddr_t load_addr);

#endif /* RVISS_MEMORY_IMAGE_H */
