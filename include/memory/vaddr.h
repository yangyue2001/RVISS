#ifndef RVISS_MEMORY_VADDR_H
#define RVISS_MEMORY_VADDR_H

#include <common.h>
#include <cpu/cpu.h>
#include <trap.h>

/*
 * 客户机访存请求和结果。一次访问的输入都在 MemRequest 里。
 * vaddr_access 把 addr 当虚拟地址，paddr_access 把 addr 当物理地址。
 * 成功的 load/fetch 把读到的值放在 result.data。store 和异常的 data 为 0；
 * exception == true 时 cause/tval 才有效。
 */

typedef enum {
  kMemIFetch,
  kMemRead,
  kMemWrite,
  kMemPteRead,
  kMemPteWrite,
} MemAccessType;

typedef struct {
  bool exception;
  word_t tval;
  Exception cause;
  word_t data;
} MemResult;

typedef struct {
  CpuState* hart;
  vaddr_t addr;
  word_t len;
  MemAccessType type;
  word_t data;
  Priv privilege;
} MemRequest;

typedef struct {
  vaddr_t vaddr;
  MemAccessType type;
  Priv privilege;
  bool mxr;
  bool sum;
} MmuRequest;

/* req 不能是空。读成功时数据在返回值的 data 里。 */
MemResult vaddr_access(MemRequest* req);

/*
 * 已通过地址格式检查的一次页表遍历。页表项本身走 paddr_access，不再翻译。
 * mxr/sum 由调用方传入。成功时 data 是物理地址；失败时是本次客户机访问的
 * page fault 或 access fault。
 */
MemResult riscv_mmu_translate(MmuRequest* req);

static inline MemResult mem_result_ok(void) {
  MemResult result = {0};
  return result;
}

static inline MemResult mem_result_data(word_t data) {
  MemResult result = {.data = data};
  return result;
}

static inline MemResult mem_result_fault(Exception cause, word_t tval) {
  MemResult result = {.exception = true, .tval = tval, .cause = cause};
  return result;
}

static inline bool mem_access_is_write(MemAccessType type) {
  return type == kMemWrite || type == kMemPteWrite;
}

static inline bool mem_access_is_pte(MemAccessType type) {
  return type == kMemPteRead || type == kMemPteWrite;
}

static inline Exception mem_misaligned_fault(MemAccessType type) {
  switch (type) {
    case kMemIFetch:
      return EXCP_INST_ADDR_MIS;
    case kMemRead:
    case kMemPteRead:
      return EXCP_LOAD_ADDR_MIS;
    case kMemWrite:
    case kMemPteWrite:
      return EXCP_STORE_AMO_ADDR_MIS;
  }
  panic("unknown memory access type %d", (int)type);
}

static inline Exception mem_access_fault(MemAccessType type) {
  switch (type) {
    case kMemIFetch:
      return EXCP_INST_ACCESS_FAULT;
    case kMemRead:
    case kMemPteRead:
      return EXCP_LOAD_ACCESS_FAULT;
    case kMemWrite:
    case kMemPteWrite:
      return EXCP_STORE_AMO_ACCESS_FAULT;
  }
  panic("unknown memory access type %d", (int)type);
}

static inline Exception mem_page_fault(MemAccessType type) {
  switch (type) {
    case kMemIFetch:
      return EXCP_INST_PAGE_FAULT;
    case kMemRead:
    case kMemPteRead:
      return EXCP_LOAD_PAGE_FAULT;
    case kMemWrite:
    case kMemPteWrite:
      return EXCP_STORE_AMO_PAGE_FAULT;
  }
  panic("unknown memory access type %d", (int)type);
}

static inline Priv cpu_effective_data_priv(void) {
  Priv privilege = cpu.priv;
  if (cpu.priv == PRV_M && (cpu.mstatus & MSTATUS_MPRV)) {
    privilege = (Priv)BITS(cpu.mstatus, 12, 11);
  }
  return privilege;
}

static inline MemResult vaddr_ifetch(vaddr_t addr, int len) {
  MemRequest req = {.addr = addr,
                    .len = (word_t)len,
                    .type = kMemIFetch,
                    .data = 0,
                    .privilege = cpu.priv};
  return vaddr_access(&req);
}

static inline MemResult vaddr_read(vaddr_t addr, int len) {
  MemRequest req = {.addr = addr,
                    .len = (word_t)len,
                    .type = kMemRead,
                    .data = 0,
                    .privilege = cpu_effective_data_priv()};
  return vaddr_access(&req);
}

static inline MemResult vaddr_write(vaddr_t addr, int len, word_t data) {
  MemRequest req = {.addr = addr,
                    .len = (word_t)len,
                    .type = kMemWrite,
                    .data = data,
                    .privilege = cpu_effective_data_priv()};
  return vaddr_access(&req);
}

#endif /* RVISS_MEMORY_VADDR_H */
