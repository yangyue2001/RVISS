# T007：RV64 C 2.0 整数压缩指令与 IALIGN

状态：verified（2026-10-12）。依赖 T006 verified，Linux/Spike 总目标继续。

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

- [x] 全部 RV64 整数 C 语义组、HINT/reserved/非法、寄存器与立即数边界，
  mixed 32-bit 指令位于 pc%4=2，C.JR/JALR link/alias，compressed load/store fault。
- [x] 真实 engine_step 常量检查；C 关闭非法，C 开启 IALIGN16；原 T003 回归保留。
- [x] 固定 Spike 对拍正常/边界/非法、取指 fault、全部 16-bit 编码空间的独立
  单事件比较（相同初始寄存器/RAM，含数据/control/fault）；重复 replay。
- [x] misa.C 动态写、抑制规则与关闭后的跳转 fault；其它能力位不能虚构。
- [x] minimal/MAC make check、旧 I/M/A difftest、UBSan，命令/身份/失败包落盘。

不实现浮点或完整特权 WARL/委派/Sv39/PMP。misa 完整能力与 CSR 测试在 T008
扩展；checkpoint 原生状态布局问题仍另记，非总目标完成条件。
完成并提交后 T008 特权 CSR/委派/xRET/counter，再 PMP/Sv39/FP/设备/Linux。

## 实际验证与接口

压缩编码清单 33 个条目（含 NOP、共享编码的细分）展开为已验证 I 路径。
Decode.encoding 保存展开编码，instr/raw 保留原始 halfword；snpc 和 JALR
链接仍用真实长度。misa 现在固定 MXL=RV64/I/编译完成的 M/A，可切换编译支持的
C；未知/尚未完成的 F/D/H/V/G/S/U 位不能写入。C 清除抑制在 csr_execute 的
明确 snpc 上判断，状态初始化接口不冒充执行指令。CSR/特权整体仍未完成。

所有实际命令/哈希/规范 blob 位于 docs/validation/t007_checks.json。
minimal：10 unit / 6800 checks；MAC：10 / 7211；C-only：10 / 7088；各 3 smoke
和 CLI/UI 14 checks。旧 T003 628 checks 的 I profile 断言保持；fault smoke
清 C 后仍检查原 12 个异常，新增 C 的 halfword/jump/混合/CSR 行为另有 469 checks。

MAC Spike 1063 cases / 10254 events；包括 1000 条 mixed C/I 顺序执行的路径
断言、内存 fault、Hint/reserved、边界取指和 misa.C 正常清除/抑制。
C-only 110 / 1425；UBSan test_c 与全部 I/M/A/C 差分无报告，均退出 0。
对所有低两位不为 11 的 49152 编码，从独立共同初态执行一个事件，双方各新
进程重复运行；全量 50215 cases / 59406 events 退出 0，三种差异注入仍捕获。
输入编码集合已另外核对完整无重复。全 records 在 build/difftest/summary.json，
耐久摘要/输入集合哈希在 docs/validation/t007_exhaustive.json；有限/UBSan/C-only
报告也已保存。无首差异失败。所有编码仅一组初始寄存器/RAM，结合定向与随机
边界；不等于全部状态空间证明，未执行官方 riscv-tests 全套。

有限 difftest 的输出目录现随 BUILD_DIR 隔离，可显式 --out，避免验证互相覆盖。
配置切换早于一个验证进程最终退出的记录不作为最终 minimal 证据，已在配置
稳定后串行重跑并恢复 minimal。D 浮点压缩编码目前合法地非法，D 实现时必须接入。
T008 CSR/同步 trap 为下一项；Linux 仍未在 RVISS 启动，总目标继续 active。
