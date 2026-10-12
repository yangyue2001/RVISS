#ifndef RVISS_RISCV64_CSR_H
#define RVISS_RISCV64_CSR_H

#include <common.h>
#include <cpu/csr_list.h> /* CSR 地址、视图掩码与存储清单 */

/*
 * target 侧的 CSR 访问接口。
 *
 * 地址/清单在 cpu/csr_list.h，M 侧位定义在 cpu/csr_machine.h；
 * CPU_state 的存储由清单生成。
 * 本头只声明"怎么访问"。实现见 csr/csr_access.c。
 *
 * 对照 NEMU：它的 csr.h 有 600 行 CSRR 定义 + 类型化位域 union，priv.c 里
 * 还有两个几百行的 read/write switch；这里清单只有一份，读写共用一段逻辑。
 */

/* CSR 指令（csrrw/rs/rc 及其立即数形式）的公共实现，op 见 CSR_OP_* */
enum { CSR_OP_RW = 0, CSR_OP_RS, CSR_OP_RC };
struct Decode;
void csr_execute(struct Decode* s, int op, bool immediate);

/* 读写一个 CSR。返回 false 表示非法（未实现 / 特权级不足 / 只读被写），
 * 调用方据此上报 EX_II —— 与硬件行为一致。 */
bool csr_read(word_t addr, word_t* val);
bool csr_write(word_t addr, word_t val);
/* 只读状态观测，绕过客户机权限但保留体系结构可见值/别名规则。 */
bool csr_peek(word_t addr, word_t *val);
word_t csr_status_value(word_t val);
word_t csr_epc_storage_value(word_t val);
word_t csr_epc_read_value(word_t val);

void csr_reset(void);
word_t riscv_misa_supported(void);

#endif /* RVISS_RISCV64_CSR_H */
