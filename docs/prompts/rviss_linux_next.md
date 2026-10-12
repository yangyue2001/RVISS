# Codex 连续执行任务：RVISS Linux 与 Spike

先读 AGENTS.md、docs/plans/linux_spike_goal.md、最新 verified 任务与当前任务契约，
再检查相关源码、配置、测试和调用方。用户已授权一直完成到 RVISS 启动固定 Linux
及完成 Spike difftest；每个实际验证任务/阶段创建 Git 提交后继续下一项。
origin=git@github.com:yangyue2001/RVISS.git。局部 verified 不等于总目标完成。

当前 T001/T002/T003/T004 verified。T003 精确边界回归：7 unit / 729 checks、
3 smoke、CLI/UI 14 checks；T004 固定官方 Spike 29 cases / 343 events、重复
reset 执行和 3 种故意差异捕获通过。T002 仅是固定 Linux 工件的 QEMU 验收，
RVISS 还未启动 Linux。T005 M verified：13 指令、I/M Spike 346 cases / 4359 events、
UBSan、minimal/M 回归通过。T006 A verified：22 指令、I/M/A Spike 982 / 9172、
UBSan 与三配置回归通过。T007 C verified：全部 49152 个 16 位编码的 Spike
差分通过，总计 50215 / 59406，三配置回归与 UBSan 通过。
T008 CSR 检查点 verified：MAC/CSR 1741 / 11442、minimal/MAC 与 UBSan。
T008 同步委派/xRET verified：MAC/CSR/priv 2195 / 12505、回归与 UBSan。
T009 中断 verified：2652 / 13405、minimal/MAC 与 UBSan。
T010 counter verified：3249 / 15178、12 unit 回归与 UBSan。
T011 PMP verified：4378 / 16606、三配置与 UBSan。当前执行 T012 Sv39（缺契约时先补）。

总路线：M → A → C → CSR/委派/xRET/counter → PMP/Sv39 → F/D →
virt/UART/PLIC/CLINT/finisher → 固定 OpenSBI/Linux PID 1/BusyBox/probe/重复 workload。
每项先写可验收契约，再实现、实际执行独立检查、记录首失败与修复、提交并继续。
若当前任务已 verified，执行其记录的下一项；缺少契约时依据已验证缺口补写。
不得改变输入、修改期望、扩大 skip 或运行中同步 DUT 状态到 REF 掩盖差异。
C++ 限于独立 Spike 工具；核心保持 C、Make+Kconfig、RV64 解释器。

Linux 输入在 docs/linux_boot_baseline.md / build/linux/artifacts/manifest.json，
源码来源 /home/yang/study_dir/riscv_embedded，配置在 configs/linux/。
规范/profile 在 docs/rv64_profile.md。实现参考为 /home/yang/study_dir/rvbosc/NEMU
和 /home/yang/soft/qemu/qemu；外部仓库 agent 规则不支配本项目。
不要 make clean 删除 build/linux 工件；使用 BUILD_DIR 隔离。

更新任务状态、实际命令/结果/身份、未验证项和下一项，保留可恢复检查点。
必要接口更新与常规本地修复在授权范围内继续，无需逐步确认。
