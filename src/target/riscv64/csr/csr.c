#include "csr.h"

#include <cpu/cpu.h>
#include <cpu/csr_list.h>
#include <cpu/interrupt.h>
#ifdef CONFIG_HAS_CLINT
#include <device/timer/clint.h>
#endif

#include "pmp.h"
#include "trap.h"

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


enum {
  MIE_IMPLEMENTED = MIP_SSIP | MIP_MSIP | MIP_STIP | MIP_MTIP | MIP_SEIP | MIP_MEIP,
  MIDELEG_IMPLEMENTED = MIP_SSIP | MIP_STIP | MIP_SEIP,
  MEDELEG_IMPLEMENTED = (1ull << EXCP_INST_ADDR_MIS) | (1ull << EXCP_INST_ACCESS_FAULT) |
                        (1ull << EXCP_ILLEGAL_INST) | (1ull << EXCP_BREAKPOINT) |
                        (1ull << EXCP_LOAD_ADDR_MIS) | (1ull << EXCP_LOAD_ACCESS_FAULT) |
                        (1ull << EXCP_STORE_AMO_ADDR_MIS) |
                        (1ull << EXCP_STORE_AMO_ACCESS_FAULT) | (1ull << EXCP_U_ECALL) |
                        (1ull << EXCP_S_ECALL) | (1ull << EXCP_INST_PAGE_FAULT) |
                        (1ull << EXCP_LOAD_PAGE_FAULT) | (1ull << EXCP_STORE_AMO_PAGE_FAULT),
  COUNTEREN_IMPLEMENTED = 7,
  COUNTINHIBIT_CYCLE = 1ull << 0,
  COUNTINHIBIT_INSTRET = 1ull << 2
};

word_t csr_epc_storage_value(word_t val) {
#ifdef CONFIG_RV_C
  if ((cpu.misa & MISA_C) != 0) return val & ~1ull;
#endif
  return val & ~3ull;
}

word_t csr_epc_read_value(word_t val) { return csr_epc_storage_value(val); }

word_t riscv_misa_supported(void) {
  word_t value = MISA_MXL_64 | MISA_I | MISA_S | MISA_U;
#ifdef CONFIG_RV_M
  value |= MISA_M;
#endif
#ifdef CONFIG_RV_A
  value |= MISA_A;
#endif
#ifdef CONFIG_RV_C
  value |= MISA_C;
#endif
#ifdef CONFIG_RV_F
  value |= MISA_F;
#endif
#ifdef CONFIG_RV_D
  value |= MISA_D;
#endif
  return value;
}

static word_t csr_read_zero(const CsrDesc* csr) {
  (void)csr;
  return 0;
}

static word_t csr_read_mhartid(const CsrDesc* csr) {
  (void)csr;
  return cpu.mhartid;
}

static word_t csr_read_misa(const CsrDesc* csr) {
  (void)csr;
  return cpu.misa;
}

static void csr_write_misa(const CsrDesc* csr, word_t value) {
  (void)csr;
  word_t next = riscv_misa_supported();
#ifdef CONFIG_RV_C
  if ((value & MISA_C) == 0) next &= ~MISA_C;
#else
  (void)value;
#endif
  cpu.misa = next;
}

static void csr_write_masked(const CsrDesc* csr, word_t value) {
  *csr->field = value & csr->write_mask;
}

static word_t csr_read_epc(const CsrDesc* csr) {
  return csr_epc_read_value(*csr->field);
}

static void csr_write_epc(const CsrDesc* csr, word_t value) {
  *csr->field = csr_epc_storage_value(value);
}

static void csr_write_tvec(const CsrDesc* csr, word_t value) {
  word_t mode = value & 3;
  if (mode > 1) mode = *csr->field & 3;
  *csr->field = (value & ~3ull) | mode;
}

static word_t csr_read_mie(const CsrDesc* csr) {
  (void)csr;
  return cpu.mie & MIE_IMPLEMENTED;
}

static void csr_write_mie(const CsrDesc* csr, word_t value) {
  (void)csr;
  cpu.mie = value & MIE_IMPLEMENTED;
}

static word_t csr_read_mip(const CsrDesc* csr) {
  (void)csr;
  cpu_sync_irq_inputs();
  return cpu.mip & MIE_IMPLEMENTED;
}

static void csr_write_mip(const CsrDesc* csr, word_t value) {
  (void)csr;
  /* 没有 Sstc 时 STIP 由 M 态软件置位，表示 S 态时钟中断。
   * SEIP 由 PLIC 线驱动，不接受软件粘滞。 */
  const word_t software = MIP_SSIP | MIP_STIP;
  const word_t writable = software | MIP_MSIP;
  cpu.mip_software = (cpu.mip_software & ~software) | (value & software);
  cpu.mip = (cpu.mip & ~writable) | (value & writable);
  cpu_sync_irq_inputs();
}

static word_t csr_read_sie(const CsrDesc* csr) {
  (void)csr;
  return cpu.mie & cpu.mideleg & SIE_MASK;
}

static void csr_write_sie(const CsrDesc* csr, word_t value) {
  (void)csr;
  const word_t mask = cpu.mideleg & SIE_MASK;
  cpu.mie = (cpu.mie & ~mask) | (value & mask);
}

static word_t csr_read_sip(const CsrDesc* csr) {
  (void)csr;
  cpu_sync_irq_inputs();
  return cpu.mip & cpu.mideleg & SIE_MASK;
}

static void csr_write_sip(const CsrDesc* csr, word_t value) {
  (void)csr;
  const word_t mask = cpu.mideleg & MIP_SSIP;
  cpu.mip_software = (cpu.mip_software & ~mask) | (value & mask);
  cpu_sync_irq_inputs();
}

static bool csr_check_satp(const CsrDesc* csr, bool write) {
  (void)csr;
  (void)write;
  return !(cpu.priv == PRV_S && (cpu.mstatus & MSTATUS_TVM) != 0);
}

static void csr_write_satp(const CsrDesc* csr, word_t value) {
  (void)csr;
  word_t mode = value & SATP64_MODE;
  if (mode != SATP64_MODE_BARE && mode != SATP64_MODE_SV39) {
    mode = cpu.satp & SATP64_MODE;
  }
  cpu.satp = mode | (value & (SATP64_ASID | SATP64_PPN));
}

static bool csr_check_counter(const CsrDesc* csr, bool write) {
  (void)write;
  if (cpu.priv >= PRV_M) return true;
  const unsigned index = (unsigned)(csr->addr - CSR_CYCLE);
  const word_t bit = 1ull << index;
  if ((cpu.mcounteren & bit) == 0) return false;
  if (cpu.priv == PRV_U && (cpu.scounteren & bit) == 0) return false;
  return true;
}

static word_t csr_read_cycle(const CsrDesc* csr) {
  (void)csr;
  return cpu.mcycle;
}

static word_t csr_read_instret(const CsrDesc* csr) {
  (void)csr;
  return cpu.minstret;
}

static word_t csr_read_time(const CsrDesc* csr) {
  (void)csr;
#ifdef CONFIG_HAS_CLINT
  return clint_mtime_now();
#else
  return 0;
#endif
}

#define RVISS_CSR_IMPL(X)                                                      \
  X(mstatus, CSR_MSTATUS, &cpu.mstatus, ~0ull, ~0ull, NULL, csr_read_mstatus,   \
    csr_write_mstatus)                                                         \
  X(sstatus, CSR_SSTATUS, &cpu.mstatus, ~0ull, ~0ull, NULL, csr_read_sstatus,   \
    csr_write_sstatus)                                                         \
  X(misa, CSR_MISA, &cpu.misa, ~0ull, ~0ull, NULL, csr_read_misa, csr_write_misa) \
  X(medeleg, CSR_MEDELEG, &cpu.medeleg, MEDELEG_IMPLEMENTED,                    \
    MEDELEG_IMPLEMENTED, NULL, NULL, csr_write_masked)                         \
  X(mideleg, CSR_MIDELEG, &cpu.mideleg, MIDELEG_IMPLEMENTED,                    \
    MIDELEG_IMPLEMENTED, NULL, NULL, csr_write_masked)                         \
  X(mie, CSR_MIE, &cpu.mie, MIE_IMPLEMENTED, MIE_IMPLEMENTED, NULL,             \
    csr_read_mie, csr_write_mie)                                               \
  X(mip, CSR_MIP, &cpu.mip, MIE_IMPLEMENTED, MIE_IMPLEMENTED, NULL,             \
    csr_read_mip, csr_write_mip)                                               \
  X(mtvec, CSR_MTVEC, &cpu.mtvec, ~0ull, ~0ull, NULL, NULL, csr_write_tvec)    \
  X(mscratch, CSR_MSCRATCH, &cpu.mscratch, ~0ull, ~0ull, NULL, NULL, NULL)     \
  X(mepc, CSR_MEPC, &cpu.mepc, ~0ull, ~0ull, NULL, csr_read_epc, csr_write_epc) \
  X(mcause, CSR_MCAUSE, &cpu.mcause, ~0ull, ~0ull, NULL, NULL, NULL)           \
  X(mtval, CSR_MTVAL, &cpu.mtval, ~0ull, ~0ull, NULL, NULL, NULL)              \
  X(mcounteren, CSR_MCOUNTEREN, &cpu.mcounteren, COUNTEREN_IMPLEMENTED,        \
    COUNTEREN_IMPLEMENTED, NULL, NULL, csr_write_masked)                       \
  X(mcountinhibit, CSR_MCOUNTINHIBIT, &cpu.mcountinhibit,                      \
    COUNTINHIBIT_CYCLE | COUNTINHIBIT_INSTRET,                                \
    COUNTINHIBIT_CYCLE | COUNTINHIBIT_INSTRET, NULL, NULL, csr_write_masked)  \
  X(mcycle, CSR_MCYCLE, &cpu.mcycle, ~0ull, ~0ull, NULL, NULL, NULL)           \
  X(minstret, CSR_MINSTRET, &cpu.minstret, ~0ull, ~0ull, NULL, NULL, NULL)     \
  X(stvec, CSR_STVEC, &cpu.stvec, ~0ull, ~0ull, NULL, NULL, csr_write_tvec)    \
  X(scounteren, CSR_SCOUNTEREN, &cpu.scounteren, COUNTEREN_IMPLEMENTED,        \
    COUNTEREN_IMPLEMENTED, NULL, NULL, csr_write_masked)                       \
  X(sscratch, CSR_SSCRATCH, &cpu.sscratch, ~0ull, ~0ull, NULL, NULL, NULL)     \
  X(sepc, CSR_SEPC, &cpu.sepc, ~0ull, ~0ull, NULL, csr_read_epc, csr_write_epc) \
  X(scause, CSR_SCAUSE, &cpu.scause, ~0ull, ~0ull, NULL, NULL, NULL)           \
  X(stval, CSR_STVAL, &cpu.stval, ~0ull, ~0ull, NULL, NULL, NULL)              \
  X(satp, CSR_SATP, &cpu.satp, ~0ull, ~0ull, csr_check_satp, NULL,              \
    csr_write_satp)                                                           \
  X(sie, CSR_SIE, &cpu.mie, SIE_MASK, SIE_MASK, NULL, csr_read_sie,            \
    csr_write_sie)                                                             \
  X(sip, CSR_SIP, &cpu.mip, SIE_MASK, SIE_MASK, NULL, csr_read_sip,            \
    csr_write_sip)                                                             \
  X(cycle, CSR_CYCLE, &cpu.mcycle, ~0ull, ~0ull, csr_check_counter,            \
    csr_read_cycle, NULL)                                                      \
  X(time, CSR_TIME, NULL, ~0ull, 0, csr_check_counter, csr_read_time, NULL)    \
  X(instret, CSR_INSTRET, &cpu.minstret, ~0ull, ~0ull, csr_check_counter,      \
    csr_read_instret, NULL)                                                    \
  X(mvendorid, CSR_MVENDORID, NULL, 0, 0, NULL, csr_read_zero, NULL)           \
  X(marchid, CSR_MARCHID, NULL, 0, 0, NULL, csr_read_zero, NULL)               \
  X(mimpid, CSR_MIMPID, NULL, 0, 0, NULL, csr_read_zero, NULL)                 \
  X(mhartid, CSR_MHARTID, &cpu.mhartid, ~0ull, 0, NULL, csr_read_mhartid, NULL)               \
  X(mconfigptr, CSR_MCONFIGPTR, NULL, 0, 0, NULL, csr_read_zero, NULL)

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
  if (!csr_addr_legal(addr, false)) return false;
  if (riscv_pmp_csr(addr)) {
    *val = riscv_pmp_read(addr);
    return true;
  }
  const CsrDesc* csr = csr_match(addr, false);
  if (!csr) return false;
  *val = csr_desc_read(csr);
  return true;
}

bool csr_write(word_t addr, word_t val) {
  if (!csr_addr_legal(addr, true)) return false;
  if (riscv_pmp_csr(addr)) {
    riscv_pmp_write(addr, val);
    return true;
  }
  const CsrDesc* csr = csr_match(addr, true);
  if (!csr) return false;
  csr_desc_write(csr, val);
  return true;
}

bool csr_peek(word_t addr, word_t* val) {
  if (riscv_pmp_csr(addr)) {
    *val = riscv_pmp_read(addr);
    return true;
  }
  const CsrDesc* csr = csr_lookup(addr);
  if (!csr) return false;
  *val = csr_desc_read(csr);
  return true;
}
