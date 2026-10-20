#include <common.h>

/*
 * 客户机取指、load、store 不再从这里进入。
 * 对齐、两段拆分、翻译判断和物理访问在 src/memory/vaddr.c。
 * 页表遍历在 mmu.c，PMP 在 pmp.c。
 */
