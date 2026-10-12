#include <common.h>
#include <cpu/csr_list.h>
#include <cpu/decode.h>
#include <device/device.h>

#include "cpu_state.h"
#include "csr.h"
#include "encoding.h"
#include "intr.h"
#include "pmp.h"
#include "mmu.h"

/* 地址/存储元信息来自唯一清单；有 WARL、别名或副作用的 CSR 显式处理。 */
typedef struct {
  word_t addr;
  word_t *field;
  word_t view_mask;
} CsrEntry;

#define CSR_ENTRY_OWNED(_n, _a, _m) {_a, &cpu._n, _m},
#define CSR_ENTRY_VIEW(_n, _a, _f, _m) {_a, &cpu._f, _m},
#define CSR_ENTRY_COMPUTED(_n, _a) {_a, NULL, CSR_MASK_ALL},
#define CSR_ENTRY_HPM(_i) {CSR_MHPMCOUNTER3+_i-3, NULL, 0}, {CSR_MHPMEVENT3+_i-3, NULL, 0},
#define CSR_ENTRY_PMP(_i) {CSR_PMPADDR0+_i,NULL,CSR_MASK_ALL},
#define CSR_ENTRY_PMPCFG(_i) {CSR_PMPCFG0+_i,NULL,CSR_MASK_ALL},
static CsrEntry csr_table[] = {RVISS_CSRS_OWNED(CSR_ENTRY_OWNED)
                             RVISS_CSRS_VIEW(CSR_ENTRY_VIEW)
                             RVISS_CSRS_COMPUTED(CSR_ENTRY_COMPUTED)
                             RVISS_HPM_INDICES(CSR_ENTRY_HPM)
                             RVISS_PMP_INDICES(CSR_ENTRY_PMP)
                             RVISS_PMPCFG_INDICES(CSR_ENTRY_PMPCFG)};
#undef CSR_ENTRY_OWNED
#undef CSR_ENTRY_VIEW
#undef CSR_ENTRY_COMPUTED
#undef CSR_ENTRY_HPM
#undef CSR_ENTRY_PMP
#undef CSR_ENTRY_PMPCFG

static CsrEntry *csr_lookup(word_t addr) {
  for (size_t i = 0; i < ARRLEN(csr_table); i++) {
    if (csr_table[i].addr == addr) return &csr_table[i];
  }
  return NULL;
}

/* RV64、little endian、无 VS/XS。S 实现保留 FS 以允许软件模拟浮点。 */
word_t csr_status_value(word_t val) {
  val &= MSTATUS_WRITABLE_MASK;
  if ((val & MSTATUS_MPP) == (2ull << 11)) val &= ~MSTATUS_MPP;
  val |= MSTATUS_UXL_64 | MSTATUS_SXL_64;
  if ((val & MSTATUS_FS) == MSTATUS_FS_DIRTY) val |= MSTATUS_SD;
  return val;
}

word_t csr_epc_storage_value(word_t val) {
#ifdef CONFIG_RV_C
  return val & ~1ull;
#else
  return val & ~3ull;
#endif
}

word_t csr_epc_read_value(word_t val) {
  return val & ((cpu.misa & MISA_C) ? ~1ull : ~3ull);
}

void csr_reset(void) {
  cpu.mstatus = csr_status_value(0);
}

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
  /* M/S/U CSR/委派/xRET/IRQ/counter/PMP/Sv39 已闭环；F/D 后续接入。 */
  return value;
}

static bool csr_allowed(word_t addr, bool write) {
  if (!csr_lookup(addr) || cpu.priv < ((addr >> 8) & 3)) return false;
  if (riscv_pmp_csr(addr) && !cpu.pmp_count) return false;
  if (write && ((addr >> 10) & 3) == 3) return false;
  if (addr >= CSR_CYCLE && addr <= CSR_INSTRET && cpu.priv < PRV_M) {
    word_t enable=1ull<<(addr-CSR_CYCLE);
    if (!(cpu.mcounteren & enable) || (cpu.priv == PRV_U && !(cpu.scounteren & enable)))
      return false;
  }
  if (addr == CSR_SATP && cpu.priv == PRV_S && (cpu.mstatus & MSTATUS_TVM))
    return false;
  return true;
}

bool csr_peek(word_t addr, word_t *val) {
  CsrEntry *e = csr_lookup(addr);
  if (!e) return false;
  if(riscv_pmp_csr(addr)) { *val=riscv_pmp_read(addr); return true; }
  switch (addr) {
    case CSR_TIME: *val = device_time(); break;
    case CSR_MCOUNTINHIBIT: *val = 0; break;
    case CSR_MSTATUS: *val = csr_status_value(cpu.mstatus); break;
    case CSR_SSTATUS: *val = csr_status_value(cpu.mstatus) & SSTATUS_MASK; break;
    case CSR_MEPC: case CSR_SEPC:
      /* C 的实现支持 bit 1 存储；当前 IALIGN=32 时仅屏蔽读值。 */
      *val = csr_epc_read_value(*e->field);
      break;
    case CSR_SIE: *val = cpu.mie & cpu.mideleg & SIE_MASK; break;
    case CSR_MIP: case CSR_SIP:
      cpu_sync_irq_inputs();
      *val = cpu.mip & (addr == CSR_MIP ? MIE_MASK : cpu.mideleg & SIE_MASK);
      break;
    default: *val = e->field ? *e->field & e->view_mask : 0; break;
  }
  return true;
}

bool csr_read(word_t addr, word_t *val) {
  return csr_allowed(addr, false) && csr_peek(addr, val);
}

bool csr_write(word_t addr, word_t val) {
  if (!csr_allowed(addr, true)) return false;
  CsrEntry *e = csr_lookup(addr);
  if(riscv_pmp_csr(addr)) { riscv_pmp_write(addr,val); return true; }
  switch (addr) {
    case CSR_MCOUNTINHIBIT: break;
    case CSR_MISA: {
      word_t supported = riscv_misa_supported();
      cpu.misa = (supported & ~MISA_C) | (supported & val & MISA_C);
      break;
    }
    case CSR_MSTATUS: cpu.mstatus = csr_status_value(val); break;
    case CSR_SSTATUS:
      cpu.mstatus = csr_status_value((cpu.mstatus & ~SSTATUS_WRITABLE_MASK) |
                                    (val & SSTATUS_WRITABLE_MASK));
      break;
    case CSR_MTVEC: case CSR_STVEC: *e->field = val & ~2ull; break;
    case CSR_MEPC: case CSR_SEPC: *e->field = csr_epc_storage_value(val); break;
    case CSR_MEDELEG: cpu.medeleg = val & MEDELEG_MASK; break;
    case CSR_MIDELEG: cpu.mideleg = val & SIE_MASK; break;
    case CSR_MIE: cpu.mie = val & MIE_MASK; break;
    case CSR_SIE: {
      word_t mask = cpu.mideleg & SIE_MASK;
      cpu.mie = (cpu.mie & ~mask) | (val & mask);
      break;
    }
    case CSR_MIP:
      cpu.mip_software = val & SIE_MASK;
      cpu_sync_irq_inputs();
      break;
    case CSR_SIP: {
      word_t mask = cpu.mideleg & MIP_SSIP;
      cpu.mip_software = (cpu.mip_software & ~mask) | (val & mask);
      cpu_sync_irq_inputs();
      break;
    }
    case CSR_MCOUNTEREN: case CSR_SCOUNTEREN: *e->field = val & 7; break;
    case CSR_SATP: cpu.satp = riscv_satp_value(val); break;
    default: if(e->field) *e->field = val & e->view_mask; break;
  }
  return true;
}

void csr_execute(Decode *s, int op, bool immediate) {
  word_t addr = BITS(s->instr, 31, 20);
  word_t src = immediate ? ZIMM5(s->instr) : *s->src1.preg;
  bool write = op == CSR_OP_RW || s->rs1 != 0;
  word_t old = 0;
  if (!csr_allowed(addr, write)) {
    riscv_exec_trap(s, EX_II, s->instr);
    return;
  }
  /* CSRRW rd=x0 完全抑制读取，而不是读取后丢弃。RS/RC 始终读取。 */
  if (op != CSR_OP_RW || s->rd != 0) csr_peek(addr, &old);
  if (write) {
    word_t rmw_old = old;
    if (addr == CSR_MIP) {
      /* SEIP 的 RMW 操作数是软件位 B；rd 返回 B | 设备线 E。 */
      rmw_old = (old & ~MIP_SEIP) | (cpu.mip_software & MIP_SEIP);
    }
    word_t val = op == CSR_OP_RW ? src : op == CSR_OP_RS ? rmw_old | src : rmw_old & ~src;
    bool suppress = addr == CSR_MISA && (cpu.misa & MISA_C) &&
                    !(val & MISA_C) && (s->snpc & 3);
    if (!suppress && !csr_write(addr, val)) {
      riscv_exec_trap(s, EX_II, s->instr);
      return;
    }
    if (!suppress) {
      s->counter_write_cycle = addr == CSR_MCYCLE;
      s->counter_write_instret = addr == CSR_MINSTRET;
    }
  }
  if (s->rd != 0) *s->dest.preg = old;
}
