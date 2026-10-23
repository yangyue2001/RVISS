#include <device/timer/clint.h>

#define TICKS_PER_MTIME 128

static Clint clint;
static bool clint_used;

static Clint* clint_alloc(void) {
  if (clint_used) return NULL;
  clint_used = true;
  memset(&clint, 0, sizeof(clint));
  return &clint;
}

static void clint_drive(Clint* dev) {
  for (int hart = 0; hart < CLINT_NHART; hart++) {
    if (dev->mtime >= dev->mtimecmp[hart])
      irq_raise(dev->mtimer_irq[hart]);
    else
      irq_lower(dev->mtimer_irq[hart]);
    if (dev->msip[hart] & 1u)
      irq_raise(dev->msip_irq[hart]);
    else
      irq_lower(dev->msip_irq[hart]);
  }
}

static int msip_hart(paddr_t offset, int len) {
  if (len != 4 || (offset & 3) != 0 || offset >= 0x4000) return -1;
  unsigned hart = (unsigned)(offset / 4);
  return hart < CLINT_NHART ? (int)hart : -1;
}

static int mtimecmp_hart(paddr_t offset, int len, unsigned* shift) {
  if (offset < 0x4000 || offset >= 0x4000 + (paddr_t)CLINT_NHART * 8) return -1;
  paddr_t rel = offset - 0x4000;
  unsigned hart = (unsigned)(rel / 8);
  unsigned byte = (unsigned)(rel % 8);
  if (len == 8 && byte == 0) {
    *shift = 0;
    return (int)hart;
  }
  if (len == 4 && (byte == 0 || byte == 4)) {
    *shift = byte * 8;
    return (int)hart;
  }
  return -1;
}

static bool mtime_access(paddr_t offset, int len) {
  return (offset == 0xbff8 && (len == 4 || len == 8)) ||
         (offset == 0xbffc && len == 4);
}

static uint64_t read_reg(uint64_t value, int len, unsigned shift) {
  return len == 8 ? value : (uint32_t)(value >> shift);
}

static void write_reg(uint64_t* reg, int len, unsigned shift, word_t data) {
  if (len == 8) {
    *reg = data;
    return;
  }
  *reg = (*reg & ~(UINT64_C(0xffffffff) << shift)) |
         ((uint64_t)(uint32_t)data << shift);
}

static bool clint_read(void* ctx, paddr_t offset, int len, word_t* data) {
  Clint* dev = ctx;
  unsigned shift = 0;
  int hart = msip_hart(offset, len);
  if (hart >= 0) {
    *data = dev->msip[hart];
    return true;
  }
  hart = mtimecmp_hart(offset, len, &shift);
  if (hart >= 0) {
    *data = read_reg(dev->mtimecmp[hart], len, shift);
    return true;
  }
  if (!mtime_access(offset, len)) return false;
  *data = read_reg(dev->mtime, len, offset == 0xbffc ? 32 : 0);
  return true;
}

static bool clint_write(void* ctx, paddr_t offset, int len, word_t data) {
  Clint* dev = ctx;
  unsigned shift = 0;
  int hart = msip_hart(offset, len);
  if (hart >= 0) {
    dev->msip[hart] = (uint32_t)data & 1u;
  } else {
    hart = mtimecmp_hart(offset, len, &shift);
    if (hart >= 0) {
      write_reg(&dev->mtimecmp[hart], len, shift, data);
    } else if (mtime_access(offset, len)) {
      write_reg(&dev->mtime, len, offset == 0xbffc ? 32 : 0, data);
    } else {
      return false;
    }
  }
  clint_drive(dev);
  return true;
}

static void clint_reset(void* ctx) {
  Clint* dev = ctx;
  Assert(dev != NULL, "CLINT 为空");
  dev->last_ticks = 0;
  dev->mtime = 0;
  for (int hart = 0; hart < CLINT_NHART; hart++) {
    dev->msip[hart] = 0;
    dev->mtimecmp[hart] = UINT64_MAX;
  }
  clint_drive(dev);
}

static void clint_update(void* ctx) {
  Assert(ctx != NULL, "CLINT 为空");
  clint_drive(ctx);
}

static void clint_advance(void* ctx, uint64_t logical_ticks) {
  Clint* dev = ctx;
  Assert(dev != NULL, "CLINT 为空");
  if (logical_ticks >= dev->last_ticks) {
    dev->mtime +=
        logical_ticks / TICKS_PER_MTIME - dev->last_ticks / TICKS_PER_MTIME;
  }
  dev->last_ticks = logical_ticks;
  clint_drive(dev);
}

MemObject clint_init(void) {
  Clint* dev = clint_alloc();
  Assert(dev != NULL, "CLINT 分配失败");
  clint_reset(dev);
  return (MemObject){
      .name = "clint",
      .ctx = dev,
      .read = clint_read,
      .write = clint_write,
      .update = clint_update,
      .advance = clint_advance,
  };
}

uint64_t clint_mtime_now(void) { return clint.mtime; }
