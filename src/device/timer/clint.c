#include <device/timer/clint.h>

#define TICKS_PER_MTIME 128
#define CLINT_MAX 8

struct Clint {
  IrqLine* mtimer_irq;
  IrqLine* msip_irq;
  uint64_t last_ticks;
  uint64_t mtime;
  uint64_t mtimecmp;
  uint32_t msip;
};

static Clint pool[CLINT_MAX];
static bool used[CLINT_MAX];

static Clint* clint_alloc(void) {
  for (int i = 0; i < CLINT_MAX; i++) {
    if (!used[i]) {
      used[i] = true;
      memset(&pool[i], 0, sizeof(pool[i]));
      return &pool[i];
    }
  }
  return NULL;
}

static bool clint_owned(const Clint* clint) {
  return clint != NULL && clint >= pool && clint < pool + CLINT_MAX;
}

static void clint_drive(Clint* clint) {
  if (clint->mtime >= clint->mtimecmp)
    irq_raise(clint->mtimer_irq);
  else
    irq_lower(clint->mtimer_irq);
  if (clint->msip)
    irq_raise(clint->msip_irq);
  else
    irq_lower(clint->msip_irq);
}

static bool clint_accepts(paddr_t offset, int len) {
  return (offset == 0 && len == 4) ||
         ((offset == 0x4000 || offset == 0xbff8) && (len == 4 || len == 8)) ||
         ((offset == 0x4004 || offset == 0xbffc) && len == 4);
}

static bool clint_read(void* ctx, paddr_t offset, int len, word_t* data) {
  Clint* clint = ctx;
  if (!clint_accepts(offset, len)) return false;
  if (offset == 0) {
    *data = clint->msip;
    return true;
  }
  const uint64_t value = offset < 0x8000 ? clint->mtimecmp : clint->mtime;
  *data = len == 8 ? value : (uint32_t)(value >> ((offset & 4) ? 32 : 0));
  return true;
}

static bool clint_write(void* ctx, paddr_t offset, int len, word_t data) {
  Clint* clint = ctx;
  if (!clint_accepts(offset, len)) return false;
  if (offset == 0) {
    clint->msip = (uint32_t)data & 1u;
  } else {
    uint64_t* reg = offset < 0x8000 ? &clint->mtimecmp : &clint->mtime;
    if (len == 8) {
      *reg = data;
    } else {
      unsigned shift = (offset & 4) ? 32 : 0;
      *reg = (*reg & ~(UINT64_C(0xffffffff) << shift)) |
             ((uint64_t)(uint32_t)data << shift);
    }
  }
  clint_drive(clint);
  return true;
}

Clint* clint_create(IrqLine* timer_irq, IrqLine* software_irq) {
  Clint* clint = clint_alloc();
  if (clint == NULL) return NULL;
  clint->mtimer_irq = timer_irq;
  clint->msip_irq = software_irq;
  clint->mtimecmp = UINT64_MAX;
  return clint;
}

void clint_release(Clint* clint) {
  if (!clint_owned(clint)) return;
  used[clint - pool] = false;
}

void clint_mmio(Clint* clint, MemObject* mmio) {
  *mmio = (MemObject){.name = "clint",
                      .ctx = clint,
                      .read = clint_read,
                      .write = clint_write};
}

void clint_reset(Clint* clint) {
  if (clint == NULL) return;
  clint->last_ticks = 0;
  clint->mtime = 0;
  clint->msip = 0;
  clint->mtimecmp = UINT64_MAX;
  clint_drive(clint);
}

void clint_update(Clint* clint) {
  if (clint != NULL) clint_drive(clint);
}

void clint_advance(Clint* clint, uint64_t logical_ticks) {
  if (clint == NULL) return;
  if (logical_ticks >= clint->last_ticks) {
    clint->mtime += logical_ticks / TICKS_PER_MTIME -
                    clint->last_ticks / TICKS_PER_MTIME;
  }
  clint->last_ticks = logical_ticks;
  clint_drive(clint);
}

uint64_t clint_deadline(const Clint* clint) {
  if (clint == NULL || clint->mtime >= clint->mtimecmp ||
      clint->mtimecmp == UINT64_MAX)
    return UINT64_MAX;
  const uint64_t distance = clint->mtimecmp - clint->mtime;
  if (distance > (UINT64_MAX - clint->last_ticks) / TICKS_PER_MTIME)
    return UINT64_MAX;
  return clint->last_ticks + distance * TICKS_PER_MTIME -
         clint->last_ticks % TICKS_PER_MTIME;
}

uint64_t clint_mtime(const Clint* clint) {
  return clint == NULL ? 0 : clint->mtime;
}
