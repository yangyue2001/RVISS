# RVISS Linux 与 Spike 连续实现目标

状态：active（2026-10-12）。用户明确授权建立 Git、逐任务/阶段提交并持续完成
RVISS 启动 Linux 和 Spike difftest；这一授权覆盖必要的 C 核心/接口/设备实现。
原先各局部契约的非目标只限制该任务，不取消本总目标授权。

## 完成条件

- 本地 Git 已建立，origin=git@github.com:yangyue2001/RVISS.git，已验证任务有提交。
- 固定完整 Spike 源码/配置，I/RAM/trap 的逐事件差分和差异注入检查通过；
  随 M/A/C/F/D、CSR/特权/MMU 的开发持续增加独立差分，不能把 ABI 符号当作完成。
- 相同 T002 固定工件从 OpenSBI 进入 RVISS 的 S-mode Linux、Sv39、PID 1、
  BusyBox；真实串口输入，用户态 FP/原子/分页 probe 和重复 workload 通过。
- 保存实际命令、身份/哈希、预算、首差异、冷启动与长跑报告。
  QEMU 与 RVISS 分开报告。未验证能力不得标完成。

## 推进顺序

T004 最小 Spike bridge → M → A → C → CSR/委派/返回与 counter → PMP/Sv39 →
F/D → virt/UART/PLIC/CLINT/finisher → 固定 OpenSBI/Linux → 重复验收。
依照真实依赖可调整具体顺序，每一项先写规则与验收后实现/测试/提交。
不更换 Linux 输入或放宽预期掩盖问题；性能只在实际阻碍验收时局部优化。
完整 checkpoint、RTL consumer 和 H 不属于本次最终完成条件。

## 当前检查点

T001/T002/T003 verified。Git 已初始化；正在创建现有源码与可复现证据的初始提交。
T004 pending，即将固定 Spike 源码并实现独立桥。Git 写入受沙箱只读 .git 限制，
已按工具权限流程申请；这不是用户未授权提交。
