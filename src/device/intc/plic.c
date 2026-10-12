#include <device/intc/plic.h>

static unsigned select_irq(const PlicState *s, unsigned context) {
  unsigned best = 0;
  const uint32_t candidates = s->pending & s->enable_bits[context];
  for (unsigned id = 1; id < PLIC_NR_SOURCES; id++)
    if ((candidates & (UINT32_C(1) << id)) && s->priority[id] > s->priority[best]) best = id;
  return best;
}

static void update(PlicState *s) {
  IrqLine *lines[] = {s->meip_irq, s->seip_irq};
  for (unsigned c = 0; c < PLIC_NR_CONTEXTS; c++) {
    const unsigned id = select_irq(s, c);
    if (id && s->priority[id] > s->threshold[c]) irq_raise(lines[c]);
    else irq_lower(lines[c]);
  }
}

void plic_set_source(PlicState *s, unsigned id, bool level) {
  if (!id || id >= PLIC_NR_SOURCES) return;
  const uint32_t bit = UINT32_C(1) << id;
  if (level) {
    s->levels |= bit;
    if (!(s->busy & bit)) { s->pending |= bit; s->busy |= bit; }
  } else s->levels &= ~bit; /* A forwarded request cannot be retracted. */
  update(s);
}

static bool accepts(void *opaque, paddr_t off, int len, bool write) {
  (void)opaque; (void)write;
  if (len != 4 || (off & 3)) return false;
  if (off < 4 * PLIC_NR_SOURCES || off == 0x1000) return true;
  for (unsigned c = 0; c < PLIC_NR_CONTEXTS; c++)
    if (off == 0x2000 + c * 0x80 || off == 0x200000 + c * 0x1000 ||
        off == 0x200004 + c * 0x1000) return true;
  return false;
}

static word_t read_reg(void *opaque, paddr_t off, int len) {
  PlicState *s = opaque; (void)len;
  if (off < 4 * PLIC_NR_SOURCES) return s->priority[off / 4];
  if (off == 0x1000) return s->pending;
  for (unsigned c = 0; c < PLIC_NR_CONTEXTS; c++) {
    if (off == 0x2000 + c * 0x80) return s->enable_bits[c];
    if (off == 0x200000 + c * 0x1000) return s->threshold[c];
    if (off == 0x200004 + c * 0x1000) {
      const unsigned id = select_irq(s, c); /* Claim ignores threshold. */
      if (id) s->pending &= ~(UINT32_C(1) << id);
      update(s);
      return id;
    }
  }
  return 0;
}

static void write_reg(void *opaque, paddr_t off, int len, word_t data) {
  PlicState *s = opaque; (void)len;
  if (off < 4 * PLIC_NR_SOURCES) { if (off) s->priority[off / 4] = data & 7; }
  for (unsigned c = 0; c < PLIC_NR_CONTEXTS; c++) {
    if (off == 0x2000 + c * 0x80) s->enable_bits[c] = data & UINT32_C(0xfffffffe);
    if (off == 0x200000 + c * 0x1000) s->threshold[c] = data & 7;
    if (off == 0x200004 + c * 0x1000 && data > 0 && data < PLIC_NR_SOURCES) {
      const uint32_t bit = UINT32_C(1) << data;
      /* Spec requires enabled ID, not the last claim ID or claiming context. */
      if (s->enable_bits[c] & bit) {
        s->busy &= ~bit;
        if (s->levels & bit) { s->busy |= bit; s->pending |= bit; }
      }
    }
  }
  update(s);
}

void plic_reset(PlicState *s) {
  memset(s->priority, 0, sizeof(s->priority));
  memset(s->enable_bits, 0, sizeof(s->enable_bits));
  memset(s->threshold, 0, sizeof(s->threshold));
  s->levels = s->pending = s->busy = 0;
  update(s);
}
void plic_init(PlicState *s) {
  memset(s, 0, sizeof(*s));
  s->mmio = (MmioRegion){.name="plic", .size=0x4000000, .accepts=accepts,
      .read=read_reg, .write=write_reg, .opaque=s};
}
