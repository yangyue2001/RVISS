# T004：P2 固定 Spike 最小逐事件差分

状态：pending（2026-10-12）。依赖 [T003 verified](t003_execution_boundary.md)。
本契约定义下一次局部闭环，不表示已经取得或执行 Spike。

## 目标与输入

用可确认提交版本的独立 Spike，比较同一初始化状态上的裸机 RV64I/RAM/
同步异常执行事件，并能捕获故意注入的 GPR、store 和 trap 差异，保留首差异包。
输入为 [profile v1](../rv64_profile.md)、StepResult、T003 fault 回归与
固定的 Priv-v1.12/非特权规范。T002 Linux 工件继续保持。

当前证据：PATH 没有 spike；本地检索没有找到完整的 riscv-isa-sim 源码。
nanhu/ready-to-run/riscv64-spike-so 的来源 commit/profile/ABI 未确认，
Ibex vendor 目录不是完整源码；不将这些工件作为已固定 oracle。

## 范围与约束

- 获取官方完整 riscv-isa-sim 源码，固定 tag/commit、归档 SHA256、编译器与
  build flags，核对特权 1.12、RV64 I/Zicsr/Zifencei、对齐/地址/mtval/PMP 策略。
  依赖放本工作区隔离目录；不修改外部参考仓库或引入来源未知二进制。
- Spike 内部 C++ API 仅用于独立工具 tools/spike/（或同等工具目录），
  不扩散到 C 核心。先核对真实 API/callers 再冻结小型 bridge 协议。
- 构建 RVISS DUT runner，明确按 engine_step 事件驱动，同步异常不退休；
  每个 case 从同一初始 CPU/RAM 开始，执行期间禁止再同步 DUT 状态到 REF。
- 第一批仅 I 算术/分支/JAL/JALR、成功 RAM load/store、非法编码、ECALL/
  EBREAK、访问与对齐 fault。关闭异步设备输入，按明确事件上限停止，
  不用私有 nemu_trap 作为 Spike 的共同退休指令。
- 比较 PC/raw/length/priv、GPR 全量及写回、有关的 trap CSR、成功 store/RAM
  变化和 cause/epc/tval。能力标识/计数器/PMP 初值的差异先记录合法 profile
  策略和比较范围；不在失败后临时增加 skip、掩码或覆盖 REF 状态。
- 发现 T003/既有 I 的局部语义回归时，保留最小复现/固定规范依据后修复并重测。
  需要新的完整 CSR/PMP/扩展能力时写明依赖，不把本项扩大为实现所有特权语义。

非目标：GC 扩展全实现、Sv39/PMP、异步 timer/UART/PLIC、完整 Linux 对拍、
NEMU/RTL ABI、完整 checkpoint、随机长跑或性能优化。
桥接协议不假定 T003 的内部 StepResult 是稳定序列化 ABI。

## 验收与检查点

- [ ] 固定真实源码版本、构建选项、工具/工件身份；独立运行最小 Spike case。
- [ ] 公共初始化契约与 profile 差异逐项记录；重复 reset 执行结果相同。
- [ ] 普通 I、RAM load/store 和同步 trap 逐事件对拍通过，不修改原用例期望。
- [ ] 故意改变 DUT GPR、store 数据、trap tval 各被捕获；报告首个事件及字段。
- [ ] 失败包含版本、初始状态/RAM、输入字节、预算、实际命令、双方近期事件。
- [ ] 加入真实 check-diff 入口（目前不存在）；make CC=/usr/bin/gcc check 回归。
- [ ] 保存执行数/失败/任何未覆盖原因，更新任务状态、README/profile/下一项契约。

当前下一步：先确认官方固定版本及本地构建依赖，再实现最小 bridge 和两条
正常指令比较；此切片通过后扩到 store 和 trap。网络/沙箱失败按运行环境流程
申请所需权限，不能把依赖缺失写成差分通过。
