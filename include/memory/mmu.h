#ifndef RVISS_MEMORY_MMU_H
#define RVISS_MEMORY_MMU_H
#include <common.h>

/* Sv39/Sv48/Sv57 共用：4 KiB 页、每级 9 位索引、PTE 8 字节、PPN 从 bit 10 开始。 */
#define RISCV_PAGE_SHIFT 12
#define RISCV_PAGE_SIZE (UINT64_C(1) << RISCV_PAGE_SHIFT)
#define RISCV_VPN_BITS 9
#define RISCV_PTE_BYTES 8
#define RISCV_PTE_PPN_SHIFT 10
#define RISCV_PPN_BITS 44
#define RISCV_PA_BITS 56
#define RISCV_SV39_LEVELS 3
#define RISCV_SV39_VA_BITS 39
#define RISCV_SV48_LEVELS 4
#define RISCV_SV48_VA_BITS 48
#define RISCV_SV57_LEVELS 5
#define RISCV_SV57_VA_BITS 57

#define PTE_V (UINT64_C(1) << 0)
#define PTE_R (UINT64_C(1) << 1)
#define PTE_W (UINT64_C(1) << 2)
#define PTE_X (UINT64_C(1) << 3)
#define PTE_U (UINT64_C(1) << 4)
#define PTE_G (UINT64_C(1) << 5)
#define PTE_A (UINT64_C(1) << 6)
#define PTE_D (UINT64_C(1) << 7)
#define PTE_RSW (UINT64_C(3) << 8)
#define PTE_PPN (((UINT64_C(1) << RISCV_PPN_BITS) - 1) << RISCV_PTE_PPN_SHIFT)
#define PTE_RESERVED (((UINT64_C(1) << 7) - 1) << 54)
#define PTE_PBMT (UINT64_C(3) << 61)
#define PTE_N (UINT64_C(1) << 63)

#endif /* RVISS_MEMORY_MMU_H */
