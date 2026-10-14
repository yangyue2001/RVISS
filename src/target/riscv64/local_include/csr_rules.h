#ifndef RVISS_RISCV64_CSR_RULES_H
#define RVISS_RISCV64_CSR_RULES_H

#include <common.h>

/*
 * CSR 字段属性。地址目录回答“这个 CSR 在哪里”，字段规则回答“写入后
 * 哪些编码真正有意义”。规则只处理字段本身；特权、只读 CSR 编码、别名
 * 和跨 CSR 副作用仍由 csr_access.c 显式处理。
 */
typedef enum {
  CSR_FIELD_RW,
  CSR_FIELD_WPRI,
  CSR_FIELD_WARL,
  CSR_FIELD_WLRL,
  CSR_FIELD_RO,
} CsrFieldKind;

typedef bool (*CsrFieldLegalFn)(word_t value);
typedef word_t (*CsrFieldNormalizeFn)(word_t requested, word_t old_value);

typedef struct {
  word_t mask;
  CsrFieldKind kind;
  CsrFieldLegalFn legal;
  CsrFieldNormalizeFn normalize;
} CsrFieldRule;

/* 未列出的位按 WPRI 处理：写入保留旧值，读取忽略。 */
word_t csr_rules_read(const CsrFieldRule* rules, size_t count, word_t value);
word_t csr_rules_write(const CsrFieldRule* rules, size_t count,
                       word_t old_value, word_t requested);

#endif /* RVISS_RISCV64_CSR_RULES_H */
