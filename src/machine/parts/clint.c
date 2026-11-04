#include <cpu/interrupt.h>
#include <device/timer/clint.h>
#include <trap.h>

#include "../machine.h"

/*
 * CLINT 的板级部分。map 只映射；connect 再接到 CPU 的机器定时器和软件中断。
 */

static Clint* board_clint;

static void clint_map(const MachineNode* node) {
  Assert(board_clint == NULL, "CLINT 只能有一个");
  MemObject object = clint_init();
  board_clint = object.ctx;
  machine_map(node->base, CLINT_MMIO_SIZE, &object);
}

static void clint_connect(void) {
  Assert(board_clint != NULL, "CLINT 没有实例");
  board_clint->mtimer_irq[0] = cpu_irq_input(INT_MTI);
  board_clint->msip_irq[0] = cpu_irq_input(INT_MSI);
}

static void clint_register(void) __attribute__((constructor));
static void clint_register(void) {
  static const BoardPart part = {
      .name = "clint",
      .map = clint_map,
      .connect = clint_connect,
  };
  machine_add_part(&part);
}
