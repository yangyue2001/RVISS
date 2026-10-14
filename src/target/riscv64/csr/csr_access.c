#include <common.h>
#include <cpu/csr_list.h>
#include <cpu/decode.h>
#include <device/device.h>

#include "cpu_state.h"
#include "csr.h"
#include "csr_rules.h"
#include "encoding.h"
#include "fp.h"
#include "intr.h"
#include "mmu.h"
#include "pmp.h"

/* 地址/存储元信息来自唯一清单；有 WARL、别名或副作用的 CSR 显式处理。 */
typedef struct {
  word_t addr;
  word_t* field;
  word_t view_mask;
} CsrEntry;

word_t csr_rules_read(const CsrFieldRule* rules, size_t count, word_t value) {
  word_t known = 0;
  for (size_t i = 0; i < count; i++) {
    known |= rules[i].mask;
    if (rules[i].kind == CSR_FIELD_WPRI) value &= ~rules[i].mask;
  }
  /* 规则表之外的位属于未实现保留字段，读出时忽略。 */
  return value & known;
}

word_t csr_rules_write(const CsrFieldRule* rules, size_t count,
                       word_t old_value, word_t requested) {
  word_t known = 0;
  word_t result = old_value;
  for (size_t i = 0; i < count; i++) {
    const CsrFieldRule* rule = &rules[i];
    word_t old_field = old_value & rule->mask;
    word_t new_field = old_field;
    known |= rule->mask;
    switch (rule->kind) {
      case CSR_FIELD_RW:
        new_field = requested & rule->mask;
        break;
      case CSR_FIELD_WPRI:
      case CSR_FIELD_RO:
        break;
      case CSR_FIELD_WARL:
        if (rule->legal && rule->legal(requested & rule->mask))
          new_field = requested & rule->mask;
        else if (rule->normalize)
          new_field = rule->normalize(requested & rule->mask, old_field) &
                      rule->mask;
        break;
      case CSR_FIELD_WLRL:
        /* RVISS 选择确定性的“非法值保持旧值”实现策略。 */
        if (rule->legal && rule->legal(requested & rule->mask))
          new_field = requested & rule->mask;
        break;
    }
    result = (result & ~rule->mask) | new_field;
  }
  /* 未列出的字段是 WPRI，因此保留旧值。 */
  return (result & known) | (old_value & ~known);
}

#define CSR_ENTRY_OWNED(_n, _a, _m) {_a, &cpu._n, _m},
#define CSR_ENTRY_VIEW(_n, _a, _f, _m) {_a, &cpu._f, _m},
#define CSR_ENTRY_COMPUTED(_n, _a) {_a, NULL, CSR_MASK_ALL},
#define CSR_ENTRY_HPM(_i) \
  {CSR_MHPMCOUNTER3 + _i - 3, NULL, 0}, {CSR_MHPMEVENT3 + _i - 3, NULL, 0},
#define CSR_ENTRY_PMP(_i) {CSR_PMPADDR0 + _i, NULL, CSR_MASK_ALL},
#define CSR_ENTRY_PMPCFG(_i) {CSR_PMPCFG0 + _i, NULL, CSR_MASK_ALL},
static CsrEntry csr_table[] = {
    RVISS_CSRS_OWNED(CSR_ENTRY_OWNED) RVISS_CSRS_VIEW(CSR_ENTRY_VIEW)
        RVISS_CSRS_COMPUTED(CSR_ENTRY_COMPUTED) RVISS_HPM_INDICES(CSR_ENTRY_HPM)
            RVISS_PMP_INDICES(CSR_ENTRY_PMP)
                RVISS_PMPCFG_INDICES(CSR_ENTRY_PMPCFG)};
#undef CSR_ENTRY_OWNED
#undef CSR_ENTRY_VIEW
#undef CSR_ENTRY_COMPUTED
#undef CSR_ENTRY_HPM
#undef CSR_ENTRY_PMP
#undef CSR_ENTRY_PMPCFG

static const CsrFieldRule csr_delegation_rules[] = {
    {MEDELEG_MASK, CSR_FIELD_RW, NULL, NULL},
};
static const CsrFieldRule csr_mideleg_rules[] = {
    {SIE_MASK, CSR_FIELD_RW, NULL, NULL},
};
static const CsrFieldRule csr_mie_rules[] = {
    {MIE_MASK, CSR_FIELD_RW, NULL, NULL},
};
static const CsrFieldRule csr_counteren_rules[] = {
    {UINT64_C(7), CSR_FIELD_RW, NULL, NULL},
};

static bool csr_mpp_legal(word_t value) {
  return value == MSTATUS_MPP_U || value == MSTATUS_MPP_S ||
         value == MSTATUS_MPP_M;
}

static word_t csr_mpp_normalize(word_t requested, word_t old_value) {
  return csr_mpp_legal(requested) ? requested : old_value;
}

static bool csr_fs_legal(word_t value) { return value <= MSTATUS_FS_DIRTY; }

static word_t csr_fs_normalize(word_t requested, word_t old_value) {
  return csr_fs_legal(requested) ? requested : old_value;
}

static bool csr_status_xlen_legal(word_t value) {
  return value == (MSTATUS_UXL_64 | MSTATUS_SXL_64);
}

static word_t csr_status_xlen_normalize(word_t requested, word_t old_value) {
  (void)requested;
  (void)old_value;
  return MSTATUS_UXL_64 | MSTATUS_SXL_64;
}

static word_t csr_misa_normalize(word_t requested, word_t old_value) {
  (void)old_value;
  word_t supported = riscv_misa_supported();
  /* 当前 profile 只允许 C 由客户机动态关闭；其它扩展是固定能力。 */
  return (supported & ~MISA_C) | (supported & requested & MISA_C);
}

static word_t csr_epc_normalize(word_t requested, word_t old_value) {
  (void)old_value;
  return csr_epc_storage_value(requested);
}

static word_t csr_tvec_normalize(word_t requested, word_t old_value) {
  (void)old_value;
  return requested & ~UINT64_C(2);
}

static word_t csr_satp_normalize(word_t requested, word_t old_value) {
  (void)old_value;
  return riscv_satp_value(requested);
}

static const CsrFieldRule csr_mstatus_rules[] = {
    {MSTATUS_WRITABLE_MASK, CSR_FIELD_RW, NULL, NULL},
    {MSTATUS_MPP, CSR_FIELD_WARL, csr_mpp_legal, csr_mpp_normalize},
    {MSTATUS_FS, CSR_FIELD_WARL, csr_fs_legal, csr_fs_normalize},
    {MSTATUS_UXL | MSTATUS_SXL, CSR_FIELD_WARL, csr_status_xlen_legal,
     csr_status_xlen_normalize},
    {MSTATUS_SD, CSR_FIELD_RO, NULL, NULL},
};

static const CsrFieldRule csr_sstatus_rules[] = {
    {SSTATUS_WRITABLE_MASK, CSR_FIELD_RW, NULL, NULL},
    {MSTATUS_FS, CSR_FIELD_WARL, csr_fs_legal, csr_fs_normalize},
    {MSTATUS_UXL, CSR_FIELD_RO, NULL, NULL},
    {MSTATUS_SD, CSR_FIELD_RO, NULL, NULL},
};

static const CsrFieldRule csr_misa_rules[] = {
    {CSR_MASK_ALL, CSR_FIELD_WARL, NULL, csr_misa_normalize},
};

static const CsrFieldRule csr_tvec_rules[] = {
    {CSR_MASK_ALL, CSR_FIELD_WARL, NULL, csr_tvec_normalize},
};

static const CsrFieldRule csr_epc_rules[] = {
    {CSR_MASK_ALL, CSR_FIELD_WARL, NULL, csr_epc_normalize},
};

static const CsrFieldRule csr_satp_rules[] = {
    {CSR_MASK_ALL, CSR_FIELD_WARL, NULL, csr_satp_normalize},
};

static bool csr_cause_legal(word_t value) {
  word_t code = value & ~IRQ_BIT;
  if (value & IRQ_BIT) {
    return code == IRQ_SSIP || code == IRQ_MSIP || code == IRQ_STIP ||
           code == IRQ_MTIP || code == IRQ_SEIP || code == IRQ_MEIP;
  }
  switch (code) {
    case EX_IAM:
    case EX_IAF:
    case EX_II:
    case EX_BP:
    case EX_LAM:
    case EX_LAF:
    case EX_SAM:
    case EX_SAF:
    case EX_ECU:
    case EX_ECS:
    case EX_ECM:
    case EX_IPF:
    case EX_LPF:
    case EX_SPF:
      return true;
    default:
      return false;
  }
}

static const CsrFieldRule csr_cause_rules[] = {
    {CSR_MASK_ALL, CSR_FIELD_WLRL, csr_cause_legal, NULL},
};

static const CsrFieldRule* csr_rules_for(word_t addr, size_t* count) {
  *count = 0;
  switch (addr) {
    case CSR_MSTATUS:
      *count = ARRLEN(csr_mstatus_rules);
      return csr_mstatus_rules;
    case CSR_SSTATUS:
      *count = ARRLEN(csr_sstatus_rules);
      return csr_sstatus_rules;
    case CSR_MISA:
      *count = ARRLEN(csr_misa_rules);
      return csr_misa_rules;
    case CSR_MTVEC:
    case CSR_STVEC:
      *count = ARRLEN(csr_tvec_rules);
      return csr_tvec_rules;
    case CSR_MEPC:
    case CSR_SEPC:
      *count = ARRLEN(csr_epc_rules);
      return csr_epc_rules;
    case CSR_SATP:
      *count = ARRLEN(csr_satp_rules);
      return csr_satp_rules;
    case CSR_MEDELEG:
      *count = ARRLEN(csr_delegation_rules);
      return csr_delegation_rules;
    case CSR_MIDELEG:
      *count = ARRLEN(csr_mideleg_rules);
      return csr_mideleg_rules;
    case CSR_MIE:
      *count = ARRLEN(csr_mie_rules);
      return csr_mie_rules;
    case CSR_MCOUNTEREN:
    case CSR_SCOUNTEREN:
      *count = ARRLEN(csr_counteren_rules);
      return csr_counteren_rules;
    case CSR_MCAUSE:
    case CSR_SCAUSE:
      *count = ARRLEN(csr_cause_rules);
      return csr_cause_rules;
    default:
      return NULL;
  }
}

static CsrEntry* csr_lookup(word_t addr) {
#ifndef CONFIG_RV_ZICNTR
  if (addr >= CSR_CYCLE && addr <= CSR_INSTRET) return NULL;
#endif
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

void csr_reset(void) { cpu.mstatus = csr_status_value(0); }

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
  /* 编译能力经规则组验证后宣告；C 可由客户机切换，其它能力保持固定。 */
  return value;
}

static inline bool csr_check_priv(word_t addr) {
  return cpu.priv >= ((addr >> 8) & 3);
}
static inline bool csr_check_writable(word_t addr, bool write) {
  return !write || ((addr >> 10) & 3) != 3;
}
static inline bool csr_check_fp(word_t addr) {
  return !IN_RANGE_CC(addr, CSR_FFLAGS, CSR_FCSR) || riscv_fp_enabled(false);
}
static inline bool csr_check_tvm(word_t addr) {
  return !(addr == CSR_SATP && cpu.priv == PRV_S &&
           (cpu.mstatus & MSTATUS_TVM));
}
static inline bool csr_check_counter(word_t addr) {
  bool ret = true;
  if (IN_RANGE_CC(addr, CSR_CYCLE, CSR_INSTRET) && cpu.priv < PRV_M) {
    word_t enable = 1ull << (addr - CSR_CYCLE);
    if ((!(cpu.mcounteren & enable)) ||
        (cpu.priv == PRV_U && !(cpu.scounteren & enable))) {
      ret = false;
    }
  }
  return ret;
}

static bool csr_allowed(word_t addr, bool write) {
  return csr_lookup(addr) && csr_check_priv(addr) &&
         csr_check_counter(addr) && csr_check_tvm(addr) &&
         csr_check_fp(addr) && csr_check_writable(addr, write);
}

bool csr_peek(word_t addr, word_t* val) {
  CsrEntry* e = csr_lookup(addr);
  if (!e) return false;
  size_t rule_count = 0;
  const CsrFieldRule* rules = csr_rules_for(addr, &rule_count);
  if (riscv_pmp_csr(addr)) {
    *val = riscv_pmp_read(addr);
    return true;
  }
  switch (addr) {
    case CSR_FFLAGS:
      *val = cpu.fcsr & 31;
      break;
    case CSR_FRM:
      *val = (cpu.fcsr >> 5) & 7;
      break;
    case CSR_TIME:
      *val = device_time();
      break;
    case CSR_MCOUNTINHIBIT:
      *val = 0;
      break;
    case CSR_MSTATUS:
      *val = csr_status_value(cpu.mstatus);
      break;
    case CSR_SSTATUS:
      *val = csr_status_value(cpu.mstatus) & SSTATUS_MASK;
      break;
    case CSR_MEPC:
    case CSR_SEPC:
      /* C 的实现支持 bit 1 存储；当前 IALIGN=32 时仅屏蔽读值。 */
      *val = csr_epc_read_value(*e->field);
      break;
    case CSR_SIE:
      *val = cpu.mie & cpu.mideleg & SIE_MASK;
      break;
    case CSR_MIP:
    case CSR_SIP:
      cpu_sync_irq_inputs();
      *val = cpu.mip & (addr == CSR_MIP ? MIE_MASK : cpu.mideleg & SIE_MASK);
      break;
    default:
      *val = e->field ? *e->field & e->view_mask : 0;
      break;
  }
  if (rules) *val = csr_rules_read(rules, rule_count, *val);
  return true;
}

bool csr_read(word_t addr, word_t* val) {
  return csr_allowed(addr, false) && csr_peek(addr, val);
}

bool csr_write(word_t addr, word_t val) {
  if (!csr_allowed(addr, true)) return false;
  CsrEntry* e = csr_lookup(addr);
  if (riscv_pmp_csr(addr)) {
    riscv_pmp_write(addr, val);
    return true;
  }
  switch (addr) {
    case CSR_FFLAGS:
      cpu.fcsr = (cpu.fcsr & 0xe0) | (val & 31);
      riscv_fp_dirty();
      break;
    case CSR_FRM:
      cpu.fcsr = (cpu.fcsr & 31) | ((val & 7) << 5);
      riscv_fp_dirty();
      break;
    case CSR_FCSR:
      cpu.fcsr = val & 0xff;
      riscv_fp_dirty();
      break;
    case CSR_MCOUNTINHIBIT:
      break;
    case CSR_MISA: {
      cpu.misa = csr_rules_write(csr_misa_rules, ARRLEN(csr_misa_rules),
                                 cpu.misa, val);
      break;
    }
    case CSR_MSTATUS:
      cpu.mstatus = csr_rules_write(csr_mstatus_rules,
                                    ARRLEN(csr_mstatus_rules), cpu.mstatus,
                                    val);
      cpu.mstatus = csr_status_value(cpu.mstatus);
      break;
    case CSR_SSTATUS:
      cpu.mstatus = csr_rules_write(csr_sstatus_rules,
                                    ARRLEN(csr_sstatus_rules), cpu.mstatus,
                                    (cpu.mstatus & ~SSTATUS_WRITABLE_MASK) |
                                        (val & SSTATUS_WRITABLE_MASK));
      cpu.mstatus = csr_status_value(cpu.mstatus);
      break;
    case CSR_MTVEC:
    case CSR_STVEC:
      *e->field = csr_rules_write(csr_tvec_rules, ARRLEN(csr_tvec_rules),
                                  *e->field, val);
      break;
    case CSR_MEPC:
    case CSR_SEPC:
      *e->field = csr_rules_write(csr_epc_rules, ARRLEN(csr_epc_rules),
                                  *e->field, val);
      break;
    case CSR_MEDELEG:
      cpu.medeleg = csr_rules_write(csr_delegation_rules,
                                    ARRLEN(csr_delegation_rules), cpu.medeleg,
                                    val);
      break;
    case CSR_MIDELEG:
      cpu.mideleg = csr_rules_write(csr_mideleg_rules,
                                    ARRLEN(csr_mideleg_rules), cpu.mideleg,
                                    val);
      break;
    case CSR_MIE:
      cpu.mie = csr_rules_write(csr_mie_rules, ARRLEN(csr_mie_rules), cpu.mie,
                                val);
      break;
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
    case CSR_MCOUNTEREN:
    case CSR_SCOUNTEREN:
#ifdef CONFIG_RV_ZICNTR
      *e->field = csr_rules_write(csr_counteren_rules,
                                  ARRLEN(csr_counteren_rules), *e->field, val);
#else
      *e->field = 0; /* 没有用户级视图时，CY/TM/IR 硬连线零。 */
#endif
      break;
    case CSR_SATP:
      cpu.satp = csr_rules_write(csr_satp_rules, ARRLEN(csr_satp_rules),
                                 cpu.satp, val);
      break;
    default:
      if (e->field) {
        size_t rule_count = 0;
        const CsrFieldRule* rules = csr_rules_for(addr, &rule_count);
        *e->field = rules ? csr_rules_write(rules, rule_count, *e->field, val)
                          : val & e->view_mask;
      }
      break;
  }
  return true;
}

void csr_execute(Decode* s, int op, bool immediate) {
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
    word_t val = op == CSR_OP_RW   ? src
                 : op == CSR_OP_RS ? rmw_old | src
                                   : rmw_old & ~src;
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
