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

T001/T002/T003 verified，初始提交 e745972。Git 已初始化，origin 已配置，
远端 ls-remote 实测为空；尚未 push。
T004 verified：固定 Spike 29 cases / 343 events、3 种差异注入、回归均通过。
T004 已提交 a82ef82；随机程序 handler 覆盖修复 d275906。T005 M verified：
13 指令、I/M Spike 346 cases / 4359 events、UBSan、minimal/M 回归通过。
T005 已提交 215ca1e。T006 A verified：22 指令、I/M/A Spike 982 / 9172、
UBSan 与三配置回归通过，提交 1710e50。T007 C verified：49152 编码全部 Spike
差分通过（全部 50215 / 59406），三配置与 UBSan 通过；提交 fd23733。
T008 CSR 检查点 verified：MAC/CSR 1741 / 11442、minimal/MAC 回归与 UBSan；
CSR 提交 65847b2；同步委派/xRET 也 verified，MAC/CSR/priv 2195 / 12505，
minimal/MAC 回归与 UBSan，提交 27fcd99。T009 中断 verified：2652 / 13405、
minimal/MAC 回归与 UBSan，提交 ccc1005。T010 counter verified：3249 / 15178，
12 unit 回归与 UBSan，下一项 T011 PMP/Sv39。
Git 写入受沙箱只读 .git 限制，已按工具权限
流程授权。Linux 仍仅有 QEMU 工件验收，RVISS 尚未启动。
