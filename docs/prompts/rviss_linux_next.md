# Codex 任务入口：RVISS Linux 与 Spike

先读AGENTS.md、docs/plans/linux_spike_goal.md与最新任务契约，再检查有关
源码、配置、测试和调用方。当前用户要求的Linux/Spike连续目标已complete，
T015已verified（2026-10-12），没有尚待执行的该目标验收项。

## 已完成的当前任务

- 最终GC O2：17 unit /25676 checks、3 smoke、CLI14通过，相关UBSan通过。
- 固定官方Spike完整矩阵66293 cases /132231 events通过，包含49152
  压缩编码、8192压缩FP编码；reset重放与4种故意差异捕获通过。
- 相同固定OpenSBI FW_PAYLOAD/Linux/BusyBox工件在RVISS进入PID1/BusyBox，
  真实UART和FP/原子/分页/COW/timer probe通过；三次冷启动各100轮和一次
  1000轮全部精确标记/正常关机/退出码0通过。完整输入重放另有T014证据。
- 本地Git已建立，origin=git@github.com:yangyue2001/RVISS.git；每个验证
  阶段已提交，尚未push。最终验收与完整身份见
  [T015](../plans/t015_linux_acceptance.md)及
  [报告](../validation/t015_completion.json)。

## 固定上下文与边界

Linux输入在docs/linux_boot_baseline.md / build/linux/artifacts/manifest.json，
来源/home/yang/study_dir/riscv_embedded，配置在configs/linux/。
规范/profile在docs/rv64_profile.md；固定Spike源码身份在验收报告。
实现参考为/home/yang/study_dir/rvbosc/NEMU和/home/yang/soft/qemu/qemu；
外部仓库agent规则不支配本项目。不要make clean删除build/linux工件；
使用BUILD_DIR隔离。核心保持C、Make+Kconfig、RV64解释器，C++限于Spike工具。

本次Spike矩阵未包含Linux全机逐步对拍。完整checkpoint恢复、NEMU/RTL
共享库ABI、多hart/H等仍未验收；不要凭本次成功把这些能力标为已支持。
后续工作按用户的新目标确定；开始新的跨模块实现时写契约，保留实际命令、
身份、首失败与修复证据，验证通过后提交。常规已授权本地修复无需逐步确认，
不得改输入/期望、扩大skip或执行中同步DUT状态到REF掩盖差异。
