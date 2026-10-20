#ifndef RVISS_DEVICE_MISC_TEST_FINISHER_H
#define RVISS_DEVICE_MISC_TEST_FINISHER_H

#include <device/device.h>

typedef struct TestFinisher TestFinisher;

TestFinisher* test_finisher_create(void (*finish)(void* opaque, int code),
                                   void* opaque);
void test_finisher_release(TestFinisher* finisher);
#define TEST_FINISHER_MMIO_SIZE UINT64_C(0x1000)

void test_finisher_mmio(TestFinisher* finisher, MemObject* mmio);

#endif /* RVISS_DEVICE_MISC_TEST_FINISHER_H */
