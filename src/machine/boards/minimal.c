#include "../machine.h"

/*
 * 最小板：设备地址沿用 QEMU virt 风格，便于直接跑现成的裸机程序。
 *
 * 内存图（基址/大小/复位向量）来自 Kconfig：
 *   CONFIG_MBASE / CONFIG_MSIZE / CONFIG_PC_RESET_OFFSET
 * 换板子 = 加一个 MachineConfig 文件。只调内存图 = 改 Kconfig 或
 * 运行期用 --memory 覆盖大小。加设备 = 在 node 里加一行，名字和
 * src/machine/parts 里登记的名字一致。
 */

const MachineConfig machine_minimal = {
    .name = "minimal",

    .ram_base = CONFIG_MBASE,
    .ram_size = CONFIG_MSIZE,
    .reset_pc = CONFIG_MBASE + CONFIG_PC_RESET_OFFSET,

    .nr_node = 4,
    .node =
        {
            {"clint", 0x02000000ull, 0},
            {"plic", 0x0c000000ull, 0},
            {"uart16550", 0x10000000ull, 10},
            {"test-finisher", 0x00100000ull, 0},
        },
};
