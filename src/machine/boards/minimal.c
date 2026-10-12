#include "../machine.h"

/*
 * 最小板：设备地址沿用 QEMU virt 风格，便于直接跑现成的裸机程序。
 *
 * 内存图（基址/大小/复位向量）来自 Kconfig：
 *   CONFIG_MBASE / CONFIG_MSIZE / CONFIG_PC_RESET_OFFSET
 * 换板子 = 加一个 MachineConfig 文件；只调内存图 = 改 Kconfig 或
 * 运行期用 --memory 覆盖大小。
 */

const MachineConfig machine_minimal = {
    .name = "minimal",

    .ram_base = CONFIG_MBASE,
    .ram_size = CONFIG_MSIZE,
    .reset_pc = CONFIG_MBASE + CONFIG_PC_RESET_OFFSET,

    .clint_base = 0x02000000ull,
    .uart_base  = 0x10000000ull,
    .plic_base  = 0x0c000000ull,
    .test_base  = 0x00100000ull,
};
