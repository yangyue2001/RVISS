#include <common.h>
#ifdef RVISS_TEST_MISALIGNED_NATIVE
#undef CONFIG_RV_MISALIGNED_TRAP
#define CONFIG_RV_MISALIGNED_NATIVE 1
#endif

#include <cpu/cpu.h>
#include <cpu/csr_list.h>
#include <device/device.h>
#include <memory/mmu.h>
#include <memory/paddr.h>
#include <memory/vaddr.h>

#include "pmp.h"
#include "test.h"

#define BASE UINT64_C(0x80000000)
#define RAM_SIZE (UINT64_C(1) << 20)
#define DATA (BASE + UINT64_C(0x8000))
#define TABLES (BASE + UINT64_C(0x20000))
#define OLD UINT64_C(0x1122334455667788)
#define VA_PAGE 5u

enum { PMP_ENTRY_BITS = 8 };

static word_t mmio_word;
static int mmio_writes;

static bool fake_read(void* ctx, paddr_t offset, int len, word_t* data) {
  (void)ctx;
  (void)offset;
  (void)len;
  *data = mmio_word;
  return true;
}

static bool fake_write(void* ctx, paddr_t offset, int len, word_t data) {
  (void)ctx;
  (void)offset;
  (void)len;
  mmio_word = data;
  mmio_writes++;
  return true;
}

static const MemObject fake_mmio = {
    .name = "fake-mmio",
    .size = RISCV_PAGE_SIZE,
    .read = fake_read,
    .write = fake_write,
};

static void setup(void) {
  static bool ready = false;
  if (!ready) {
    CHECK(mem_init(BASE, RAM_SIZE));
    ready = true;
  }
  bus_init(BASE, RAM_SIZE);
  memset(mem_host_ptr(BASE), 0, RAM_SIZE);
  memset(&cpu, 0, sizeof(cpu));
  cpu.priv = PRV_M;
  riscv_pmp_set_regions(0);
  mmio_word = 0;
  mmio_writes = 0;
}

static void expect_fault(MemResult result, Exception cause, vaddr_t tval) {
  CHECK(result.exception);
  CHECK_EQ(result.cause, cause);
  CHECK_EQ(result.tval, tval);
  CHECK_EQ(result.data, 0);
}

static void expect_read(vaddr_t addr, int len, word_t expected) {
  MemResult result = vaddr_read(addr, len);
  CHECK(!result.exception);
  CHECK_EQ(result.data, expected);
}

static void expect_fetch(vaddr_t addr, int len, word_t expected) {
  MemResult result = vaddr_ifetch(addr, len);
  CHECK(!result.exception);
  CHECK_EQ(result.data, expected);
}

static word_t pte_of(paddr_t addr, word_t flags) {
  return ((addr >> RISCV_PAGE_SHIFT) << RISCV_PTE_PPN_SHIFT) | flags;
}

static void put(paddr_t addr, word_t value) { CHECK(paddr_write(addr, 8, value)); }

static word_t get(paddr_t addr) {
  word_t value = 0;
  CHECK(paddr_try_read(addr, 8, &value, NULL));
  return value;
}

static vaddr_t va_low(unsigned page) {
  return (vaddr_t)page << RISCV_PAGE_SHIFT;
}

static vaddr_t va_high(int va_bits, unsigned page) {
  vaddr_t value = (UINT64_C(1) << (va_bits - 1)) | va_low(page);
  word_t sign = UINT64_C(1) << (va_bits - 1);
  word_t mask = (sign << 1) - 1;
  value &= mask;
  if ((value & sign) != 0) value |= ~mask;
  return value;
}

static paddr_t map_va(reg_t mode, int levels, vaddr_t va, paddr_t data, word_t flags) {
  paddr_t table = TABLES;
  paddr_t leaf = 0;
  cpu.satp = mode | (table >> RISCV_PAGE_SHIFT);
  cpu.priv = PRV_S;
  for (int level = levels - 1; level >= 0; level--) {
    unsigned shift = RISCV_PAGE_SHIFT + (unsigned)level * RISCV_VPN_BITS;
    unsigned index = (unsigned)((va >> shift) & ((UINT64_C(1) << RISCV_VPN_BITS) - 1));
    paddr_t slot = table + (paddr_t)index * RISCV_PTE_BYTES;
    if (level == 0) {
      put(slot, pte_of(data, flags));
      leaf = slot;
    } else {
      paddr_t next = TABLES + (paddr_t)(levels - level) * RISCV_PAGE_SIZE;
      put(slot, pte_of(next, PTE_V));
      table = next;
    }
  }
  return leaf;
}

static word_t pmp_cfg(unsigned mode, unsigned perm, bool lock) {
  word_t cfg = perm | ((word_t)mode << PMP_AMATCH_SHIFT);
  if (lock) cfg |= PMP_LOCK;
  return cfg;
}

static word_t pmp_napot(paddr_t base, unsigned size_shift) {
  unsigned ones = size_shift - 3;
  return (base >> 2) | ((UINT64_C(1) << ones) - 1);
}

static void test_request_not_modified(void) {
  setup();
  put(BASE + 32, UINT64_C(0x1020304050607080));
  MemRequest read_req = {.addr = BASE + 32,
                               .len = 8,
                               .type = kMemRead,
                               .data = UINT64_C(0xdeadbeef),
                               .privilege = PRV_M};
  MemResult read = vaddr_access(&read_req);
  CHECK(!read.exception);
  CHECK_EQ(read.data, UINT64_C(0x1020304050607080));
  CHECK_EQ(read_req.data, UINT64_C(0xdeadbeef));

  MemRequest write_req = {.addr = BASE + 32,
                                .len = 8,
                                .type = kMemWrite,
                                .data = OLD,
                                .privilege = PRV_M};
  MemResult write = vaddr_access(&write_req);
  CHECK(!write.exception);
  CHECK_EQ(write.data, 0);
  CHECK_EQ(write_req.data, OLD);
  CHECK_EQ(get(BASE + 32), OLD);

#ifdef CONFIG_RV_MISALIGNED_NATIVE
  put(BASE + 32, UINT64_C(0x1020304050607080));
  MemRequest split_req = {.addr = BASE + 33,
                                .len = 4,
                                .type = kMemRead,
                                .data = UINT64_C(0x1111),
                                .privilege = PRV_M};
  MemResult split = vaddr_access(&split_req);
  CHECK(!split.exception);
  CHECK_EQ(split.data, UINT64_C(0x40506070));
  CHECK_EQ(split_req.data, UINT64_C(0x1111));
#endif
}

static void test_bare_and_alignment(void) {
  setup();
  put(BASE + 64, UINT64_C(0x8877665544332211));
  expect_read(BASE + 64, 8, UINT64_C(0x8877665544332211));
  CHECK(!vaddr_write(BASE + 64, 8, OLD).exception);
  expect_read(BASE + 64, 8, OLD);

  expect_fault(vaddr_ifetch(BASE + 1, 2), EXCP_INST_ADDR_MIS, BASE + 1);
  put(BASE + 2, UINT64_C(0x1234));
  expect_fetch(BASE + 2, 2, UINT64_C(0x1234));

  expect_fault(vaddr_read(UINT64_C(1) << RISCV_PA_BITS, 8),
               EXCP_LOAD_ACCESS_FAULT, UINT64_C(1) << RISCV_PA_BITS);
  expect_fault(vaddr_read(BASE + RAM_SIZE, 8), EXCP_LOAD_ACCESS_FAULT,
               BASE + RAM_SIZE);

  cpu.priv = PRV_S;
  cpu.satp = SATP64_MODE_BARE;
  expect_read(BASE + 64, 8, OLD);

  put(BASE + 64, UINT64_C(0x8877665544332211));
#ifdef CONFIG_RV_MISALIGNED_NATIVE
  expect_read(BASE + 65, 4, UINT64_C(0x55443322));
  CHECK(!vaddr_write(BASE + 65, 4, UINT64_C(0xaabbccdd)).exception);
  CHECK_EQ(get(BASE + 64), UINT64_C(0x887766aabbccdd11));
  expect_fault(vaddr_read(BASE + RAM_SIZE - 1, 2), EXCP_LOAD_ACCESS_FAULT,
               BASE + RAM_SIZE);
#else
  word_t before = get(BASE + 64);
  expect_fault(vaddr_read(BASE + 65, 4), EXCP_LOAD_ADDR_MIS, BASE + 65);
  expect_fault(vaddr_write(BASE + 65, 4, UINT64_C(0xaabbccdd)),
               EXCP_STORE_AMO_ADDR_MIS, BASE + 65);
  CHECK_EQ(get(BASE + 64), before);
  expect_fault(vaddr_read(BASE + RAM_SIZE - 1, 2), EXCP_LOAD_ADDR_MIS,
               BASE + RAM_SIZE - 1);
#endif
}

static void test_translation(void) {
  setup();
  put(DATA, OLD);
  word_t flags = PTE_V | PTE_R;
  paddr_t slot = map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, va_low(VA_PAGE), DATA, flags);
  expect_fault(vaddr_read(va_low(VA_PAGE), 8), EXCP_LOAD_PAGE_FAULT, va_low(VA_PAGE));
  CHECK_EQ(get(slot), pte_of(DATA, flags));
  CHECK_EQ(get(DATA), OLD);
  flags = PTE_V | PTE_R | PTE_A;
  slot = map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, va_low(VA_PAGE), DATA, flags);
  expect_read(va_low(VA_PAGE), 8, OLD);
  CHECK_EQ(get(slot), pte_of(DATA, flags));

  setup();
  put(DATA, OLD);
  flags = PTE_V | PTE_R | PTE_W;
  slot = map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, va_low(VA_PAGE), DATA, flags);
  expect_fault(vaddr_write(va_low(VA_PAGE), 8, 42), EXCP_STORE_AMO_PAGE_FAULT,
               va_low(VA_PAGE));
  CHECK_EQ(get(DATA), OLD);
  CHECK_EQ(get(slot), pte_of(DATA, flags));
  flags = PTE_V | PTE_R | PTE_W | PTE_A;
  slot = map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, va_low(VA_PAGE), DATA, flags);
  expect_fault(vaddr_write(va_low(VA_PAGE), 8, 42), EXCP_STORE_AMO_PAGE_FAULT,
               va_low(VA_PAGE));
  CHECK_EQ(get(DATA), OLD);
  CHECK_EQ(get(slot), pte_of(DATA, flags));
  flags = PTE_V | PTE_R | PTE_W | PTE_A | PTE_D;
  slot = map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, va_low(VA_PAGE), DATA, flags);
  CHECK(!vaddr_write(va_low(VA_PAGE), 8, 42).exception);
  CHECK_EQ(get(DATA), 42);
  CHECK_EQ(get(slot), pte_of(DATA, flags));

  setup();
  put(DATA, OLD);
  slot = map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, va_low(VA_PAGE), DATA, PTE_V);
  expect_fault(vaddr_read(va_low(VA_PAGE), 8), EXCP_LOAD_PAGE_FAULT, va_low(VA_PAGE));
  CHECK_EQ(get(slot), pte_of(DATA, PTE_V));
  CHECK_EQ(get(DATA), OLD);

  setup();
  put(DATA, OLD);
  vaddr_t high = va_high(RISCV_SV39_VA_BITS, VA_PAGE);
  map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, high, DATA, PTE_V | PTE_R | PTE_A);
  expect_read(high, 8, OLD);
  expect_fault(vaddr_read(UINT64_C(1) << 38, 8), EXCP_LOAD_PAGE_FAULT,
               UINT64_C(1) << 38);

  setup();
  put(BASE, OLD);
  cpu.priv = PRV_S;
  cpu.satp = UINT64_C(1) << 60;
  expect_fault(vaddr_read(BASE, 8), EXCP_LOAD_PAGE_FAULT, BASE);

  setup();
  put(DATA, OLD);
  map_va(SATP64_MODE_SV48, RISCV_SV48_LEVELS, va_low(VA_PAGE), DATA, PTE_V | PTE_R | PTE_A);
  expect_read(va_low(VA_PAGE), 8, OLD);
  setup();
  put(DATA, OLD);
  map_va(SATP64_MODE_SV57, RISCV_SV57_LEVELS, va_low(VA_PAGE), DATA, PTE_V | PTE_R | PTE_A);
  expect_read(va_low(VA_PAGE), 8, OLD);

  setup();
  paddr_t root = TABLES;
  put(root, pte_of(TABLES + RISCV_PAGE_SIZE, PTE_V | PTE_A));
  cpu.satp = SATP64_MODE_SV39 | (root >> RISCV_PAGE_SHIFT);
  cpu.priv = PRV_S;
  expect_fault(vaddr_read(va_low(VA_PAGE), 8), EXCP_LOAD_PAGE_FAULT, va_low(VA_PAGE));

  setup();
  put(root, pte_of(BASE + RISCV_PAGE_SIZE, PTE_V | PTE_R | PTE_A));
  cpu.satp = SATP64_MODE_SV39 | (root >> RISCV_PAGE_SHIFT);
  cpu.priv = PRV_S;
  expect_fault(vaddr_read(va_low(VA_PAGE), 8), EXCP_LOAD_PAGE_FAULT, va_low(VA_PAGE));
  put(root, pte_of(BASE, PTE_V | PTE_R | PTE_A));
  put(BASE + va_low(VA_PAGE), OLD);
  expect_read(va_low(VA_PAGE), 8, OLD);
}

static void test_privilege(void) {
  setup();
  put(DATA, OLD);
  map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, va_low(VA_PAGE), DATA,
         PTE_V | PTE_R | PTE_A);
  expect_fault(vaddr_ifetch(va_low(VA_PAGE), 2), EXCP_INST_PAGE_FAULT,
               va_low(VA_PAGE));

  setup();
  put(DATA, OLD);
  map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, va_low(VA_PAGE), DATA,
         PTE_V | PTE_U | PTE_R | PTE_X | PTE_A);
  cpu.priv = PRV_U;
  expect_read(va_low(VA_PAGE), 8, OLD);
  expect_fetch(va_low(VA_PAGE), 2, (word_t)(uint16_t)OLD);
  cpu.priv = PRV_S;
  cpu.mstatus |= MSTATUS_SUM;
  expect_read(va_low(VA_PAGE), 8, OLD);
  expect_fault(vaddr_ifetch(va_low(VA_PAGE), 2), EXCP_INST_PAGE_FAULT,
               va_low(VA_PAGE));

  setup();
  put(DATA, OLD);
  map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, va_low(VA_PAGE), DATA,
         PTE_V | PTE_X | PTE_A);
  cpu.mstatus |= MSTATUS_MXR;
  expect_read(va_low(VA_PAGE), 8, OLD);
  expect_fault(vaddr_write(va_low(VA_PAGE), 8, 1), EXCP_STORE_AMO_PAGE_FAULT,
               va_low(VA_PAGE));

  setup();
  put(BASE, OLD);
  cpu.priv = PRV_M;
  cpu.mstatus = MSTATUS_MPRV | MSTATUS_MPP_S;
  cpu.satp = UINT64_C(1) << 60;
  expect_fault(vaddr_read(BASE, 8), EXCP_LOAD_PAGE_FAULT, BASE);
  expect_fetch(BASE, 2, (word_t)(uint16_t)OLD);
  cpu.mstatus = MSTATUS_MPRV | MSTATUS_MPP_M;
  expect_read(BASE, 8, OLD);

  setup();
  put(BASE, OLD);
  cpu.priv = PRV_S;
  cpu.satp = SATP64_MODE_SV39 | (UINT64_C(1) << 40);
#ifdef CONFIG_RV_MISALIGNED_NATIVE
  expect_fault(vaddr_read(BASE + 1, 4), EXCP_LOAD_ACCESS_FAULT, BASE + 1);
#else
  expect_fault(vaddr_read(BASE + 1, 4), EXCP_LOAD_ADDR_MIS, BASE + 1);
#endif
}

static void test_pmp_and_ptw(void) {
  setup();
  put(BASE, OLD);
  riscv_pmp_set_regions(CPU_PMP_ENTRIES);
  cpu.priv = PRV_S;
  expect_fault(vaddr_read(BASE, 8), EXCP_LOAD_ACCESS_FAULT, BASE);
  cpu.priv = PRV_M;
  expect_read(BASE, 8, OLD);

  setup();
  put(DATA, OLD);
  riscv_pmp_set_regions(CPU_PMP_ENTRIES);
  riscv_pmp_write(CSR_PMPADDR0, DATA >> 2);
  riscv_pmp_write(CSR_PMPCFG0, pmp_cfg(PMP_AMATCH_NA4, 0, false));
  CHECK(!riscv_pmp_check(DATA, 8, kMemRead, PRV_M));
  cpu.priv = PRV_M;
  expect_read(DATA, 4, (word_t)(uint32_t)OLD);
  cpu.priv = PRV_S;
  expect_fault(vaddr_read(DATA, 4), EXCP_LOAD_ACCESS_FAULT, DATA);
  riscv_pmp_write(CSR_PMPCFG0, PMP_WRITE);
  CHECK_EQ(cpu.pmpcfg[0] & (PMP_READ | PMP_WRITE), 0);

  setup();
  put(DATA, OLD);
  word_t flags = PTE_V | PTE_R | PTE_W | PTE_A | PTE_D;
  paddr_t slot = map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, va_low(VA_PAGE), DATA, flags);
  riscv_pmp_set_regions(CPU_PMP_ENTRIES);
  riscv_pmp_write(CSR_PMPADDR0, pmp_napot(DATA, RISCV_PAGE_SHIFT));
  riscv_pmp_write(CSR_PMPADDR0 + 1, pmp_napot(TABLES, 15));
  word_t deny = pmp_cfg(PMP_AMATCH_NAPOT, 0, true);
  word_t allow = pmp_cfg(PMP_AMATCH_NAPOT, PMP_READ | PMP_WRITE, true);
  riscv_pmp_write(CSR_PMPCFG0, deny | (allow << PMP_ENTRY_BITS));
  expect_fault(vaddr_write(va_low(VA_PAGE), 8, 42), EXCP_STORE_AMO_ACCESS_FAULT,
               va_low(VA_PAGE));
  CHECK_EQ(get(DATA), OLD);
  CHECK_EQ(get(slot), pte_of(DATA, flags));

  setup();
  put(DATA, OLD);
  slot = map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, va_low(VA_PAGE), DATA,
                PTE_V | PTE_R | PTE_A);
  word_t before = get(slot);
  riscv_pmp_set_regions(CPU_PMP_ENTRIES);
  riscv_pmp_write(CSR_PMPADDR0, pmp_napot(TABLES, 15));
  riscv_pmp_write(CSR_PMPCFG0, pmp_cfg(PMP_AMATCH_NAPOT, PMP_EXEC, true));
  expect_fault(vaddr_read(va_low(VA_PAGE), 8), EXCP_LOAD_ACCESS_FAULT,
               va_low(VA_PAGE));
  CHECK_EQ(get(slot), before);
  CHECK_EQ(get(DATA), OLD);
}

static void test_mmio_and_split_pmp(void) {
  setup();
  const paddr_t mmio_base = UINT64_C(0x10000000);
  CHECK(mmio_map(&fake_mmio, mmio_base));
  mmio_word = OLD;
  expect_read(mmio_base, 8, OLD);
  CHECK(!vaddr_write(mmio_base, 4, UINT64_C(0xabcd)).exception);
  CHECK_EQ(mmio_writes, 1);
  int writes = mmio_writes;
  MemResult split = vaddr_write(mmio_base + 1, 4, 1);
#ifdef CONFIG_RV_MISALIGNED_NATIVE
  CHECK(!split.exception);
  CHECK_EQ(split.data, 0);
  CHECK_EQ(mmio_writes, writes + 2);
  CHECK_EQ(mmio_word, 0);
#else
  CHECK(split.exception);
  CHECK_EQ(split.cause, EXCP_STORE_AMO_ADDR_MIS);
  CHECK_EQ(mmio_writes, writes);
#endif

  setup();
  put(BASE, 0);
  riscv_pmp_set_regions(CPU_PMP_ENTRIES);
  riscv_pmp_write(CSR_PMPADDR0, BASE >> 2);
  riscv_pmp_write(CSR_PMPCFG0, pmp_cfg(PMP_AMATCH_NA4, PMP_READ | PMP_WRITE, true));
  cpu.priv = PRV_S;
  MemResult result = vaddr_write(BASE + 3, 2, UINT64_C(0xffff));
  CHECK(result.exception);
#ifdef CONFIG_RV_MISALIGNED_NATIVE
  CHECK_EQ(result.cause, EXCP_STORE_AMO_ACCESS_FAULT);
  CHECK_EQ(result.tval, BASE + 4);
  CHECK_EQ(get(BASE), UINT64_C(0xff) << 24);
#else
  CHECK_EQ(get(BASE), 0);
  CHECK_EQ(result.cause, EXCP_STORE_AMO_ADDR_MIS);
  CHECK_EQ(result.tval, BASE + 3);
#endif
}

#ifdef CONFIG_RV_MISALIGNED_NATIVE
static void test_cross_page(void) {
  setup();
  paddr_t low_page = DATA;
  paddr_t high_page = DATA + RISCV_PAGE_SIZE;
  vaddr_t addr = va_low(1) + RISCV_PAGE_SIZE - 1;
  map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, va_low(1), low_page, PTE_V | PTE_R | PTE_W | PTE_A | PTE_D);
  map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, va_low(2), high_page, PTE_V | PTE_R | PTE_W | PTE_A | PTE_D);
  CHECK(paddr_write(low_page + RISCV_PAGE_SIZE - 1, 1, 0xab));
  CHECK(paddr_write(high_page, 1, 0xcd));
  expect_read(addr, 2, UINT64_C(0xcdab));
  CHECK(!vaddr_write(addr, 2, UINT64_C(0x2211)).exception);
  word_t low_byte = 0, high_byte = 0;
  CHECK(paddr_try_read(low_page + RISCV_PAGE_SIZE - 1, 1, &low_byte, NULL));
  CHECK(paddr_try_read(high_page, 1, &high_byte, NULL));
  CHECK_EQ(low_byte, 0x11);
  CHECK_EQ(high_byte, 0x22);

  setup();
  paddr_t slot = map_va(SATP64_MODE_SV39, RISCV_SV39_LEVELS, va_low(1), low_page,
                        PTE_V | PTE_R | PTE_W | PTE_A | PTE_D);
  CHECK(paddr_write(low_page + RISCV_PAGE_SIZE - 1, 1, 0xab));
  MemResult result = vaddr_write(addr, 2, UINT64_C(0x2211));
  expect_fault(result, EXCP_STORE_AMO_PAGE_FAULT, va_low(2));
  low_byte = 0;
  CHECK(paddr_try_read(low_page + RISCV_PAGE_SIZE - 1, 1, &low_byte, NULL));
  CHECK_EQ(low_byte, 0x11);
  CHECK_EQ(get(slot) & (PTE_A | PTE_D), PTE_A | PTE_D);
}
#endif

int main(void) {
  test_request_not_modified();
  test_bare_and_alignment();
  test_translation();
  test_privilege();
  test_pmp_and_ptw();
  test_mmio_and_split_pmp();
#ifdef CONFIG_RV_MISALIGNED_NATIVE
  test_cross_page();
#endif
  mem_shutdown();
  TEST_MAIN_END();
}
