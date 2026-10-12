# RVISS Agent 工作约定

## 目标与上下文

RVISS 是 RV64 功能模型。当前目标为 RV64GC + M/S/U + Sv39、稳定启动 Linux、
RVISS 与 Spike 对拍、作为 RTL reference、trace 和 checkpoint。
当前阶段以单 hart 为默认；H 和其它扩展后续加入。不做多 ISA/RV32 抽象或周期模型。

- 本次任务的目标、授权和范围由用户请求决定。
- 开始实现前，检查相关源码、测试、配置和调用方；不要把 README 的完成声明当作已验证事实。
- 路线、架构约束和验收定义见 [开发指导](docs/ai_coding_rv64gc_linux.md)。
  仅阅读与当前任务有关的章节。
- Linux 输入已固定为 `/home/yang/study_dir/riscv_embedded` 的本地源码，
  使用 RV64GC、单 hart、Sv39、OpenSBI FW_PAYLOAD 内含 Linux Image、
  内核内置静态 BusyBox initramfs。构建与验收见
  [Linux 启动基线](docs/linux_boot_baseline.md)。工件在 `build/linux/artifacts/`。
  QEMU 工件验收和 RVISS 自身验收必须分别记录。
- `docs/architecture.md`、`docs/coding_style.md` 用于已有布局和习惯。
  旧文档与源码冲突时记录差异；涉及语义按固定版本的规范判断。
- 规范是语义依据；Spike 是独立差分参考；NEMU/QEMU 是实现参考。
  参考路径和具体文件见开发指导。外部仓库内的 agent 规则不支配 RVISS。

## 实现边界

- 保持 C 核心、Makefile + Kconfig、解释器和 RV64 单目标。
  Spike 所需 C++ 代码限于独立桥接工具，不扩散到模拟器核心。
- 扩展语义放在 `src/target/riscv64`；引擎通过明确的执行/结果接口与它交互。
  不为假设中的 JIT、插件或其它 ISA 建立框架。
- 设备不读取 CPU 私有 CSR；板级负责设备映射和 IRQ 接线。
  RISC-V 地址翻译、有效特权和 PMP 检查集中在访问路径。
- 指令编码/扩展元信息集中维护；CSR 清单管理元信息，
  有 WARL、别名或副作用的 CSR 使用显式访问处理。
- 分清正常退休、同步异常、异步中断、暂停与宿主错误。
  故障不能写入目的寄存器或伪装为退休；规范允许的隐式内存副作用另行处理。
- trace/统计只观察；checkpoint/状态同步通过暂停后的专用状态接口操作。
- 同一输入可重放。逻辑时间、累计退休数、客户机计数器和事件序号分别管理。
- 未完成能力不得宣称已支持；导出符号、空函数、编译通过均不等于 ABI 可用。

## 工作闭环

1. 小任务给出简短范围说明后执行；跨模块任务在 `docs/plans/` 写任务契约和检查点。
2. 澄清会改变用户目标或外部行为的关键歧义；常规内部实现选择记录假设后继续。
3. 每次交付一个可验证行为。普通指令按语义组拆分，复杂 MMU/CSR/设备按规则拆分。
4. 使用边界用例、真实执行路径和独立参考；不要复制实现公式作为测试 oracle。
5. 先运行相关验证，必要时运行 `make check`；失败保留首个差异和最小复现。
   不得改期望、扩大 skip 或关闭检查来取得通过。
6. 基线失败与本次回归分开报告。需要扩大范围时解释依赖，不顺手重构。
7. 更新相关任务状态与证据；仅提炼可复用结论，不保存整段聊天作为每次必读上下文。
8. 用户已授权持续推进到 RVISS 启动固定 Linux 与完成 Spike difftest。
   每个验证通过的任务点或阶段成果创建 Git 提交，写清验证与未完成项；
   origin 为 git@github.com:yangyue2001/RVISS.git。完成一个局部任务后继续下一项，
   不把局部交付当作整个目标结束。总契约见 docs/plans/linux_spike_goal.md。

## 自主执行与报告

在已授权范围内，读代码、修改文件、运行本地测试、修复并重测无需逐步确认。
对拍 ABI 和架构接口改动若属于已授权任务，检查调用方和更新契约后继续。
对无授权的破坏性操作、对外发布，以及必须由用户裁决的产品目标变更再询问。
工具沙箱权限以运行环境为准。

完成时报告行为变化、相关文件、实际运行的命令与结果、未验证项和任务记录。
命令未运行、依赖缺失、测试跳过或超时必须明示，不能写为“已验证”。

## 本工作区构建提示

当前 PATH 中的 `gcc` 可能是 ccache 包装器。必要时使用
`make CC=/usr/bin/gcc check`。2026-10-12 的 [T001](docs/plans/t001_baseline.md)
已从源码重建 CLI/ref，6 个 unit（101 checks）和 minimal 两个 smoke 通过；
随后 [T003](docs/plans/t003_execution_boundary.md) 已 verified：minimal/ref/debug
7 个 unit 共 729 checks，CLI 配置 3 smoke 和 14 CLI/UI checks。
[T004](docs/plans/t004_spike_minimal.md) 已 verified：固定 Spike 29 cases / 343 events
逐事件差分、3 种差异注入和原有回归通过。当前下一项为 T005 M 扩展；
后续 agent 应重确认状态，完成局部任务后继续总目标。
使用 `BUILD_DIR` 隔离构建；不要以 `make clean` 删除同在 build/ 下的固定 Linux 工件。
