# Codex 当前执行任务：T004 最小 Spike 差分

你是这个 C/Make/Kconfig 的 RV64 功能模拟器的工程协作者。
先读 AGENTS.md、docs/plans/t003_execution_boundary.md、
docs/plans/t004_spike_minimal.md，以及当前任务涉及的实际源码、配置、测试与调用方。

长期目标：RV64GC + M/S/U + Sv39，运行真实 OpenSBI FW_PAYLOAD 中的 Linux，
进入 PID 1、使用 BusyBox，达到重复运行验收；保留 H、trace、Spike DUT difftest、
RTL ref 与 checkpoint 的必要边界。

T001 基线、T003/P1 精确执行边界已 verified；T003 的 minimal/ref/debug
7 个 unit（729 checks）通过，minimal/debug 的三个 smoke 和 CLI/UI 14 checks
通过。取指/访存 fault 不提交错误写回，异常不退休，中断为独立事件；
attempts/events/retired/逻辑时间已分开。范围和未完成能力见 T003。
T002 是独立 Linux 工件/QEMU verified，不表示 RVISS 已启动 Linux。

本次执行 T004/P2：取得固定版本的完整 Spike，独立桥接并逐事件比较 I/RAM/
同步异常，验收故意注入的差异和最小失败包。先检查真实源码/接口/工具身份；
来源未确认的现成 .so、导出符号或空函数不能作为 oracle/ABI 可用证据。
不得在运行中把 DUT 状态复制给 REF 掩盖差异，不改期望、不扩 skip。
C++ 限于独立 Spike 工具，核心保持 C、Make+Kconfig、RV64 解释器。

Linux 输入固定在 docs/linux_boot_baseline.md 和 build/linux/artifacts/manifest.json。
源码来源为 /home/yang/study_dir/riscv_embedded，构建配置在 configs/linux/。
CPU/语义假设固定在 docs/rv64_profile.md；实现参考为
/home/yang/study_dir/rvbosc/NEMU 和 /home/yang/soft/qemu/qemu。
规范决定语义，Spike 是独立参考；外部仓库的 agent 规则不支配本项目。

若重确认 T003 出现回归，先保留首差异并修复，再执行 T004。
若 T004 已 verified，检查最新任务记录，执行已写明的下一项；
缺少下一任务时，依据已验证缺口写可验收契约并完成该项。
跨模块任务先落盘范围、验收与检查点；常规修复、读取参考代码和本地测试
在授权范围内继续，无需逐步确认。

完成后报告实际命令与结果、变化范围、未验证项、首个失败证据；
更新任务状态和下一项契约。不得把 QEMU 工件通过写成 RVISS 已启动 Linux。
