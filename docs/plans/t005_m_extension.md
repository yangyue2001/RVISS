# T005：RV64 M 2.0 全部乘除指令

状态：active（2026-10-12）。依赖 T004 verified；属于 Linux/Spike 总目标。

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

- [ ] 13 个编码执行，关闭配置全部非法，保留 T003 原断言。
- [ ] 固定边界期望经真实 engine_step 覆盖除零、signed overflow、high multiply、
  *W 高位输入忽略和 unsigned 符号扩展、x0、rd alias；UBSan 路径无报告。
- [ ] 固定 Spike 全边界组合与 seeded 序列逐事件比较、重复执行一致，I 基线
  29 cases / 343 events 和 3 种差异注入继续通过。
- [ ] make CC=/usr/bin/gcc check 在 minimal 和 M 配置通过，提交证据与任务状态。

## 下一步

完成并提交后执行 T006 A 2.1：LR/SC reservation、AMO RAM 权限/精确 fault 与 Spike。
总目标仍 active，不能在 M 局部完成时停下。
