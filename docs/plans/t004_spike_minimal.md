# T004：P2 固定 Spike 最小逐事件差分

状态：verified（2026-10-12）。依赖 [T003 verified](t003_execution_boundary.md)。
本任务已完成固定源码、独立桥接和限定范围的逐事件差分。

## 目标与输入

用可确认提交版本的独立 Spike，比较同一初始化状态上的裸机 RV64I/RAM/
同步异常执行事件，并能捕获故意注入的 GPR、store 和 trap 差异，保留首差异包。
输入为 [profile v1](../rv64_profile.md)、StepResult、T003 fault 回归与
固定的 Priv-v1.12/非特权规范。T002 Linux 工件继续保持。

实施前证据：PATH 没有 spike；本地检索没有找到完整的 riscv-isa-sim 源码。
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

- [x] 固定真实源码版本、构建选项、工具/工件身份；独立运行最小 Spike case。
- [x] 公共初始化契约与 profile 差异逐项记录；重复 reset 执行结果相同。
- [x] 普通 I、RAM load/store 和同步 trap 逐事件对拍通过，不修改原用例期望。
- [x] 故意改变 DUT GPR、store 数据、trap tval 各被捕获；报告首个事件及字段。
- [x] 失败包含版本、初始状态/RAM、输入字节、预算、实际命令、双方近期事件。
- [x] 加入真实 check-diff 入口（目前不存在）；make CC=/usr/bin/gcc check 回归。
- [x] 保存执行数/失败/任何未覆盖原因，更新任务状态、README/profile/下一项契约。

## 实际结果（2026-10-12）

Git 初始提交 e745972 记录 T001/T002/T003，origin 已配置。
固定官方 Spike 提交 da2f415bb62bec3e93b33b379d787b4262aade9f（2023-12-30），
源码归档 SHA256 1d82e498671cf82279c28d8e46c6fa85e3cd14b7c5c18e24707ed628c8b8c6b5。
逐文件与归档核对，参考源码未修改；bridge 使用独立 C++ 进程，DUT 是 C 进程。
构建工具 /usr/bin/gcc、/usr/bin/g++ 11.4；本版本自带 FDT/SoftFloat，关闭 Boost，
使用本机 dtc。提交与归档身份见 docs/validation/t004_spike_manifest.json。

实际运行：

```sh
python3 tools/spike/build.py --jobs 4
make CC=/usr/bin/gcc check-diff
make CC=/usr/bin/gcc check
```

全部退出 0。差分 29 cases / 343 events；每个 case 在新进程重复 reset/执行，
双方全事件输出各自重复相同。普通整数各语义组、7 load/4 store、分支/JAL/JALR、
非法编码/CSR、U/S/M ECALL、EBREAK、未映射/非对齐/越 RAM 边界、第二 halfword
取指 fault，以及 seed=0x52495649 的 200 条整数 R/W 运算序列通过。
故意改 DUT GPR（event 1）、成功 store 后 RAM（event 8）、trap mtval（event 1）
全部捕获；最小输入、双方至多最近 16 事件、身份、命令保存在 failure JSON。
独立提交证据位于 docs/validation/t004_difftest.json 和 t004_injected_*.json；
完整运行文件在 build/difftest/，构建日志在 build/spike/。
原 make check 7 unit / 729 checks、3 smoke、CLI/UI 14 checks 回归通过。

首次 Spike 构建在 ar 归档时因磁盘不足失败：build-no-space-first.log。
只将本次新生成的对象 strip-debug，重建全部库后成功；未动 Linux 工件。
初次 configure 给出的 --enable-commitlog 本版本不认识，日志保留；实际 commit
记录由 processor_t::enable_log_commits() 开启，不把 configure warning 当作启用证据。
最终 build.py 不再传该无效参数。后续干净构建使用 CXXFLAGS=-O2 避免大体积调试对象。

## 比较边界和剩余工作

两进程读共同 case，初始化后不再同步；比较完整 GPR、PC、raw/length/valid、priv、
18 个 trap/priv CSR、退休和 store/整 RAM hash。输入固定 mstatus.UXL/SXL=RV64
(0xa00000000)，不在运行中掩码或修正这些位。当前 RVISS reset/WARL 尚未完成，
该初始化是测试输入，不声称 reset 合规。能力 misa 身份与 mcycle 平台计时策略不同，
本项在开始前限定不比较它们；不测试 counter 写入，Spike 单事件退休通过 minstret
变化识别。后续 CSR/counter 项需扩展观测，不能将此判定用于写 minstret 的指令。

第一阶段关闭 PMP（Spike cfg.pmpregions=0）和异步设备，不表示 profile 的 16 PMP
已支持。参考 RAM 用 checked callback，避免 Spike 整页 host mapping 假设掩盖
非整页边界故障；callback 仅提供物理字节，fault/对齐/语义由 Spike 自己决定。
未测试长指令编码：双方前端可取得的长度不同，正常共同输入仅 16/32 位。
该版本 README 的 Priv 1.11 标注过时且没有版本选择参数；本次按固定 1.12 规范核对
所用 trap/mtval 规则，不把整套未测试特权语义称为已认证 1.12。

这一结果是实际 RVISS DUT → 独立 Spike 的最小差分，不能代替 GC/Sv39/Linux 对拍。
整个用户目标仍 active；下一项为 T005 M 扩展，随后 A/C/特权/MMU/FD/设备和 Linux。
每个通过的任务点创建 Git 提交并继续，不在此局部阶段结束总目标。

## 后续复核修正

扩展 M 用例时发现 case generator 的固定 trap handler（偏移 256）会覆盖
较长随机程序的两条指令；原 200 条序列实际提前跳进 handler 的循环。
原双方事件仍一致，但完整随机路径覆盖声明不足。已将 handler 移至 RAM 尾部，
对 seeded-integer 添加全部 200 条顺序退休及第 201 条尾部跳转的 PC 断言。
重新运行 make CC=/usr/bin/gcc check-diff 退出 0，仍 29 cases / 343 events，
三种注入均捕获。日志 build/spike/check-handler-fix.log，耐久 JSON 已更新。
这项修复只改测试输入布局，不改变指令期望或忽略差异。
