#include <cpu/cpu.h>
#include <device/misc/test_finisher.h>
#include <util/state.h>

#include "../machine.h"

/*
 * 测试停机设备的板级部分。map 只映射；connect 再接停机回调。
 */

static TestFinisher* board_finisher;

static void finish_guest(void* opaque, int code) {
  (void)opaque;
  sim_halt(cpu.pc, code);
}

static void finisher_map(const MachineNode* node) {
  Assert(board_finisher == NULL, "test finisher 只能有一个");
  MemObject object = test_finisher_init();
  board_finisher = object.ctx;
  machine_map(node->base, TEST_FINISHER_MMIO_SIZE, &object);
}

static void finisher_connect(void) {
  Assert(board_finisher != NULL, "test finisher 没有实例");
  board_finisher->finish = finish_guest;
  board_finisher->finish_opaque = NULL;
}

static void finisher_register(void) __attribute__((constructor));
static void finisher_register(void) {
  static const BoardPart part = {
      .name = "test-finisher",
      .map = finisher_map,
      .connect = finisher_connect,
  };
  machine_add_part(&part);
}
