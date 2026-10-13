#include <common.h>
#include <cpu/cpu.h>
#include <memory/paddr.h>
#include <memory/vaddr.h>

#include "mmu.h"
#include "pmp.h"
#include "test.h"

#define BASE UINT64_C(0x80000000)

static void setup(void) {
  CHECK(mem_init(BASE, 128));
  cpu_reset(BASE);
  riscv_pmp_set_regions(0);
  riscv_mmu_set_sv39(false);
  CHECK(paddr_write(BASE + 64, 8, UINT64_C(0x8877665544332211)));
}

int main(void) {
  setup();

#ifdef CONFIG_RV_MISALIGNED_TRAP
  MemAccess read = vaddr_read(BASE + 65, 4);
  CHECK_EQ(read.status, MEM_FAULT);
  CHECK_EQ(read.cause, EX_LAM);
  CHECK_EQ(read.tval, BASE + 65);

  MemAccess write = vaddr_write(BASE + 65, 4, UINT64_C(0xaabbccdd));
  CHECK_EQ(write.status, MEM_FAULT);
  CHECK_EQ(write.cause, EX_SAM);
  CHECK_EQ(paddr_read(BASE + 64, 8), UINT64_C(0x8877665544332211));
#else
  MemAccess read = vaddr_read(BASE + 65, 4);
  CHECK_EQ(read.status, MEM_OK);
  CHECK_EQ(read.data, UINT64_C(0x55443322));

  MemAccess write = vaddr_write(BASE + 65, 4, UINT64_C(0xaabbccdd));
  CHECK_EQ(write.status, MEM_OK);
  CHECK(write.write_performed);
  CHECK_EQ(paddr_read(BASE + 64, 8), UINT64_C(0x887766aabbccdd11));

  /* 跨 RAM 尾部时，逐字节预检查拒绝第二个字节，不能越界读取。 */
  CHECK_EQ(vaddr_read(BASE + 127, 2).status, MEM_FAULT);
#endif

  /* A 族的 ram_only 路径始终保持自然对齐要求。 */
  CHECK_EQ(isa_mem_prepare_atomic(BASE + 65, 4, true).status, MEM_FAULT);
  mem_shutdown();
  TEST_MAIN_END();
}
