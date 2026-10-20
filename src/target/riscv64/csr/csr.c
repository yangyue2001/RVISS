#include "csr.h"

#include <cpu/cpu.h>
#include <cpu/csr_list.h>

/*
 * 一条 CSR 在 RVISS_CSR_IMPL 里占一行。访问顺序是地址合法性、查表、
 * 可选 check、然后 read/write。别名和原寄存器指向同一个 field。
 */

#define SSTATUS_MASK                                                     \
  (MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP | MSTATUS_FS | MSTATUS_SUM | \
   MSTATUS_MXR | MSTATUS_UXL | MSTATUS_SD)
#define SSTATUS_WRITABLE_MASK                                            \
  (MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_SPP | MSTATUS_FS | MSTATUS_SUM | \
   MSTATUS_MXR)
#define MSTATUS_WRITABLE_MASK                                         \
  (SSTATUS_WRITABLE_MASK | MSTATUS_MIE | MSTATUS_MPIE | MSTATUS_MPP | \
   MSTATUS_MPRV | MSTATUS_TVM | MSTATUS_TW | MSTATUS_TSR)

word_t csr_status_value(word_t val) {
  val &= MSTATUS_WRITABLE_MASK;
  /* 这里没有旧值可比，非法 MPP 只能清掉。写路径会先把它换回旧编码。 */
  if ((val & MSTATUS_MPP) == (2ull << 11)) val &= ~MSTATUS_MPP;
  val |= MSTATUS_UXL_64 | MSTATUS_SXL_64;
  if ((val & MSTATUS_FS) == MSTATUS_FS_DIRTY) val |= MSTATUS_SD;
  return val;
}

static word_t csr_default_read(const CsrDesc* csr) {
  Assert(csr->field, "CSR %s has no storage for the default read", csr->name);
  return *csr->field & csr->read_mask;
}

static void csr_default_write(const CsrDesc* csr, word_t value) {
  Assert(csr->field, "CSR %s has no storage for the default write", csr->name);
  *csr->field = (*csr->field & ~csr->write_mask) | (value & csr->write_mask);
}

static word_t csr_read_mstatus(const CsrDesc* csr) {
  (void)csr;
  return csr_status_value(cpu.mstatus);
}

static void csr_write_mstatus(const CsrDesc* csr, word_t value) {
  (void)csr;
  word_t merged = (cpu.mstatus & ~MSTATUS_WRITABLE_MASK) |
                  (value & MSTATUS_WRITABLE_MASK);
  if ((merged & MSTATUS_MPP) == (2ull << 11)) {
    merged = (merged & ~MSTATUS_MPP) | (cpu.mstatus & MSTATUS_MPP);
  }
  cpu.mstatus = csr_status_value(merged);
}

static word_t csr_read_sstatus(const CsrDesc* csr) {
  return csr_read_mstatus(csr) & SSTATUS_MASK;
}

/* S 视图先并回完整 mstatus，再走同一套 WARL，避免两份提交规则。 */
static void csr_write_sstatus(const CsrDesc* csr, word_t value) {
  word_t merged = (cpu.mstatus & ~SSTATUS_WRITABLE_MASK) |
                  (value & SSTATUS_WRITABLE_MASK);
  csr_write_mstatus(csr, merged);
}

#ifdef CONFIG_RV_F
static bool csr_check_fp(const CsrDesc* csr, bool write) {
  (void)csr;
  (void)write;
  return (cpu.mstatus & MSTATUS_FS) != 0;
}

static void csr_fp_dirty(void) {
  cpu.mstatus = csr_status_value(cpu.mstatus | MSTATUS_FS_DIRTY);
}

static void csr_write_fp(const CsrDesc* csr, word_t value) {
  csr_default_write(csr, value);
  csr_fp_dirty();
}

static word_t csr_read_frm(const CsrDesc* csr) {
  (void)csr;
  return (cpu.fcsr >> 5) & 7;
}

static void csr_write_frm(const CsrDesc* csr, word_t value) {
  (void)csr;
  cpu.fcsr = (cpu.fcsr & 31) | ((value & 7) << 5);
  csr_fp_dirty();
}
#endif

#define RVISS_CSR_IMPL(X)                                                    \
  X(mstatus, CSR_MSTATUS, &cpu.mstatus, ~0ull, ~0ull, NULL, csr_read_mstatus, \
    csr_write_mstatus)                                                       \
  X(sstatus, CSR_SSTATUS, &cpu.mstatus, ~0ull, ~0ull, NULL, csr_read_sstatus, \
    csr_write_sstatus)

#ifdef CONFIG_RV_F
#define RVISS_CSR_FP(X)                                                \
  X(fflags, CSR_FFLAGS, &cpu.fcsr, 31, 31, csr_check_fp, NULL,         \
    csr_write_fp)                                                      \
  X(frm, CSR_FRM, &cpu.fcsr, ~0ull, ~0ull, csr_check_fp, csr_read_frm, \
    csr_write_frm)                                                     \
  X(fcsr, CSR_FCSR, &cpu.fcsr, 0xff, 0xff, csr_check_fp, NULL, csr_write_fp)
#else
#define RVISS_CSR_FP(X)
#endif

#define CSR_DECL(name, addr, field, rmask, wmask, check, read, write) \
  static const CsrDesc csr_desc_##name = {                            \
      #name, addr, field, rmask, wmask, check, read, write};
RVISS_CSR_IMPL(CSR_DECL)
RVISS_CSR_FP(CSR_DECL)
#undef CSR_DECL

#define CSR_SLOT(name, addr, field, rmask, wmask, check, read, write) \
  [addr] = &csr_desc_##name,
static const CsrDesc* const csr_map[4096] = {RVISS_CSR_IMPL(CSR_SLOT)
                                                 RVISS_CSR_FP(CSR_SLOT)};
#undef CSR_SLOT

bool csr_addr_legal(word_t addr, bool write) {
  if (addr > 0xfff) return false;
  if (cpu.priv < ((addr >> 8) & 3)) return false;
  if (write && ((addr >> 10) & 3) == 3) return false;
  return true;
}

const CsrDesc* csr_lookup(word_t addr) {
  if (addr > 0xfff) return NULL;
  return csr_map[addr];
}

const CsrDesc* csr_match(word_t addr, bool write) {
  const CsrDesc* csr;
  if (!csr_addr_legal(addr, write)) return NULL;
  csr = csr_lookup(addr);
  if (!csr) return NULL;
  if (csr->check && !csr->check(csr, write)) return NULL;
  return csr;
}

word_t csr_desc_read(const CsrDesc* csr) {
  return csr->read ? csr->read(csr) : csr_default_read(csr);
}

void csr_desc_write(const CsrDesc* csr, word_t value) {
  if (csr->write) csr->write(csr, value);
  else csr_default_write(csr, value);
}

bool csr_read(word_t addr, word_t* val) {
  const CsrDesc* csr = csr_match(addr, false);
  if (!csr) return false;
  *val = csr_desc_read(csr);
  return true;
}

bool csr_write(word_t addr, word_t val) {
  const CsrDesc* csr = csr_match(addr, true);
  if (!csr) return false;
  csr_desc_write(csr, val);
  return true;
}

bool csr_peek(word_t addr, word_t* val) {
  const CsrDesc* csr = csr_lookup(addr);
  if (!csr) return false;
  *val = csr_desc_read(csr);
  return true;
}
