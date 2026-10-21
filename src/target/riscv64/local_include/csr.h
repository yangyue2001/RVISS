#ifndef RVISS_RISCV64_CSR_H
#define RVISS_RISCV64_CSR_H

#include <common.h>
#include <cpu/cpu.h>
/*
 * CSR 访问描述符。权限由地址位和可选 check 决定；
 * 读写函数为空时走默认存储掩码。布局见
 * docs/plans/t028_csr_descriptor.md。
 */
typedef struct CsrDesc CsrDesc;

typedef bool (*CsrCheckFn)(CpuState* hart, const CsrDesc* csr, bool write);
typedef word_t (*CsrReadFn)(CpuState* hart, const CsrDesc* csr);
typedef void (*CsrWriteFn)(CpuState* hart, const CsrDesc* csr, word_t value);
typedef void (*CsrInitFn)(CpuState* hart, const CsrDesc* csr);

/* 相对 CpuState 的字节偏移。没有对应存储时用 CSR_NO_FIELD。 */
#define CSR_NO_FIELD ((size_t)-1)

struct CsrDesc {
  const char* name;
  word_t addr;
  size_t offset;
  word_t read_mask;
  word_t write_mask;
  CsrCheckFn check;
  CsrReadFn read;
  CsrWriteFn write;
  CsrInitFn init; /* NULL：复位后保持 0，csr_init 跳过 */
};

/* 只看地址编码：范围、csr[9:8] 特权、csr[11:10]==3 的写。 */
bool csr_addr_legal(Priv privilege, word_t addr, bool write);
/* 不检查权限。未实现返回 NULL。 */
const CsrDesc* csr_lookup(word_t addr);
/* 已实现，且依赖 hart 状态的 check 通过。不含地址特权检查。 */
const CsrDesc* csr_match(CpuState* hart, word_t addr, bool write);

word_t csr_desc_read(CpuState* hart, const CsrDesc* csr);
void csr_desc_write(CpuState* hart, const CsrDesc* csr, word_t value);

/* 只看地址编码和是否实现，不读 hart 里的 CSR。 */
bool csr_accessible(Priv privilege, word_t addr, bool write);
/* 读写落在这一个 hart 上。false 表示应报非法指令，且没有写入。 */
bool csr_read(CpuState* hart, word_t addr, word_t* val);
bool csr_write(CpuState* hart, word_t addr, word_t val);

/* 宿主观察：不检查客户机权限，仍返回体系结构可见值。 */
bool csr_peek(CpuState* hart, word_t addr, word_t* val);

/* 遍历已实现 CSR。init 为空则跳过。 */
void csr_init(CpuState* hart);

/* mepc/sepc 的 bit 0 固定为 0。写入时清掉，之后直接读字段。 */
#define CSR_EPC_MASK (~1ull)

#endif /* RVISS_RISCV64_CSR_H */
