#ifndef RVISS_CHECKPOINT_CHECKPOINT_H
#define RVISS_CHECKPOINT_CHECKPOINT_H

#include <common.h>

/*
 * checkpoint：把"CPU 寄存器 + CSR + RAM（只存非零页）+ 设备状态"序列化到文件。
 * 文件格式细节见 docs/implementation_plan.md 的 M7。当前为骨架实现。
 */

bool cpt_save(const char *path);
bool cpt_load(const char *path);

#endif /* RVISS_CHECKPOINT_CHECKPOINT_H */
