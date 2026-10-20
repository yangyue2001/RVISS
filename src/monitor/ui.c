#include <common.h>
#include <host/host.h>
#include <cpu/cpu.h>
#include <memory/paddr.h>
#include <monitor/monitor.h>
#include <util/log.h>
#include <util/state.h>

#include "../engine/engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 交互调试器（骨架版）。
 * 命令：c(continue) / si [n](step) / info r / x ADDR / q(quit)
 * TODO(M8)：表达式求值、watchpoint、断点、反汇编。
 */

static const char *const abi_name[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0",   "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6",   "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6",
};

static void print_regs(void) {
  printf("pc = " FMT_WORD "  priv = %s  retired=%lu attempts=%lu events=%lu\n",
         cpu.pc, cpu_priv_name(cpu.priv), sim.instret, sim.attempts, sim.event_seq);
  for (int i = 0; i < 32; i++) {
    printf("x%-2d/%-4s = " FMT_WORD "%s", i, abi_name[i], (word_t)cpu.gpr[i],
           (i % 4 == 3) ? "\n" : "  ");
  }
}

static void cmd_x(const char *arg) {
  const paddr_t addr = (paddr_t)strtoull(arg, NULL, 0);
  for (int row = 0; row < 4; row++) {
    printf(FMT_PADDR ":", addr + (paddr_t)row * 16);
    for (int i = 0; i < 4; i++) {
      printf(" %08lx", (unsigned long)host_mem_read(addr + (paddr_t)row * 16 + (paddr_t)i * 4, 4));
    }
    printf("\n");
  }
}

void ui_mainloop(void) {
  char line[128];

  printf("rviss 交互调试器：c=继续 si=单步 info r=寄存器 x=查内存 q=退出\n");

  while (sim_running()) {
    printf("(rviss) ");
    fflush(stdout);

    if (fgets(line, sizeof(line), stdin) == NULL) break;
    line[strcspn(line, "\n")] = '\0';

    if (line[0] == '\0') continue;

    if (strcmp(line, "c") == 0) {
      engine_exec(UINT64_MAX);
    } else if (strncmp(line, "si", 2) == 0) {
      uint64_t n = 1;
      if (line[2] != '\0') n = strtoull(line + 3, NULL, 0);
      engine_exec(n); /* n 次指令尝试，含同步异常；纯中断独立记录 */
    } else if (strcmp(line, "info r") == 0) {
      print_regs();
    } else if (line[0] == 'x' && line[1] == ' ') {
      cmd_x(line + 2);
    } else if (line[0] == 'q') {
      sim.state = SIM_STOPPED;
    } else {
      printf("未知命令：%s\n", line);
    }
  }
}
