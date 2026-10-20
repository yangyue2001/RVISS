#ifndef RVISS_COMMON_H
#define RVISS_COMMON_H

/*
 * 全项目的基础聚合头。
 * 只放"任何模块都可以依赖、且不依赖任何模块"的东西：
 *   类型 (types.h) / 位操作 (bits.h) / 宏工具 (macro.h) / 断言 (assert.h)
 *
 * 规则：这个头（及其子头）不允许 include 项目内其它任何头，保证依赖有向无环。
 */

#include <generated/autoconf.h> /* Kconfig 生成的 #define CONFIG_xxx */

/* C 标准库基础（NEMU 同款做法：这几个头在本项目里被视为"语言基础"） */
#include <common/assert.h>
#include <common/bits.h>
#include <common/macro.h>
#include <common/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#endif /* RVISS_COMMON_H */
