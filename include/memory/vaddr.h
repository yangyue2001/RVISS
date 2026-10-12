#ifndef RVISS_MEMORY_VADDR_H
#define RVISS_MEMORY_VADDR_H

#include <common.h>

/*
 * 客户机虚拟地址访问：翻译（MMU）→ 物理访问（paddr）。
 * 接口返回成功数据或明确故障，不修改 CPU/trap 状态。
 * 有效特权、翻译及故障归类集中于 target/riscv64/mmu/access.c。
 * loader/debug 直接使用物理接口，不冒充客户机访问。
 */

enum {
  MEM_TYPE_IFETCH = 0,
  MEM_TYPE_READ,
  MEM_TYPE_WRITE,
};

typedef enum { MEM_OK, MEM_FAULT, MEM_HOST_ERROR } MemStatus;

typedef struct {
  bool valid;
  int type, length;
  vaddr_t vaddr;
  paddr_t paddr;
  bool paddr_valid, mmio;
  bool write_performed; /* SC 失败虽完成权限检查，却没有实际 store */
  word_t data;
  MemStatus status;
  word_t cause;
  vaddr_t tval;
} MemAccess;

MemAccess vaddr_ifetch(vaddr_t addr, int len);
MemAccess vaddr_read(vaddr_t addr, int len);
MemAccess vaddr_write(vaddr_t addr, int len, word_t data);

MemAccess isa_mem_access(vaddr_t addr, int len, int type, word_t data);
/* target 原子路径使用：只检查/翻译，不读数据或写 RAM/MMIO。 */
MemAccess isa_mem_prepare(vaddr_t addr, int len, int type, bool ram_only);

/* ---- 以下由 target/riscv64/mmu/ 实现 ---- */
paddr_t isa_mmu_translate(vaddr_t addr, int len, int type);
void    isa_mmu_flush_tlb(void);
bool    isa_mmu_enabled(void);

#endif /* RVISS_MEMORY_VADDR_H */
