#include <device/intc/plic.h>

#define PLIC_MAX 8

struct Plic {
  IrqLine* external_irq[PLIC_NR_CONTEXTS];
  uint32_t priority[PLIC_NR_SOURCES];
  uint32_t enable_bits[PLIC_NR_CONTEXTS];
  uint32_t threshold[PLIC_NR_CONTEXTS];
  uint32_t levels;
  uint32_t pending;
  uint32_t busy;
};

static Plic pool[PLIC_MAX];
static bool used[PLIC_MAX];

static Plic* plic_alloc(void) {
  for (int i = 0; i < PLIC_MAX; i++) {
    if (!used[i]) {
      used[i] = true;
      memset(&pool[i], 0, sizeof(pool[i]));
      return &pool[i];
    }
  }
  return NULL;
}

static bool plic_owned(const Plic* plic) {
  return plic != NULL && plic >= pool && plic < pool + PLIC_MAX;
}

static unsigned select_irq(const Plic* plic, unsigned context) {
  unsigned best = 0;
  const uint32_t candidates = plic->pending & plic->enable_bits[context];
  for (unsigned id = 1; id < PLIC_NR_SOURCES; id++) {
    if ((candidates & (UINT32_C(1) << id)) &&
        plic->priority[id] > plic->priority[best])
      best = id;
  }
  return best;
}

static void plic_drive(Plic* plic) {
  for (unsigned context = 0; context < PLIC_NR_CONTEXTS; context++) {
    const unsigned id = select_irq(plic, context);
    if (id != 0 && plic->priority[id] > plic->threshold[context])
      irq_raise(plic->external_irq[context]);
    else
      irq_lower(plic->external_irq[context]);
  }
}

static bool plic_accepts(paddr_t offset, int len) {
  if (len != 4 || (offset & 3) != 0) return false;
  if (offset < 4 * PLIC_NR_SOURCES || offset == 0x1000) return true;
  for (unsigned context = 0; context < PLIC_NR_CONTEXTS; context++) {
    if (offset == 0x2000 + context * 0x80 ||
        offset == 0x200000 + context * 0x1000 ||
        offset == 0x200004 + context * 0x1000)
      return true;
  }
  return false;
}

static bool plic_read(void* ctx, paddr_t offset, int len, word_t* data) {
  Plic* plic = ctx;
  if (!plic_accepts(offset, len)) return false;
  if (offset < 4 * PLIC_NR_SOURCES) {
    *data = plic->priority[offset / 4];
    return true;
  }
  if (offset == 0x1000) {
    *data = plic->pending;
    return true;
  }
  for (unsigned context = 0; context < PLIC_NR_CONTEXTS; context++) {
    if (offset == 0x2000 + context * 0x80) {
      *data = plic->enable_bits[context];
      return true;
    }
    if (offset == 0x200000 + context * 0x1000) {
      *data = plic->threshold[context];
      return true;
    }
    if (offset == 0x200004 + context * 0x1000) {
      const unsigned id = select_irq(plic, context); /* claim 忽略 threshold */
      if (id != 0) plic->pending &= ~(UINT32_C(1) << id);
      plic_drive(plic);
      *data = id;
      return true;
    }
  }
  return false;
}

static bool plic_write(void* ctx, paddr_t offset, int len, word_t data) {
  Plic* plic = ctx;
  if (!plic_accepts(offset, len)) return false;
  if (offset < 4 * PLIC_NR_SOURCES) {
    if (offset != 0) plic->priority[offset / 4] = (uint32_t)data & 7u;
  }
  for (unsigned context = 0; context < PLIC_NR_CONTEXTS; context++) {
    if (offset == 0x2000 + context * 0x80)
      plic->enable_bits[context] = (uint32_t)data & UINT32_C(0xfffffffe);
    if (offset == 0x200000 + context * 0x1000)
      plic->threshold[context] = (uint32_t)data & 7u;
    if (offset == 0x200004 + context * 0x1000 && data > 0 &&
        data < PLIC_NR_SOURCES) {
      const uint32_t bit = UINT32_C(1) << data;
      if (plic->enable_bits[context] & bit) {
        plic->busy &= ~bit;
        if (plic->levels & bit) {
          plic->busy |= bit;
          plic->pending |= bit;
        }
      }
    }
  }
  plic_drive(plic);
  return true;
}

Plic* plic_create(IrqLine* machine_external, IrqLine* supervisor_external) {
  Plic* plic = plic_alloc();
  if (plic == NULL) return NULL;
  plic->external_irq[0] = machine_external;
  plic->external_irq[1] = supervisor_external;
  return plic;
}

void plic_release(Plic* plic) {
  if (!plic_owned(plic)) return;
  used[plic - pool] = false;
}

void plic_mmio(Plic* plic, MemObject* mmio) {
  *mmio = (MemObject){.name = "plic",
                      .ctx = plic,
                      .read = plic_read,
                      .write = plic_write};
}

void plic_reset(Plic* plic) {
  if (plic == NULL) return;
  memset(plic->priority, 0, sizeof(plic->priority));
  memset(plic->enable_bits, 0, sizeof(plic->enable_bits));
  memset(plic->threshold, 0, sizeof(plic->threshold));
  plic->levels = 0;
  plic->pending = 0;
  plic->busy = 0;
  plic_drive(plic);
}

void plic_update(Plic* plic) {
  if (plic != NULL) plic_drive(plic);
}

void plic_set_source(Plic* plic, unsigned source, bool level) {
  if (plic == NULL || source == 0 || source >= PLIC_NR_SOURCES) return;
  const uint32_t bit = UINT32_C(1) << source;
  if (level) {
    plic->levels |= bit;
    if ((plic->busy & bit) == 0) {
      plic->pending |= bit;
      plic->busy |= bit;
    }
  } else {
    plic->levels &= ~bit; /* 已经转发的请求不能撤回 */
  }
  plic_drive(plic);
}
