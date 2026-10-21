#ifndef RVISS_RISCV64_CSR_H
#define RVISS_RISCV64_CSR_H

#include <common.h>

/*
 * CSR 访问描述符。权限由地址位和可选 check 决定；
 * 读写函数为空时走默认存储掩码。布局见
 * docs/plans/t028_csr_descriptor.md。
 */
typedef struct CsrDesc CsrDesc;

typedef bool (*CsrCheckFn)(const CsrDesc* csr, bool write);
typedef word_t (*CsrReadFn)(const CsrDesc* csr);
typedef void (*CsrWriteFn)(const CsrDesc* csr, word_t value);

struct CsrDesc {
  const char* name;
  word_t addr;
  word_t* field;
  word_t read_mask;
  word_t write_mask;
  CsrCheckFn check;
  CsrReadFn read;
  CsrWriteFn write;
};

/* 只看地址编码：范围、csr[9:8] 特权、csr[11:10]==3 的写。 */
bool csr_addr_legal(word_t addr, bool write);
/* 不检查权限。未实现返回 NULL。 */
const CsrDesc* csr_lookup(word_t addr);
/* 地址合法、已实现，且 desc->check 通过。失败返回 NULL。 */
const CsrDesc* csr_match(word_t addr, bool write);

word_t csr_desc_read(const CsrDesc* csr);
void csr_desc_write(const CsrDesc* csr, word_t value);

/* 客户机访问。false 表示应报非法指令，且没有写入。 */
bool csr_read(word_t addr, word_t* val);
bool csr_write(word_t addr, word_t val);
/* 宿主观察：不检查客户机权限，仍返回体系结构可见值。 */
bool csr_peek(word_t addr, word_t* val);

/* trap 与 CSR 写共用。保留未实现位清零，并派生 SD。 */
word_t csr_status_value(word_t val);
word_t csr_epc_storage_value(word_t val);
word_t csr_epc_read_value(word_t val);
word_t riscv_misa_supported(void);

#endif /* RVISS_RISCV64_CSR_H */
