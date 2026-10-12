# T005：RV64 M 2.0 全部乘除指令

状态：verified（2026-10-12）。依赖 T004 verified；属于 Linux/Spike 总目标。

## 行为与范围

开启 CONFIG_RV_M 后，真实取指/译码/engine_step 执行 13 条 RV64 M 指令，
与固定 Spike 在相同初始状态逐事件一致。关闭扩展时这些编码产生非法指令，
不退休、不写 rd。核心 C，元信息集中在 instr/m/instr.def，执行位于同目录。
不实现 A/C/F/D、CSR WARL、Sv39 或设备；这些是后续总目标任务。

固定语义依据为 Ratified-IMAFDQC/src/m.tex（M 2.0）：低/高位乘积的三种符号
组合，signed 除法向零截断，余数符号跟随被除数；除零商全 1、余数为被除数；
signed 最小值除以 −1 返回最小值和零余数；所有 *W（包括 unsigned）结果符号
扩展至 64 位。不得让宿主 signed 除零/溢出进入 C UB。

当前 decoder 已检查 funct7 并从 X-macro 构建；I 的源寄存器是 CPU 指针，
执行须在写 rd 前取得两个输入，以支持 rd=rs1/rs2。根 minimal 保持 M 关闭，
新增 m_defconfig 只开启已完成的 M；misa 完整 WARL/能力宣告在特权任务完成。
不同配置顺序构建，隔离 BUILD_DIR；恢复根 minimal 后继续。

## 验收

- [x] 13 个编码执行，关闭配置全部非法，保留 T003 原断言。
- [x] 固定边界期望经真实 engine_step 覆盖除零、signed overflow、high multiply、
  *W 高位输入忽略和 unsigned 符号扩展、x0、rd alias；UBSan 路径无报告。
- [x] 固定 Spike 全边界组合与 seeded 序列逐事件比较、重复执行一致，I 基线
  29 cases / 343 events 和 3 种差异注入继续通过。
- [x] make CC=/usr/bin/gcc check 在 minimal 和 M 配置通过，提交证据与任务状态。

## 下一步

完成并提交后执行 T006 A 2.1：LR/SC reservation、AMO RAM 权限/精确 fault 与 Spike。
总目标仍 active，不能在 M 局部完成时停下。

## 实际验证

官方 M 2.0 源文件 blob fb3ac6a0c62aca0382895b87d088c02dc22345be 已读取。
实现 13 条指令，128 位高乘积与显式除零/overflow 分支避开宿主 UB。
执行源先取值再写回；所有 *W 无条件符号扩展。misa 的完整处理仍属后续特权项。

实际命令及日志/哈希在 docs/validation/t005_checks.json；所有命令退出 0：
minimal make check：8 unit / 2394 checks，M 配置：8 unit / 2202 checks；
两配置各 3 smoke 与 CLI/UI 14 checks。新增 test_m 的 32 常量边界乘 4 种
rd 选择经 engine_step 验证成功/非法，不改旧 T003 期望。
M 的 check-diff：346 cases / 4359 events，包括原 I 29/343、15×15 输入边界
每组 13 指令、91 alias/zero-source case 和完整 1000 条 seeded 序列。
双方重复执行一致，3 种差异注入仍捕获；docs/validation/t005_difftest.json。
UBSan 独立目录构建 test_m 与完整 I/M difftest，执行均通过且无报告。
这是自建真实 ISA 编码检查与独立 Spike 差分，未运行官方 riscv-tests 全套。
根配置完成后恢复 minimal；T006 A 为下一项，总目标继续。
