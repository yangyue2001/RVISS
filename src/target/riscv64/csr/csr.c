#include "csr.h"

#include <cpu/csr_list.h>
#include <cpu/interrupt.h>
#ifdef CONFIG_HAS_CLINT
#include <device/timer/clint.h>
#endif

#include "pmp.h"
#include "trap.h"

/*
 * 一条 CSR 在表里占一行。read 或 write 为空时走默认路径：
 * 读出 offset 处的值再与 read_mask 相与；写入只更新 write_mask 里的位。
 * 没有对应存储时读出 0。掩码表达不了的 WARL、别名和副作用才写函数。
 */

/* 只有选出部分位时才命名：XXX_READ_MASK / XXX_WRITE_MASK。
 * 全位直接写 ~0ull，不可写或恒为 0 直接写 0。 */
#define SSTATUS_WRITE_MASK                                               \
  (MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP | MSTATUS_FS | MSTATUS_SUM | \
   MSTATUS_MXR)
#define SSTATUS_READ_MASK (SSTATUS_WRITE_MASK | MSTATUS_UXL | MSTATUS_SD)
#define MSTATUS_WRITE_MASK                                         \
  (SSTATUS_WRITE_MASK | MSTATUS_MIE | MSTATUS_MPIE | MSTATUS_MPP | \
   MSTATUS_MPRV | MSTATUS_TVM | MSTATUS_TW | MSTATUS_TSR)
#define MSTATUS_READ_MASK \
  (MSTATUS_WRITE_MASK | MSTATUS_UXL | MSTATUS_SXL | MSTATUS_SD)

#define MEDELEG_READ_MASK                                                      \
  ((1ull << EXCP_INST_ADDR_MIS) | (1ull << EXCP_INST_ACCESS_FAULT) |           \
   (1ull << EXCP_ILLEGAL_INST) | (1ull << EXCP_BREAKPOINT) |                   \
   (1ull << EXCP_LOAD_ADDR_MIS) | (1ull << EXCP_LOAD_ACCESS_FAULT) |           \
   (1ull << EXCP_STORE_AMO_ADDR_MIS) | (1ull << EXCP_STORE_AMO_ACCESS_FAULT) | \
   (1ull << EXCP_U_ECALL) | (1ull << EXCP_S_ECALL) |                           \
   (1ull << EXCP_INST_PAGE_FAULT) | (1ull << EXCP_LOAD_PAGE_FAULT) |           \
   (1ull << EXCP_STORE_AMO_PAGE_FAULT))
#define MEDELEG_WRITE_MASK MEDELEG_READ_MASK
#define MIDELEG_READ_MASK (MIP_SSIP | MIP_STIP | MIP_SEIP)
#define MIDELEG_WRITE_MASK MIDELEG_READ_MASK
#define MIE_READ_MASK \
  (MIP_SSIP | MIP_MSIP | MIP_STIP | MIP_MTIP | MIP_SEIP | MIP_MEIP)
#define MIE_WRITE_MASK MIE_READ_MASK
#define MIP_READ_MASK MIE_READ_MASK
#define MIP_WRITE_MASK (MIP_SSIP | MIP_STIP | MIP_SEIP)

#define SIE_WRITE_MASK MIP_SSIP
#define SIP_WRITE_MASK SIE_WRITE_MASK
#define MCOUNTEREN_READ_MASK (MCOUNTEREN_CY | MCOUNTEREN_TM | MCOUNTEREN_IR)
#define MCOUNTEREN_WRITE_MASK MCOUNTEREN_READ_MASK
#define MCOUNTINHIBIT_READ_MASK (MCOUNTINHIBIT_CY | MCOUNTINHIBIT_IR)
#define MCOUNTINHIBIT_WRITE_MASK MCOUNTINHIBIT_READ_MASK

#define FFLAGS_READ_MASK FCSR_FFLAGS
#define FFLAGS_WRITE_MASK FCSR_FFLAGS
#define FRM_READ_MASK (FCSR_FRM >> FCSR_FRM_SHIFT)
#define FRM_WRITE_MASK FRM_READ_MASK
#define FCSR_READ_MASK (FCSR_FFLAGS | FCSR_FRM)
#define FCSR_WRITE_MASK FCSR_READ_MASK

#define MISA_INIT                                                              \
  MISA_MXL_64 | MISA_I | MISA_M | MISA_A | MISA_F | MISA_D | MISA_C | MISA_S | \
      MISA_U

static word_t* csr_slot(CpuState* hart, const CsrDesc* csr) {
  Assert(csr->offset != CSR_NO_FIELD, "CSR %s has no storage", csr->name);
  return (word_t*)((unsigned char*)hart + csr->offset);
}

static inline word_t csr_default_read(CpuState* hart, const CsrDesc* csr) {
  if (csr->offset == CSR_NO_FIELD) return 0;
  return *csr_slot(hart, csr) & csr->read_mask;
}

static inline void csr_default_write(CpuState* hart, const CsrDesc* csr,
                                     word_t value) {
  if (csr->offset == CSR_NO_FIELD) return;
  word_t* slot = csr_slot(hart, csr);
  *slot = (*slot & ~csr->write_mask) | (value & csr->write_mask);
}

/* 读出的 SD 不写回。FS、VS、XS 任一为 Dirty 则为 1。 */
static void mstatus_update_sd(word_t* status) {
  const bool dirty = (*status & MSTATUS_FS) == MSTATUS_FS_DIRTY ||
                     (*status & MSTATUS_VS) == MSTATUS_VS_DIRTY ||
                     (*status & MSTATUS_XS) == MSTATUS_XS_DIRTY;
  if (dirty)
    *status |= MSTATUS_SD;
  else
    *status &= ~MSTATUS_SD;
}

static word_t csr_read_mstatus(CpuState* hart, const CsrDesc* csr) {
  word_t value = hart->mstatus;
  mstatus_update_sd(&value);
  return value & csr->read_mask;
}

/* MPP 的非法编码保留旧值。sstatus 用更窄的写掩码。不改 SD。 */
static void csr_write_mstatus(CpuState* hart, const CsrDesc* csr,
                              word_t value) {
  word_t merged =
      (hart->mstatus & ~csr->write_mask) | (value & csr->write_mask);

  if ((merged & MSTATUS_MPP) == (2ull << 11)) {
    merged = (merged & ~MSTATUS_MPP) | (hart->mstatus & MSTATUS_MPP);
  }

  hart->mstatus = merged;
}

#ifdef CONFIG_RV_F
static bool csr_check_fp(CpuState* hart, const CsrDesc* csr, bool write) {
  (void)csr;
  (void)write;
  return (hart->mstatus & MSTATUS_FS) != 0;
}

static void csr_fp_dirty(CpuState* hart) {
  hart->mstatus = (hart->mstatus & ~MSTATUS_FS) | MSTATUS_FS_DIRTY;
}

static void csr_write_fp(CpuState* hart, const CsrDesc* csr, word_t value) {
  csr_default_write(hart, csr, value);
  csr_fp_dirty(hart);
}

/* frm 在 fcsr[7:5]，读出来要落到 [2:0]，掩码无法单独完成移位。 */
static word_t csr_read_frm(CpuState* hart, const CsrDesc* csr) {
  (void)csr;
  return (hart->fcsr >> FCSR_FRM_SHIFT) & FRM_READ_MASK;
}

static void csr_write_frm(CpuState* hart, const CsrDesc* csr, word_t value) {
  (void)csr;
  hart->fcsr = (hart->fcsr & FFLAGS_WRITE_MASK) |
               ((value & FRM_WRITE_MASK) << FCSR_FRM_SHIFT);
  csr_fp_dirty(hart);
}
#endif

static void csr_init_misa(CpuState* hart, const CsrDesc* csr) {
  (void)csr;
  hart->misa = MISA_INIT;
}

static void csr_init_mstatus(CpuState* hart, const CsrDesc* csr) {
  (void)csr;
  /* MPP=U，FS/VS/XS=Off，因此 SD=0。UXL/SXL 固定为 64。 */
  hart->mstatus = MSTATUS_UXL_64 | MSTATUS_SXL_64;
}

static void csr_write_tvec(CpuState* hart, const CsrDesc* csr, word_t value) {
  word_t mode = value & 3;
  if (mode > 1) mode = *csr_slot(hart, csr) & 3;
  *csr_slot(hart, csr) = (value & ~3ull) | mode;
}

/* 可见位还要看当前 mideleg，静态掩码只能给出上限。 */
static word_t csr_read_sie(CpuState* hart, const CsrDesc* csr) {
  (void)csr;
  return hart->mie & hart->mideleg & SIE_MASK;
}

// 只有委托了才能看见
static word_t csr_read_sip(CpuState* hart, const CsrDesc* csr) {
  (void)csr;
  return hart->mip & hart->mideleg & SIE_MASK;
}

static void csr_write_mip(CpuState* hart, const CsrDesc* csr, word_t value) {
  (void)csr;
  cpu_write_mip(hart, value);
}

static void csr_write_sip(CpuState* hart, const CsrDesc* csr, word_t value) {
  (void)csr;
  cpu_write_sip(hart, value);
}

static bool csr_check_satp(CpuState* hart, const CsrDesc* csr, bool write) {
  (void)csr;
  (void)write;
  return !(hart->priv == PRV_S && (hart->mstatus & MSTATUS_TVM) != 0);
}

static void csr_write_satp(CpuState* hart, const CsrDesc* csr, word_t value) {
  (void)csr;
  word_t mode = value & SATP64_MODE;
  if (mode != SATP64_MODE_BARE && mode != SATP64_MODE_SV39) {
    mode = hart->satp & SATP64_MODE;
  }
  hart->satp = mode | (value & (SATP64_ASID | SATP64_PPN));
}

static bool csr_check_counter(CpuState* hart, const CsrDesc* csr, bool write) {
  (void)write;
  if (hart->priv >= PRV_M) return true;
  const unsigned index = (unsigned)(csr->addr - CSR_CYCLE);
  const word_t bit = 1ull << index;
  if ((hart->mcounteren & bit) == 0) return false;
  if (hart->priv == PRV_U && (hart->scounteren & bit) == 0) return false;
  return true;
}

static word_t csr_read_time(CpuState* hart, const CsrDesc* csr) {
  (void)hart;
  (void)csr;
#ifdef CONFIG_HAS_CLINT
  return clint_mtime_now();
#else
  return 0;
#endif
}

enum { BITS_PER_BYTE = 8, PMP_CFG_BYTES = 8 };

/* RV64 的 pmpcfg 地址间隔是 2，每个寄存器装 8 个 entry。 */
static unsigned pmp_cfg_index(word_t addr) {
  return (unsigned)(addr - CSR_PMPCFG0) * (PMP_CFG_BYTES / 2);
}

static word_t csr_read_pmp(CpuState* hart, const CsrDesc* csr) {
  const word_t addr = csr->addr;
  if (addr >= CSR_PMPADDR0) {
    unsigned index = (unsigned)(addr - CSR_PMPADDR0);
    return index < hart->pmp_count ? hart->pmpaddr[index] : 0;
  }
  if ((addr & 1) != 0 || addr < CSR_PMPCFG0) return 0;
  unsigned first = pmp_cfg_index(addr);
  word_t value = 0;
  for (unsigned i = 0; i < PMP_CFG_BYTES && first + i < hart->pmp_count; i++) {
    value |= (word_t)hart->pmpcfg[first + i] << (BITS_PER_BYTE * i);
  }
  return value;
}

static void csr_write_pmp(CpuState* hart, const CsrDesc* csr, word_t value) {
  const word_t addr = csr->addr;
  if (addr >= CSR_PMPADDR0) {
    unsigned index = (unsigned)(addr - CSR_PMPADDR0);
    if (index >= hart->pmp_count || (hart->pmpcfg[index] & PMP_LOCK) != 0) {
      return;
    }
    unsigned tor = (unsigned)PMP_AMATCH_TOR << PMP_AMATCH_SHIFT;
    if (index + 1 < hart->pmp_count &&
        (hart->pmpcfg[index + 1] & (PMP_LOCK | PMP_AMATCH)) ==
            (PMP_LOCK | tor)) {
      return;
    }
    hart->pmpaddr[index] = value & PMPADDR_ADDR_RV64;
    return;
  }
  if ((addr & 1) != 0 || addr < CSR_PMPCFG0) return;
  unsigned first = pmp_cfg_index(addr);
  word_t keep = PMP_READ | PMP_WRITE | PMP_EXEC | PMP_AMATCH | PMP_LOCK;
  for (unsigned i = 0; i < PMP_CFG_BYTES && first + i < hart->pmp_count; i++) {
    if ((hart->pmpcfg[first + i] & PMP_LOCK) != 0) continue;
    uint8_t cfg = (uint8_t)((value >> (BITS_PER_BYTE * i)) & keep);
    if ((cfg & PMP_READ) == 0) cfg = (uint8_t)(cfg & ~PMP_WRITE);
    hart->pmpcfg[first + i] = cfg;
  }
}

// clang-format off
/* 新 CSR 只在 RVISS_CSR 加一行：名字、存储、读掩码、写掩码、check、read、write、init。
 * 地址由 CSR_<名字> 得到。read/write 为空则按掩码访问存储。init 为空则复位保持 0。 */
#ifdef CONFIG_RV_F
#define RVISS_CSR_FP(X) \
  X(fflags, offsetof(CpuState, fcsr), FFLAGS_READ_MASK, FFLAGS_WRITE_MASK, csr_check_fp, NULL, csr_write_fp, NULL) \
  X(frm, offsetof(CpuState, fcsr), FRM_READ_MASK, FRM_WRITE_MASK, csr_check_fp, csr_read_frm, csr_write_frm, NULL) \
  X(fcsr, offsetof(CpuState, fcsr), FCSR_READ_MASK, FCSR_WRITE_MASK, csr_check_fp, NULL, csr_write_fp, NULL)
#else
#define RVISS_CSR_FP(X)
#endif
#define PMP_CFG(X, n) \
  X(pmpcfg##n, CSR_NO_FIELD, ~0ull, ~0ull, NULL, csr_read_pmp, csr_write_pmp, NULL)
#define PMP_ADDR(X, n) \
  X(pmpaddr##n, CSR_NO_FIELD, ~0ull, ~0ull, NULL, csr_read_pmp, csr_write_pmp, NULL)


#define RVISS_CSR(X) \
  X(mstatus, offsetof(CpuState, mstatus), MSTATUS_READ_MASK, MSTATUS_WRITE_MASK, NULL, csr_read_mstatus, csr_write_mstatus, csr_init_mstatus) \
  X(sstatus, offsetof(CpuState, mstatus), SSTATUS_READ_MASK, SSTATUS_WRITE_MASK, NULL, csr_read_mstatus, csr_write_mstatus, NULL) \
  X(misa, offsetof(CpuState, misa), ~0ull, 0, NULL, NULL, NULL, csr_init_misa) \
  X(medeleg, offsetof(CpuState, medeleg), MEDELEG_READ_MASK, MEDELEG_WRITE_MASK, NULL, NULL, NULL, NULL) \
  X(mideleg, offsetof(CpuState, mideleg), MIDELEG_READ_MASK, MIDELEG_WRITE_MASK, NULL, NULL, NULL, NULL) \
  X(mie, offsetof(CpuState, mie), MIE_READ_MASK, MIE_WRITE_MASK, NULL, NULL, NULL, NULL) \
  X(mip, offsetof(CpuState, mip), MIP_READ_MASK, MIP_WRITE_MASK, NULL, NULL, csr_write_mip, NULL) \
  X(mtvec, offsetof(CpuState, mtvec), ~0ull, ~0ull, NULL, NULL, csr_write_tvec, NULL) \
  X(mscratch, offsetof(CpuState, mscratch), ~0ull, ~0ull, NULL, NULL, NULL, NULL) \
  X(mepc, offsetof(CpuState, mepc), CSR_EPC_MASK, CSR_EPC_MASK, NULL, NULL, NULL, NULL) \
  X(mcause, offsetof(CpuState, mcause), ~0ull, ~0ull, NULL, NULL, NULL, NULL) \
  X(mtval, offsetof(CpuState, mtval), ~0ull, ~0ull, NULL, NULL, NULL, NULL) \
  X(mcounteren, offsetof(CpuState, mcounteren), MCOUNTEREN_READ_MASK, MCOUNTEREN_WRITE_MASK, NULL, NULL, NULL, NULL) \
  X(mcountinhibit, offsetof(CpuState, mcountinhibit), MCOUNTINHIBIT_READ_MASK, MCOUNTINHIBIT_WRITE_MASK, NULL, NULL, NULL, NULL) \
  X(mcycle, offsetof(CpuState, mcycle), ~0ull, ~0ull, NULL, NULL, NULL, NULL) \
  X(minstret, offsetof(CpuState, minstret), ~0ull, ~0ull, NULL, NULL, NULL, NULL) \
  X(stvec, offsetof(CpuState, stvec), ~0ull, ~0ull, NULL, NULL, csr_write_tvec, NULL) \
  X(scounteren, offsetof(CpuState, scounteren), MCOUNTEREN_READ_MASK, MCOUNTEREN_WRITE_MASK, NULL, NULL, NULL, NULL) \
  X(sscratch, offsetof(CpuState, sscratch), ~0ull, ~0ull, NULL, NULL, NULL, NULL) \
  X(sepc, offsetof(CpuState, sepc), CSR_EPC_MASK, CSR_EPC_MASK, NULL, NULL, NULL, NULL) \
  X(scause, offsetof(CpuState, scause), ~0ull, ~0ull, NULL, NULL, NULL, NULL) \
  X(stval, offsetof(CpuState, stval), ~0ull, ~0ull, NULL, NULL, NULL, NULL) \
  X(satp, offsetof(CpuState, satp), ~0ull, ~0ull, csr_check_satp, NULL, csr_write_satp, NULL) \
  X(sie, offsetof(CpuState, mie), SIE_MASK, SIE_WRITE_MASK, NULL, csr_read_sie, NULL, NULL) \
  X(sip, offsetof(CpuState, mip), SIE_MASK, SIP_WRITE_MASK, NULL, csr_read_sip, csr_write_sip, NULL) \
  X(cycle, offsetof(CpuState, mcycle), ~0ull, 0, csr_check_counter, NULL, NULL, NULL) \
  X(time, CSR_NO_FIELD, ~0ull, 0, csr_check_counter, csr_read_time, NULL, NULL) \
  X(instret, offsetof(CpuState, minstret), ~0ull, 0, csr_check_counter, NULL, NULL, NULL) \
  X(mvendorid, CSR_NO_FIELD, 0, 0, NULL, NULL, NULL, NULL) \
  X(marchid, CSR_NO_FIELD, 0, 0, NULL, NULL, NULL, NULL) \
  X(mimpid, CSR_NO_FIELD, 0, 0, NULL, NULL, NULL, NULL) \
  X(mhartid, offsetof(CpuState, mhartid), ~0ull, 0, NULL, NULL, NULL, NULL) \
  X(mconfigptr, CSR_NO_FIELD, 0, 0, NULL, NULL, NULL, NULL) \
  RVISS_CSR_FP(X) \
  PMP_CFG(X, 0) PMP_CFG(X, 2) PMP_CFG(X, 4) PMP_CFG(X, 6) \
  PMP_CFG(X, 8) PMP_CFG(X, 10) PMP_CFG(X, 12) PMP_CFG(X, 14) \
  PMP_ADDR(X, 0) PMP_ADDR(X, 1) PMP_ADDR(X, 2) PMP_ADDR(X, 3) PMP_ADDR(X, 4) PMP_ADDR(X, 5) PMP_ADDR(X, 6) PMP_ADDR(X, 7) \
  PMP_ADDR(X, 8) PMP_ADDR(X, 9) PMP_ADDR(X, 10) PMP_ADDR(X, 11) PMP_ADDR(X, 12) PMP_ADDR(X, 13) PMP_ADDR(X, 14) PMP_ADDR(X, 15) \
  PMP_ADDR(X, 16) PMP_ADDR(X, 17) PMP_ADDR(X, 18) PMP_ADDR(X, 19) PMP_ADDR(X, 20) PMP_ADDR(X, 21) PMP_ADDR(X, 22) PMP_ADDR(X, 23) \
  PMP_ADDR(X, 24) PMP_ADDR(X, 25) PMP_ADDR(X, 26) PMP_ADDR(X, 27) PMP_ADDR(X, 28) PMP_ADDR(X, 29) PMP_ADDR(X, 30) PMP_ADDR(X, 31) \
  PMP_ADDR(X, 32) PMP_ADDR(X, 33) PMP_ADDR(X, 34) PMP_ADDR(X, 35) PMP_ADDR(X, 36) PMP_ADDR(X, 37) PMP_ADDR(X, 38) PMP_ADDR(X, 39) \
  PMP_ADDR(X, 40) PMP_ADDR(X, 41) PMP_ADDR(X, 42) PMP_ADDR(X, 43) PMP_ADDR(X, 44) PMP_ADDR(X, 45) PMP_ADDR(X, 46) PMP_ADDR(X, 47) \
  PMP_ADDR(X, 48) PMP_ADDR(X, 49) PMP_ADDR(X, 50) PMP_ADDR(X, 51) PMP_ADDR(X, 52) PMP_ADDR(X, 53) PMP_ADDR(X, 54) PMP_ADDR(X, 55) \
  PMP_ADDR(X, 56) PMP_ADDR(X, 57) PMP_ADDR(X, 58) PMP_ADDR(X, 59) PMP_ADDR(X, 60) PMP_ADDR(X, 61) PMP_ADDR(X, 62) PMP_ADDR(X, 63)

#define CSR_DECL(name, field, rmask, wmask, check, read, write, init) \
  static const CsrDesc csr_desc_##name = \
      {#name, CSR_##name, field, rmask, wmask, check, read, write, init};
RVISS_CSR(CSR_DECL)
#undef CSR_DECL

#define CSR_SLOT(name, field, rmask, wmask, check, read, write, init) \
  [CSR_##name] = &csr_desc_##name,
static const CsrDesc* const csr_map[4096] = {RVISS_CSR(CSR_SLOT)};
#undef CSR_SLOT
#undef PMP_ADDR
#undef PMP_CFG
#undef RVISS_CSR_FP
// clang-format on

void csr_init(CpuState* hart) {
  Assert(hart != NULL, "csr_init 缺少 hart");
  for (word_t addr = 0; addr < 4096; addr++) {
    const CsrDesc* csr = csr_map[addr];
    if (csr != NULL && csr->init != NULL) csr->init(hart, csr);
  }
}

bool csr_addr_legal(Priv privilege, word_t addr, bool write) {
  if (addr > 0xfff) return false;
  if (privilege < ((addr >> 8) & 3)) return false;
  if (write && ((addr >> 10) & 3) == 3) return false;
  return true;
}

const CsrDesc* csr_lookup(word_t addr) {
  if (addr > 0xfff) return NULL;
  return csr_map[addr];
}

const CsrDesc* csr_match(CpuState* hart, word_t addr, bool write) {
  const CsrDesc* csr = csr_lookup(addr);
  if (!csr) return NULL;
  if (csr->check && !csr->check(hart, csr, write)) return NULL;
  return csr;
}

word_t csr_desc_read(CpuState* hart, const CsrDesc* csr) {
  return csr->read ? csr->read(hart, csr) : csr_default_read(hart, csr);
}

void csr_desc_write(CpuState* hart, const CsrDesc* csr, word_t value) {
  if (csr->write)
    csr->write(hart, csr, value);
  else
    csr_default_write(hart, csr, value);
}

bool csr_accessible(Priv privilege, word_t addr, bool write) {
  if (!csr_addr_legal(privilege, addr, write)) return false;
  return csr_lookup(addr) != NULL;
}

bool csr_read(CpuState* hart, word_t addr, word_t* val) {
  if (!csr_addr_legal(hart->priv, addr, false)) return false;
  const CsrDesc* csr = csr_match(hart, addr, false);
  if (!csr) return false;
  *val = csr_desc_read(hart, csr);
  return true;
}

bool csr_write(CpuState* hart, word_t addr, word_t val) {
  if (!csr_addr_legal(hart->priv, addr, true)) return false;
  const CsrDesc* csr = csr_match(hart, addr, true);
  if (!csr) return false;
  csr_desc_write(hart, csr, val);
  return true;
}

bool csr_peek(CpuState* hart, word_t addr, word_t* val) {
  const CsrDesc* csr = csr_lookup(addr);
  if (!csr) return false;
  *val = csr_desc_read(hart, csr);
  return true;
}
