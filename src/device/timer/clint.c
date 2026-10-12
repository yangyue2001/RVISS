#include <device/timer/clint.h>
#define TICKS_PER_MTIME 128
word_t clint_time(void *opaque) { return ((ClintState *)opaque)->mtime; }

static void update(ClintState *s) {
  if (s->mtime >= s->mtimecmp) irq_raise(s->mtimer_irq); else irq_lower(s->mtimer_irq);
  if (s->msip) irq_raise(s->msip_irq); else irq_lower(s->msip_irq);
}
static bool accepts(void *opaque, paddr_t off, int len, bool write) {
  (void)opaque; (void)write;
  return (off == 0 && len == 4) ||
    ((off == 0x4000 || off == 0xbff8) && (len == 4 || len == 8)) ||
    ((off == 0x4004 || off == 0xbffc) && len == 4);
}
static word_t read_reg(void *opaque, paddr_t off, int len) {
  ClintState *s = opaque;
  if (!off) return s->msip;
  const uint64_t value = off < 0x8000 ? s->mtimecmp : s->mtime;
  return len == 8 ? value : (uint32_t)(value >> ((off & 4) ? 32 : 0));
}
static void write_reg(void *opaque, paddr_t off, int len, word_t data) {
  ClintState *s = opaque;
  if (!off) s->msip = data & 1;
  else {
    uint64_t *r = off < 0x8000 ? &s->mtimecmp : &s->mtime;
    if (len == 8) *r = data;
    else { unsigned shift = (off & 4) ? 32 : 0;
      *r = (*r & ~(UINT64_C(0xffffffff) << shift)) | ((uint64_t)(uint32_t)data << shift);
    }
  }
  update(s);
}
static void tick(void *opaque, uint64_t ticks) {
  ClintState *s = opaque;
  if (ticks >= s->last_ticks) s->mtime += ticks / TICKS_PER_MTIME - s->last_ticks / TICKS_PER_MTIME;
  s->last_ticks = ticks;
  update(s);
}
uint64_t clint_next_tick(const ClintState *s) {
  if (s->mtime >= s->mtimecmp || s->mtimecmp == UINT64_MAX) return UINT64_MAX;
  const uint64_t distance = s->mtimecmp - s->mtime;
  if (distance > (UINT64_MAX - s->last_ticks) / TICKS_PER_MTIME) return UINT64_MAX;
  return s->last_ticks + distance * TICKS_PER_MTIME - s->last_ticks % TICKS_PER_MTIME;
}
void clint_reset(ClintState *s) {
  s->last_ticks = s->mtime = s->msip = 0;
  s->mtimecmp = UINT64_MAX;
  update(s);
}
static uint64_t deadline(void *opaque) { return clint_next_tick(opaque); }
void clint_init(ClintState *s) {
  memset(s, 0, sizeof(*s));
  s->mmio = (MmioRegion){.name="clint", .size=0x10000, .accepts=accepts,
      .read=read_reg, .write=write_reg, .opaque=s};
  clint_reset(s);
  Assert(device_register_tick(tick, s), "CLINT tick registration failed");
  Assert(device_register_deadline(deadline, s), "CLINT deadline registration failed");
}
