#ifndef RVISS_DEVICE_MISC_TEST_FINISHER_H
#define RVISS_DEVICE_MISC_TEST_FINISHER_H

#include <device/device.h>

/*
 * 测试停机设备。test_finisher_init 只分配对象。
 * 板卡随后填写 finish。客人写出停机码时调用它。
 */

#define TEST_FINISHER_MMIO_SIZE UINT64_C(0x1000)

typedef struct TestFinisher {
  void (*finish)(void* opaque, int code);
  void* finish_opaque;
} TestFinisher;

MemObject test_finisher_init(void);

#endif /* RVISS_DEVICE_MISC_TEST_FINISHER_H */
