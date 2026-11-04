#include <common.h>
#include <cpu/cpu.h>
#include <cpu/step.h>
#include <device/device.h>
#include <memory/paddr.h>
#include <util/log.h>

#include "machine.h"

/*
 * 板卡 = CPU + 清单上的外设。
 * 外设在自己的 part 里登记。这里只做两轮：
 * 先按清单 map，再 connect。接线时所有实例都已经存在。
 * 先复位 CPU，再登记外设，避免外设复位拉高的中断被清掉。
 * 仿真器复位和镜像不在这里。
 */

static MachineConfig active;

static struct {
  const BoardPart* part;
  bool used;
} parts[MACHINE_PART_MAX];
static int nr_parts;

const MachineConfig* machine_get(void) { return &active; }

static bool part_named(const char* name) {
  for (int i = 0; i < nr_parts; i++) {
    if (strcmp(parts[i].part->name, name) == 0) return true;
  }
  return false;
}

void machine_add_part(const BoardPart* part) {
  Assert(part != NULL && part->name != NULL && part->map != NULL,
         "板卡部件不完整");
  Assert(!part_named(part->name), "设备 %s 重复注册", part->name);
  Assert(nr_parts < MACHINE_PART_MAX, "板卡部件超过上限 %d", MACHINE_PART_MAX);
  parts[nr_parts].part = part;
  parts[nr_parts].used = false;
  nr_parts++;
}

void machine_map(paddr_t base, paddr_t size, const MemObject* object) {
  Assert(object != NULL && object->ctx != NULL, "设备没有实例");
  Assert(mem_region_add(base, size, object), "设备映射失败");
}

void* machine_device(const char* name) {
  Assert(name != NULL, "设备名字为空");
  for (int i = 0; i < mem_region_count(); i++) {
    const MemRegion* region = mem_region_at(i);
    if (strcmp(region->object.name, name) == 0) return region->object.ctx;
  }
  return NULL;
}

static void machine_register(const MachineConfig* cfg) {
  Assert(cfg->nr_node >= 0 && cfg->nr_node <= MACHINE_NODE_MAX,
         "板卡设备数量非法");
  Assert(mem_init(cfg->ram_base, cfg->ram_size), "RAM 初始化失败");
  Assert(mem_install_ram(cfg->ram_base, cfg->ram_size), "RAM 登记失败");

  for (int i = 0; i < nr_parts; i++) parts[i].used = false;

  /* 按清单顺序映射，和链接顺序无关。 */
  for (int i = 0; i < cfg->nr_node; i++) {
    const char* name = cfg->node[i].name;
    Assert(name != NULL, "板卡设备没有名字");
    const BoardPart* part = NULL;
    for (int p = 0; p < nr_parts; p++) {
      if (strcmp(parts[p].part->name, name) != 0) continue;
      part = parts[p].part;
      parts[p].used = true;
      break;
    }
    Assert(part != NULL, "板卡设备 %s 没有注册", name);
    part->map(&cfg->node[i]);
  }

  for (int p = 0; p < nr_parts; p++) {
    if (!parts[p].used || parts[p].part->connect == NULL) continue;
    parts[p].part->connect();
  }
}

void machine_init(const MachineConfig* cfg) {
  active = *cfg;
  /* 再次 init 先清空旧区间，否则同一块 RAM 会和自己重叠。 */
  mem_region_reset();
  Log("machine: %s", active.name);
  cpu_reset(cfg->reset_pc);
  machine_register(&active);
}

void machine_advance(uint64_t logical_ticks) {
  for (int i = 0; i < mem_region_count(); i++) {
    const MemObject* object = &mem_region_at(i)->object;
    if (object->advance != NULL) object->advance(object->ctx, logical_ticks);
  }
  for (int i = 0; i < nr_parts; i++) {
    if (parts[i].used && parts[i].part->poll != NULL) parts[i].part->poll();
  }
}

void machine_update(StepResult* result) {
  for (int i = 0; i < mem_region_count(); i++) {
    const MemObject* object = &mem_region_at(i)->object;
    if (object->update != NULL) object->update(object->ctx);
  }
  riscv_step(result);
}
