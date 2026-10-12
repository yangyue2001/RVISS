# T007：RV64 C 2.0 整数压缩指令与 IALIGN

状态：active（2026-10-12）。依赖 T006 verified，Linux/Spike 总目标继续。

## 行为与边界

CONFIG_RV_C 开启后支持 RV64C 整数指令、NOP/HINT、C.EBREAK，以及 16/32 混合
取指与 IALIGN=16。D 未实现时 C.FLD/FSD/FLDSP/FSDSP 保持非法，D 任务接入。
不开 C 时保留全部 IALIGN=32/16 位非法断言。核心只 RV64，不加入 RV32/RV128。
固定依据 Ratified-IMAFDQC/src/c.tex blob af8aca70af026605066b68450253d359a2f3101a
与 rvc-instr-table.tex；Priv-v1.12 的 misa.C / IALIGN 更新抑制规则。

压缩编码在 instr/c 集中维护，展开至已验证 I 执行路径；保留原始 16 位编码、
真实长度和 snpc=pc+2，执行 operand 使用展开编码，C.JALR 链接 pc+2。
所有零立即数/rd=0 的 reserved 和 HINT 按规范分别处理，不把 reserved 当 NOP。
取 32 位第二 halfword fault 保留起始 epc 与第二半字 tval；不多取压缩指令。

C 改变运行时 IALIGN，需要提前实现有限 misa WARL：固定 MXL/I/已实现 M/A，
只有编译支持的 C 位可开关，未实现扩展不得置位；S/U 和完整 CSR/PMP 仍在下一项。
清 C 会使下一指令不满足 IALIGN=32 时抑制整个 misa 写。DUT case 在共同初始
ISA 上设置 C，运行中不复制状态。旧 T003 测试显式选 C 关闭的 I profile，
旧 fault smoke 启动时执行 misa.C 清除，继续原 12 trap 断言；新增 C profile
检查对应半字目标成功行为。这不是关闭检查或改变旧期望。

## 验收

- [ ] 全部 RV64 整数 C 语义组、HINT/reserved/非法、寄存器与立即数边界，
  mixed 32-bit 指令位于 pc%4=2，C.JR/JALR link/alias，compressed load/store fault。
- [ ] 真实 engine_step 常量检查；C 关闭非法，C 开启 IALIGN16；原 T003 回归保留。
- [ ] 固定 Spike 对拍正常/边界/非法、取指 fault、全部 16-bit 编码空间的独立
  单事件比较（相同初始寄存器/RAM，含数据/control/fault）；重复 replay。
- [ ] misa.C 动态写、抑制规则与关闭后的跳转 fault；其它能力位不能虚构。
- [ ] minimal/MAC make check、旧 I/M/A difftest、UBSan，命令/身份/失败包落盘。

不实现浮点或完整特权 WARL/委派/Sv39/PMP。misa 完整能力与 CSR 测试在 T008
扩展；checkpoint 原生状态布局问题仍另记，非总目标完成条件。
完成并提交后 T008 特权 CSR/委派/xRET/counter，再 PMP/Sv39/FP/设备/Linux。
