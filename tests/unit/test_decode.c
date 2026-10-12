#include <common.h>
#include <cpu/cpu.h>
#include <cpu/decode.h>
#include <memory/paddr.h>

#include "test.h"

#include <string.h>

/*
 * 译码单元测试：把指令字写进内存，走真实的取指+译码路径，
 * 校验助记符、下一个 pc、操作数。
 */

static Decode decode_one(vaddr_t pc) {
  Decode s = {0};
  s.pc = pc;
  const bool ok = riscv_decode(&s);
  CHECK(ok);
  return s;
}

int main(void) {
  mem_init(0x80000000ull, 1 << 20);
  cpu_reset(0x80000000ull);
  riscv_decode_init();

  uint32_t *code = (uint32_t *)mem_host_ptr(0x80000000ull);
  code[0] = 0x00a00293; /* addi t0, x0, 10         */
  code[1] = 0x006285b3; /* add  a1, t0, t1         */
  code[2] = 0x00000073; /* ecall                   */
  code[3] = 0x0005006b; /* nemu_trap（rs1=a0）     */
  code[4] = 0x00228463; /* beq  t0, sp, +8         */
  code[5] = 0x0082b383; /* ld   t2, 8(t0)          */

  Decode s = decode_one(0x80000000ull);
  CHECK(strcmp(s.mnemonic, "addi") == 0);
  CHECK_EQ(s.ilen, 4);
  CHECK_EQ(s.snpc, 0x80000004ull);
  CHECK(s.dest.preg == &cpu.gpr[5]);
  CHECK_EQ(s.src1.preg, &cpu.gpr[0]);
  CHECK_EQ(s.src2.imm, 10);

  s = decode_one(0x80000004ull);
  CHECK(strcmp(s.mnemonic, "add") == 0);
  CHECK(s.dest.preg == &cpu.gpr[11]);
  CHECK(s.src1.preg == &cpu.gpr[5]);
  CHECK(s.src2.preg == &cpu.gpr[6]);

  s = decode_one(0x80000008ull);
  CHECK(strcmp(s.mnemonic, "ecall") == 0);

  s = decode_one(0x8000000cull);
  CHECK(strcmp(s.mnemonic, "nemu_trap") == 0);

  s = decode_one(0x80000010ull);
  CHECK(strcmp(s.mnemonic, "beq") == 0);
  CHECK_EQ(s.dest.imm, 8);

  s = decode_one(0x80000014ull);
  CHECK(strcmp(s.mnemonic, "ld") == 0);
  CHECK(s.dest.preg == &cpu.gpr[7]);
  CHECK_EQ(s.src2.imm, 8);

  /* 非法指令应译码失败 */
  code[6] = 0xffffffff;
  Decode bad = {0};
  bad.pc = 0x80000018ull;
  CHECK(!riscv_decode(&bad));

  mem_shutdown();
  TEST_MAIN_END();
}
