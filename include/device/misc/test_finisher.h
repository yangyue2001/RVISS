#ifndef RVISS_DEVICE_MISC_TEST_FINISHER_H
#define RVISS_DEVICE_MISC_TEST_FINISHER_H
#include <device/device.h>
typedef struct {
  MmioRegion mmio;
  void (*finish)(void *opaque, int code);
  void *finish_opaque;
} TestFinisherState;
void test_finisher_init(TestFinisherState *s);
#endif
