#include <common.h>
#include <device/intc/plic.h>
#include <util/log.h>

/*
 * PLIC（最小实现）：单上下文（hart0 / M 模式）。
 * 寄存器：priority[source] / pending / enable / threshold / claim-complete。
 * TODO(M4)：多上下文、优先级仲裁、外部中断源、真实的 claim 语义；
 *           有中断挂起时应通过 s->meip_irq 拉高 CPU 的 MEIP 线。
 */

#define PLIC_SIZE 0x400000

#define PLIC_PRIORITY_BASE 0x000000
#define PLIC_PENDING_BASE  0x001000
#define PLIC_ENABLE_BASE   0x002000
#define PLIC_THRESHOLD     0x200000
#define PLIC_CLAIM         0x200004

static word_t plic_read(void *opaque, paddr_t off, int len) {
  PlicState *s = opaque;
  (void)len;
  if (off >= PLIC_PRIORITY_BASE && off < PLIC_PRIORITY_BASE + 4u * PLIC_NR_SOURCES) {
    return s->priority[(off - PLIC_PRIORITY_BASE) / 4];
  }
  if (off == PLIC_PENDING_BASE) return 0; /* 骨架：无外部中断源挂起 */
  if (off == PLIC_ENABLE_BASE)  return s->enable_bits;
  if (off == PLIC_THRESHOLD)    return s->threshold;
  if (off == PLIC_CLAIM)        return 0; /* 无中断可 claim */
  return 0;
}

static void plic_write(void *opaque, paddr_t off, int len, word_t data) {
  PlicState *s = opaque;
  (void)len;
  if (off >= PLIC_PRIORITY_BASE && off < PLIC_PRIORITY_BASE + 4u * PLIC_NR_SOURCES) {
    s->priority[(off - PLIC_PRIORITY_BASE) / 4] = (uint32_t)data;
    return;
  }
  if (off == PLIC_ENABLE_BASE)  { s->enable_bits = (uint32_t)data; return; }
  if (off == PLIC_THRESHOLD)    { s->threshold = (uint32_t)data; return; }
  if (off == PLIC_CLAIM)        { /* complete：骨架无状态可清 */ return; }
}

void plic_init(PlicState *s) {
  memset(s, 0, sizeof(*s));
  s->mmio = (MmioRegion){
      .name = "plic",
      .size = PLIC_SIZE,
      .read = plic_read,
      .write = plic_write,
      .opaque = s,
  };
}

void plic_reset(PlicState *s) {
  memset(s->priority, 0, sizeof(s->priority));
  s->enable_bits = 0;
  s->threshold = 0;
  irq_lower(s->meip_irq);
}
