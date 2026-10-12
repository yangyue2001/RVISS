#include <device/char/uart16550.h>

static unsigned trigger(const Uart16550State *s) {
  static const unsigned levels[] = {1, 4, 8, 14};
  return (s->fcr & 1) ? levels[s->fcr >> 6] : 1;
}
unsigned uart16550_capacity(const Uart16550State *s) {
  return ((s->fcr & 1) ? 16 : 1) - s->count;
}
static unsigned interrupt_id(const Uart16550State *s) {
  if ((s->ier & 4) && s->overrun) return 6;
  if ((s->ier & 1) && s->count >= trigger(s)) return 4;
  if ((s->ier & 1) && s->count && s->timeout) return 12;
  if ((s->ier & 2) && s->thre_pending) return 2;
  if ((s->ier & 8) && (s->msr & 15)) return 0;
  return 1;
}
static void update(Uart16550State *s) {
  if (interrupt_id(s) != 1) irq_raise(s->irq); else irq_lower(s->irq);
}
static void activity(Uart16550State *s) { s->rx_activity = s->now; s->timeout = false; }

uint64_t uart16550_next_tick(const Uart16550State *s) {
  if (!(s->fcr & 1) || !s->count || s->timeout) return UINT64_MAX;
  unsigned divisor = s->dll | ((unsigned)s->dlm << 8);
  if (!divisor) divisor = 1;
  /* Frame length in half bits: start + 5..8 data + optional parity + stops. */
  unsigned frame = 2 * (1 + 5 + (s->lcr & 3) + ((s->lcr & 8) != 0));
  frame += (s->lcr & 4) ? ((s->lcr & 3) ? 4 : 3) : 2;
  const uint64_t numerator = UINT64_C(10000000) * 128 * 4 * frame * 16 * divisor;
  const uint64_t delay = (numerator + 7372800 - 1) / 7372800;
  return s->rx_activity > UINT64_MAX - delay ? UINT64_MAX : s->rx_activity + delay;
}
static void tick(void *opaque, uint64_t now) {
  Uart16550State *s = opaque;
  s->now = now;
  if (!s->timeout && now >= uart16550_next_tick(s) && s->count && (s->fcr & 1)) {
    s->timeout = true; update(s);
  }
}
bool uart16550_receive(Uart16550State *s, uint8_t byte) {
  if (!uart16550_capacity(s)) {
    s->overrun = true;
    if (!(s->fcr & 1)) { s->fifo[s->head] = byte; activity(s); }
    update(s); return false;
  }
  s->fifo[(s->head + s->count) % 16] = byte; s->count++;
  activity(s); update(s); return true;
}
static bool accepts(void *opaque, paddr_t off, int len, bool write) {
  (void)opaque; (void)write;
  return off < 8 && len == 1;
}
static word_t read_reg(void *opaque, paddr_t off, int len) {
  Uart16550State *s = opaque; (void)len;
  switch (off) {
    case 0:
      if (s->lcr & 0x80) return s->dll;
      if (s->count) {
        uint8_t value = s->fifo[s->head]; s->head = (s->head + 1) % 16; s->count--;
        activity(s); update(s); return value;
      }
      return 0;
    case 1: return (s->lcr & 0x80) ? s->dlm : s->ier;
    case 2: {
      unsigned id = interrupt_id(s), value = id | ((s->fcr & 1) ? 0xc0 : 0);
      if (id == 2) { s->thre_pending = false; update(s); }
      return value;
    }
    case 3: return s->lcr;
    case 4: return s->mcr;
    case 5: {
      unsigned value = 0x60 | (s->count != 0) | (s->overrun ? 2 : 0);
      s->overrun = false; update(s); return value;
    }
    case 6: { uint8_t value = s->msr; s->msr &= 0xf0; update(s); return value; }
    case 7: return s->scr;
  }
  return 0;
}
static void write_reg(void *opaque, paddr_t off, int len, word_t data) {
  Uart16550State *s = opaque; uint8_t value = data; (void)len;
  switch (off) {
    case 0:
      if (s->lcr & 0x80) s->dll = value;
      else {
        /* Functional immediate TX: holding and shift register empty on return. */
        s->thre_pending = false;
        if (s->mcr & 0x10) uart16550_receive(s, value);
        else if (s->transmit) s->transmit(s->transmit_opaque, value);
        s->thre_pending = true;
      }
      break;
    case 1:
      if (s->lcr & 0x80) s->dlm = value;
      else {
        value &= 15;
        if ((value ^ s->ier) & 2) s->thre_pending = (value & 2) != 0;
        s->ier = value;
      }
      break;
    case 2:
      if (!(value & 1)) value = 0; /* Other FCR bits require FIFO enable. */
      if (((value ^ s->fcr) & 1) || (value & 2)) { s->count = s->head = 0; activity(s); }
      if (((value ^ s->fcr) & 1) || (value & 4)) s->thre_pending = true;
      s->fcr = value & 0xc9;
      break;
    case 3: s->lcr = value; break;
    case 4: {
      s->mcr = value & 31;
      uint8_t status = (s->mcr & 0x10) ?
        ((s->mcr & 2) << 3) | ((s->mcr & 1) << 5) |
        ((s->mcr & 4) << 4) | ((s->mcr & 8) << 4) : 0xb0;
      uint8_t changed = (status ^ s->msr) & 0xf0;
      uint8_t delta = (changed >> 4) & 0xb;
      if ((changed & 0x40) && !(status & 0x40)) delta |= 4;
      s->msr = status | (s->msr & 15) | delta;
      break;
    }
    case 7: s->scr = value; break;
  }
  update(s);
}
void uart16550_reset(Uart16550State *s) {
  s->dll = 1; s->dlm = s->ier = s->fcr = s->lcr = s->mcr = s->scr = 0;
  s->msr = 0xb0; s->head = s->count = 0;
  memset(s->fifo, 0, sizeof(s->fifo));
  s->overrun = s->thre_pending = s->timeout = false; s->now = s->rx_activity = 0;
  update(s);
}
static uint64_t deadline(void *opaque) { return uart16550_next_tick(opaque); }
void uart16550_init(Uart16550State *s) {
  memset(s, 0, sizeof(*s));
  s->mmio = (MmioRegion){.name="uart16550", .size=0x100, .accepts=accepts,
      .read=read_reg, .write=write_reg, .opaque=s};
  uart16550_reset(s);
  Assert(device_register_tick(tick, s), "UART tick registration failed");
  Assert(device_register_deadline(deadline, s), "UART deadline registration failed");
}
