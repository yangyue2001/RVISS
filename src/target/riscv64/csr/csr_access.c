#include <common.h>
#include <cpu/csr_list.h>
#include <cpu/decode.h>
#include <memory/vaddr.h>
#include <util/log.h>

#include "cpu_state.h"
#include "csr.h"
#include "encoding.h"
#include "intr.h"

/*
 * CSR 访问：表驱动，表由 include/cpu/csr_list.h 的清单生成。
 *
 * 「加一个 CSR」= 只改清单（地址常量 + 一行表项），本文件一行都不用动。
 * 对照 NEMU：它的 csr_read/csr_write 各是一个几百行的 switch（文件里共 155 个
 * case），加 CSR 要同时改列表宏和两个 switch。
 *
 * 特权级与只读属性**不写进表**，而是从地址推导（privileged spec 规定）：
 *   csr[9:8]   = 访问所需的最低特权级
 *   csr[11:10] == 3 表示只读
 * 于是数据里没有"可以填错的策略字段"——这一招学自 NEMU
 * （它在 csr_normal_permit_check 里用 access_table、只读用 BITS(addr,11,10)）。
 */

typedef struct {
  word_t addr;
  word_t* field;    /* 存储所在（视图 CSR 指向别处的存储） */
  word_t view_mask; /* 该视角可见/可写的位 */
  const char* name;
} CsrEntry;

/* 宏参数名带下划线前缀：否则 `.addr = addr` 里的成员名会被参数替换掉
 * （这个坑在 decode.c 的 X-macro 上踩过一次）。 */
#define CSR_ENTRY_OWNED(_n, _a, _m) {_a, &cpu._n, _m, #_n},
#define CSR_ENTRY_VIEW(_n, _a, _f, _m) {_a, &cpu._f, _m, #_n},

static CsrEntry csr_table[] = {RVISS_CSRS_OWNED(CSR_ENTRY_OWNED)
                                   RVISS_CSRS_VIEW(CSR_ENTRY_VIEW)};

#undef CSR_ENTRY_OWNED
#undef CSR_ENTRY_VIEW

#define NR_CSRS ((int)ARRLEN(csr_table))

static CsrEntry* csr_lookup(word_t addr) {
  for (int i = 0; i < NR_CSRS; i++) {
    if (csr_table[i].addr == addr) return &csr_table[i];
  }
  return NULL; /* 未实现的 CSR 一律按非法处理（比静默返回 0 诚实） */
}

/* spec: csr[9:8] = 访问所需最低特权级（U=0/S=1/H=2/M=3） */
static inline uint8_t csr_min_priv(word_t addr) {
  return (uint8_t)((addr >> 8) & 0x3);
}

/* spec: csr[11:10] == 3（二进制 11）表示只读 */
static inline bool csr_is_readonly(word_t addr) {
  return ((addr >> 10) & 0x3) == 0x3;
}

void csr_reset(void) {
  /* 复位值：目前全是 0；有非零复位值的 CSR（如 mvendorid）将来在这里补 */
}

bool csr_read(word_t addr, word_t* val) {
  CsrEntry* e = csr_lookup(addr);
  if (e == NULL) return false;
  if (cpu.priv < csr_min_priv(addr)) {
    LogTrap("csr_read: %s 需要 %s 特权级，当前 %s", e->name,
            cpu_priv_name(csr_min_priv(addr)), cpu_priv_name(cpu.priv));
    return false;
  }
  *val = *e->field & e->view_mask;
  return true;
}

bool csr_write(word_t addr, word_t val) {
  CsrEntry* e = csr_lookup(addr);
  if (e == NULL) return false;
  if (csr_is_readonly(addr)) return false;
  if (cpu.priv < csr_min_priv(addr)) {
    LogTrap("csr_write: %s 需要 %s 特权级，当前 %s", e->name,
            cpu_priv_name(csr_min_priv(addr)), cpu_priv_name(cpu.priv));
    return false;
  }

  /* 只改 view_mask 允许的位：视图写（sstatus/sie/sip）因此不会碰 M 侧独有的位
   */
  *e->field = (*e->field & ~e->view_mask) | (val & e->view_mask);
  return true;
}

/*
 * csrrw/csrrs/csrrc(+i) 的公共实现。
 * 注意 csr 号从指令位域直接取（无符号 12 位），不用 src2.imm（它是有符号的 I
 * 型立即数）。
 */
void csr_execute(Decode* s, int op, bool immediate) {
  word_t addr = BITS(s->instr, 31, 20);
  word_t src = immediate ? ZIMM5(s->instr) : *s->src1.preg;
  word_t old = 0;

  if (!csr_read(addr, &old)) {
    riscv_exec_trap(s, EX_II, s->instr);
    return;
  }

  /* rs1 == x0（立即数形式即 zimm == 0）时不写 */
  if (op == CSR_OP_RW || s->rs1 != 0) {
    word_t newval = (op == CSR_OP_RW)   ? src
                    : (op == CSR_OP_RS) ? (old | src)
                                        : (old & ~src);
    if (!csr_write(addr, newval)) {
      riscv_exec_trap(s, EX_II, s->instr);
      return;
    }
  }

  if (s->rd != 0) {
    *s->dest.preg = old;
  }
}
