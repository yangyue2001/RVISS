#include <common.h>
#include <device/device.h>
#include <memory/paddr.h>

#include "test.h"

/*
 * 总线映射单元测试。
 * 重点验证"错误配置必须当场拒绝"：重叠、压住 RAM、非法大小，
 * 这些如果静默通过，会变成极难查的 bug（设备收不到访问）。
 */

static word_t dummy_read(void *opaque, paddr_t off, int len) {
  (void)opaque;
  (void)off;
  (void)len;
  return 0xdeadbeef;
}

static void dummy_write(void *opaque, paddr_t off, int len, word_t data) {
  (void)opaque;
  (void)off;
  (void)len;
  (void)data;
}

int main(void) {
  mem_init(0x80000000ull, 1 << 20);        /* RAM: 0x80000000 + 1MiB */
  bus_init(0x80000000ull, 1 << 20);

  MmioRegion a = {.name = "a", .size = 0x1000, .read = dummy_read, .write = dummy_write};
  MmioRegion b = {.name = "b", .size = 0x1000, .read = dummy_read, .write = dummy_write};
  MmioRegion zero = {.name = "zero", .size = 0, .read = dummy_read, .write = dummy_write};
  MmioRegion overlap_ram = {.name = "ram-ov", .size = 0x1000, .read = dummy_read, .write = dummy_write};

  /* 正常映射 */
  CHECK(mmio_map(&a, 0x10000000ull));

  /* 与 a 重叠（起始在 a 内部）→ 拒绝 */
  CHECK(!mmio_map(&b, 0x10000800ull));
  /* 与 a 重叠（跨 a 的尾部）→ 拒绝 */
  CHECK(!mmio_map(&b, 0x10000f00ull));
  /* 紧邻但不重叠 → 允许 */
  CHECK(mmio_map(&b, 0x10001000ull));

  /* 压在 RAM 上 → 拒绝（否则 RAM 会挡住设备）*/
  CHECK(!mmio_map(&overlap_ram, 0x80000000ull));
  /* 与 RAM 尾部相交（RAM 结尾 0x800FFFFF）→ 拒绝 */
  CHECK(!mmio_map(&overlap_ram, 0x800fffffull));
  /* 紧贴 RAM 之后 → 允许 */
  CHECK(mmio_map(&overlap_ram, 0x80100000ull));

  /* 大小非法 → 拒绝 */
  CHECK(!mmio_map(&zero, 0x20000000ull));

  /* 分发：命中已映射区域 */
  word_t v = 0;
  CHECK(mmio_dispatch_read(0x10000000ull, 4, &v));
  CHECK_EQ(v, 0xdeadbeefull);
  CHECK(mmio_dispatch_write(0x10001000ull, 4, 0x1234));
  /* 未映射地址 → false（由 memory 层决定按 0 处理还是上报） */
  CHECK(!mmio_dispatch_read(0x30000000ull, 4, &v));
  /* 跨区域边界 → false（一个访问必须完整落在某个区域内） */
  CHECK(!mmio_dispatch_read(0x10000ffcull, 8, &v));

  mem_shutdown();
  TEST_MAIN_END();
}
