#include <common.h>
#include <device/timer/clint.h>
#include <util/log.h>

/*
 * CLINT：mtime / mtimecmp / msip。
 *
 * 关键设计：
 *   - mtime 由平台逻辑 tick 驱动（确定性时钟），见 clint_tick
 *   - 状态全在 ClintState 实例里，回调经 opaque 拿到它
 *   - 中断通过"输出线对象"发出，本文件不知道 MTIP/MSIP 这类 ISA 编号 ——
 *     接线由板子完成（machine.c: clint.mtimer_irq = cpu_irq_input(IRQ_MTIP)）
 */

#define CLINT_MSIP     0x0000
#define CLINT_MTIMECMP 0x4000
#define CLINT_MTIME    0xbff8
#define CLINT_SIZE     0xc000

#define CYCLES_PER_MTIME_TICK 128

word_t clint_time(void *opaque) {
  return ((ClintState *)opaque)->mtime;
}

static void clint_update_timer_irq(ClintState *s) {
  if (s->mtime >= s->mtimecmp) {
    irq_raise(s->mtimer_irq);
  } else {
    irq_lower(s->mtimer_irq);
  }
}

static word_t clint_read(void *opaque, paddr_t off, int len) {
  ClintState *s = opaque;
  (void)len; /* 骨架只支持 4/8 字节访问语义 */
  switch (off) {
    case CLINT_MSIP:     return s->msip;
    case CLINT_MTIMECMP: return s->mtimecmp;
    case CLINT_MTIME:    return s->mtime;
    default:             return 0;
  }
}

static void clint_write(void *opaque, paddr_t off, int len, word_t data) {
  ClintState *s = opaque;
  (void)len;
  switch (off) {
    case CLINT_MSIP:
      s->msip = (uint32_t)data & 1u;
      if (s->msip) {
        irq_raise(s->msip_irq);
      } else {
        irq_lower(s->msip_irq);
      }
      break;
    case CLINT_MTIMECMP:
      s->mtimecmp = data;
      clint_update_timer_irq(s);
      break;
    case CLINT_MTIME:
      s->mtime = data;
      clint_update_timer_irq(s);
      break;
    default:
      break;
  }
}

/* 设备节拍：每 CYCLES_PER_MTIME_TICK 个平台逻辑 tick推进一次 mtime */
static void clint_tick(void *opaque, uint64_t logical_ticks) {
  ClintState *s = opaque;
  if ((logical_ticks % CYCLES_PER_MTIME_TICK) != 0) return;
  s->mtime++;
  clint_update_timer_irq(s);
}

void clint_init(ClintState *s) {
  memset(s, 0, sizeof(*s));
  s->mmio = (MmioRegion){
      .name = "clint",
      .size = CLINT_SIZE,
      .read = clint_read,
      .write = clint_write,
      .opaque = s,
  };
  s->mtimecmp = ~0ull;
  device_register_tick(clint_tick, s);
}

void clint_reset(ClintState *s) {
  s->mtime = 0;
  s->mtimecmp = ~0ull;
  s->msip = 0;
  irq_lower(s->mtimer_irq);
  irq_lower(s->msip_irq);
}
