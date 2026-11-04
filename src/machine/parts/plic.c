#include <cpu/interrupt.h>
#include <device/intc/plic.h>
#include <trap.h>

#include "../machine.h"

/*
 * PLIC 的板级部分。map 只映射；connect 再接到 CPU 的机器/监管者外部中断。
 */

static Plic* board_plic;

static void plic_map(const MachineNode* node) {
  Assert(board_plic == NULL, "PLIC 只能有一个");
  MemObject object = plic_init();
  board_plic = object.ctx;
  machine_map(node->base, PLIC_MMIO_SIZE, &object);
}

static void plic_connect(void) {
  Assert(board_plic != NULL, "PLIC 没有实例");
  board_plic->external_irq[0] = cpu_irq_input(INT_MEI);
  board_plic->external_irq[1] = cpu_irq_input(INT_SEI);
}

static void plic_register(void) __attribute__((constructor));
static void plic_register(void) {
  static const BoardPart part = {
      .name = "plic",
      .map = plic_map,
      .connect = plic_connect,
  };
  machine_add_part(&part);
}
