#include <difftest.h>

#include <stdio.h>
#include <string.h>

/*
 * 直接链接 librviss_ref.so，不走内部 CPU 结构。
 * 程序是 auipc / addi / sw，然后检查 WFI 停住、注入中断和委托后的异常。
 */

#define EXPECT(cond)                                                         \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
      return 1;                                                              \
    }                                                                        \
  } while (0)

enum {
  ADDI_X1_7 = 0x00700093,
  AUIPC_X2_1 = 0x00001117,
  SW_X1_0_X2 = 0x00112023,
  WFI = 0x10500073
};

static void put_inst(uint8_t* dst, uint32_t inst) {
  dst[0] = (uint8_t)inst;
  dst[1] = (uint8_t)(inst >> 8);
  dst[2] = (uint8_t)(inst >> 16);
  dst[3] = (uint8_t)(inst >> 24);
}

static void load_state(RvissRefState* state) {
  memset(state, 0, sizeof(*state));
  difftest_regcpy(state, DIFFTEST_TO_DUT);
}

static void store_state(RvissRefState* state) {
  difftest_regcpy(state, DIFFTEST_TO_REF);
}

static int check_program(uint64_t base) {
  uint8_t image[12];
  uint8_t loaded[12];
  uint8_t word[4];
  RvissRefState state;

  put_inst(image + 0, AUIPC_X2_1);
  put_inst(image + 4, ADDI_X1_7);
  put_inst(image + 8, SW_X1_0_X2);
  difftest_memcpy(base, image, sizeof(image), DIFFTEST_TO_REF);
  memset(loaded, 0, sizeof(loaded));
  difftest_memcpy(base, loaded, sizeof(loaded), DIFFTEST_TO_DUT);
  EXPECT(memcmp(image, loaded, sizeof(image)) == 0);

  load_state(&state);
  state.pc = base;
  store_state(&state);
  difftest_exec(3);

  load_state(&state);
  EXPECT(state.gpr[0] == 0);
  EXPECT(state.gpr[1] == 7);
  EXPECT(state.gpr[2] == base + 0x1000);
  EXPECT(state.pc == base + 12);
  EXPECT(state.minstret == 3);
  EXPECT(state.priv == 3);
  memset(word, 0, sizeof(word));
  difftest_memcpy(base + 0x1000, word, sizeof(word), DIFFTEST_TO_DUT);
  EXPECT(word[0] == 7 && word[1] == 0 && word[2] == 0 && word[3] == 0);
  return 0;
}

static int check_wfi_and_intr(uint64_t base) {
  uint8_t slot[4];
  RvissRefState state;
  const uint64_t wfi_pc = base + 0x2000;
  const uint64_t mtvec = base + 0x3000;
  const uint64_t mie = 1ull << 3;
  const uint64_t mpie = 1ull << 7;
  const uint64_t mpp_m = 3ull << 11;

  put_inst(slot, WFI);
  difftest_memcpy(wfi_pc, slot, sizeof(slot), DIFFTEST_TO_REF);
  load_state(&state);
  state.pc = wfi_pc;
  store_state(&state);
  difftest_exec(1);
  load_state(&state);
  EXPECT(state.pc == wfi_pc + 4);
  EXPECT(state.wfi == 1);
  EXPECT(state.minstret == 4);
  difftest_exec(1);
  load_state(&state);
  EXPECT(state.pc == wfi_pc + 4);
  EXPECT(state.wfi == 1);
  EXPECT(state.minstret == 4);

  state.mtvec = mtvec;
  state.mstatus |= mie;
  store_state(&state);
  difftest_raise_trap((1ull << 63) | 7, 0);
  load_state(&state);
  EXPECT(state.wfi == 0);
  EXPECT(state.pc == mtvec);
  EXPECT(state.mepc == wfi_pc + 4);
  EXPECT(state.mcause == ((1ull << 63) | 7));
  EXPECT(state.mtval == 0);
  EXPECT(state.priv == 3);
  EXPECT((state.mstatus & mie) == 0);
  EXPECT((state.mstatus & mpie) != 0);
  EXPECT((state.mstatus & mpp_m) == mpp_m);
  EXPECT(state.minstret == 4);
  return 0;
}

static int check_delegated_excp(uint64_t base) {
  RvissRefState state;
  const uint64_t pc = base + 0x40;
  const uint64_t stvec = base + 0x4000;

  load_state(&state);
  state.pc = pc;
  state.priv = 0;
  state.medeleg = 1ull << 8;
  state.stvec = stvec;
  state.mstatus &= ~(1ull << 1);
  store_state(&state);
  difftest_raise_trap(8, 0x111);
  load_state(&state);
  EXPECT(state.priv == 1);
  EXPECT(state.pc == stvec);
  EXPECT(state.sepc == pc);
  EXPECT(state.scause == 8);
  EXPECT(state.stval == 0x111);
  EXPECT((state.mstatus & (1ull << 1)) == 0);
  EXPECT((state.mstatus & (1ull << 8)) == 0);
  return 0;
}

static int check_mirror(uint64_t base) {
  RvissRefState state;
  const uint64_t fs_dirty = 3ull << 13;
  const uint64_t sd = 1ull << 63;

  load_state(&state);
  state.gpr[0] = 0x99;
  state.gpr[3] = 0x1234;
  state.fpr[1] = 0xabcdef;
  state.fcsr = 0x161;
  state.satp = (8ull << 60) | 0x11;
  state.pmpcfg[0] = 0x9f;
  state.pmpaddr[0] = (1ull << 60) | 0x1fff;
  state.lr_valid = 1;
  state.lr_addr = base + 32;
  state.lr_len = 8;
  state.mstatus |= fs_dirty;
  store_state(&state);
  memset(&state, 0, sizeof(state));
  load_state(&state);
  EXPECT(state.gpr[0] == 0);
  EXPECT(state.gpr[3] == 0x1234);
  EXPECT(state.fpr[1] == 0xabcdef);
  EXPECT(state.fcsr == 0x61);
  EXPECT(state.satp == ((8ull << 60) | 0x11));
  EXPECT(state.pmpcfg[0] == 0x9f);
  EXPECT(state.pmpaddr[0] == 0x1fff);
  EXPECT(state.lr_valid == 1);
  EXPECT(state.lr_addr == base + 32);
  EXPECT(state.lr_len == 8);
  EXPECT((state.mstatus & sd) != 0);

  state.lr_valid = 0;
  store_state(&state);
  load_state(&state);
  EXPECT(state.lr_valid == 0);
  EXPECT(state.lr_addr == 0);
  EXPECT(state.lr_len == 0);
  return 0;
}

int main(void) {
  uint64_t base;
  uint8_t byte = 0;

  EXPECT(difftest_state_size() == sizeof(RvissRefState));
  difftest_init();
  base = difftest_ram_base();
  EXPECT(difftest_ram_size() > 0x5000);
  EXPECT(base == 0x80000000);

  if (check_program(base) != 0) return 1;
  if (check_wfi_and_intr(base) != 0) return 1;
  if (check_delegated_excp(base) != 0) return 1;
  if (check_mirror(base) != 0) return 1;

  difftest_init();
  difftest_memcpy(base, &byte, 1, DIFFTEST_TO_DUT);
  EXPECT(byte == 0);
  difftest_memcpy(base, NULL, 0, DIFFTEST_TO_REF);
  printf("ref api ok\n");
  return 0;
}
