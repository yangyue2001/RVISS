#include <device/misc/test_finisher.h>
static bool accepts(void *opaque, paddr_t offset, int length, bool write) {
  (void)opaque; (void)write;
  return offset == 0 && (length == 2 || length == 4);
}
static word_t read_reg(void *opaque, paddr_t offset, int length) {
  (void)opaque; (void)offset; (void)length; return 0;
}
static void write_reg(void *opaque, paddr_t offset, int length, word_t data) {
  TestFinisherState *s = opaque; (void)offset; (void)length;
  unsigned status = data & 0xffff, code = (data >> 16) & 0xffff;
  if (s->finish && (status == 0x5555 || status == 0x3333))
    s->finish(s->finish_opaque, status == 0x5555 ? code : (code ? code : 1));
}
void test_finisher_init(TestFinisherState *s) {
  memset(s, 0, sizeof(*s));
  s->mmio = (MmioRegion){.name="test-finisher", .size=0x1000, .accepts=accepts,
      .read=read_reg, .write=write_reg, .opaque=s};
}
