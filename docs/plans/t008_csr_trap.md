# T008：CSR WARL、同步异常委派与特权指令权限

状态：active（2026-10-12）。依赖 T007 verified，属于连续 Linux/Spike 目标。

## 可验证行为

固定 Priv-v1.12。客户机通过 CSR 指令配置 trap/特权状态，读回合法 RV64 值；
M/S/U 的同步异常依据 medeleg 选择 M/S，保存正确 epc/cause/tval 和状态栈。
MRET/SRET/WFI/SFENCE.VMA 和 satp 遵守特权/TSR/TW/TVM，非法时不提交状态。
核心 C，元信息清单管理地址/存储，WARL、别名、读抑制与副作用显式处理。

范围包括 mstatus/sstatus 的 RV64 SXL/UXL、MPP/保留位、无 VS/XS 时硬连线；
FS 在 S 实现中保留软件浮点模拟所需读写位（Priv-v1.12 允许），SD 从 FS 派生；
tvec mode、epc 对齐及随 misa.C 的读掩码；mie/mip/sie/sip、deleg 的可实现位与
软件/设备线分离；未实现分页时 satp 只接受 Bare，后续 Sv39 接入集中入口。
CSR RW 的 rd=x0 读抑制、RS/RC 的 rs1/zimm=0 写抑制；Trap 与 xRET 更新路径
共用 WARL/对齐规则，返回低特权清 MPRV。S/U 能力位在实际规则闭环后再公布。

分检查点实现/验证/提交：先 CSR 可见值与访问副作用，再同步委派/xRET；
每次检查相关调用方/旧测试，遇到旧断言与新规范冲突记录明确差异，不改期望掩盖
回归。数据访问仍只 Bare；PMP/Sv39 与 F/D 是后续任务。
中断选择/全局使能与 counter 独立为 T009/T010，可根据实际接口依赖合并边界。

## 验收

- [x] 真实 CSR 指令路径的 reserved/WARL/alias/权限/抑制和无错误写回。
- [ ] M/S/U trap 目标、status 栈、xRET、epc/misa.C、非法特权指令精确性。
- [ ] 固定 Spike 对拍上述规则，共同 profile 的差异必须先判规范合法性，
  不在失败后增加 skip 或复制 DUT 状态。observer 为只读，CSR 读副作用要可测。
- [ ] 原 I/M/A/C 差分和 make check 回归，证据、首差异、实际命令与提交落盘。

下一项为中断/计数器，然后 PMP/Sv39、FP、平台设备与固定 Linux；局部完成继续。

## 检查点

T007 已提交 fd23733。当前先实现 CSR 可见值/WARL/别名和权限，
再同步 trap/xRET。所有 49152 个 C 编码无需为纯 CSR 规则重复，
正常 I/M/A/C 有限回归和新增独立 CSR 规则差分保持开启。


### CSR 检查点：verified

最小配置 10 unit / 6950 checks，MAC 10 unit / 7364 checks，各 3 smoke + CLI/UI 14
checks。独立 Spike：minimal + CSR 707 cases / 1531 events，MAC + CSR
1741 / 11442，初始 + 每事件比较 21 个特权 CSR、PC/raw/length/priv、全 GPR、
store、全 RAM hash 和退休；两端新进程重复执行、3 种差异注入都通过。
UBSan CSR 181 checks + MAC/CSR 全部差分通过，无报告。
实际命令与身份见 docs/validation/t008_csr_checks.json 和三个差分报告。

显式 WARL/别名规则覆盖 mstatus/SSTATUS、tvec、epc、deleg、IRQ CSR、counteren、
Bare satp 与 TVM。旧 SIE 单测缺少 mideleg 前提，补该前提后保留原可见位断言。
FS 可写用于 S 的软件 FP 模拟，VS/XS/BE/未实现位清零，SD 派生；不据此宣称 F 支持。
无 read-side-effect CSR 的当前 profile 下，RW rd=x0 的实现读抑制通过代码审查；
实际指令测试验证写权限、源捕获、RS/RC 写抑制和错误无 GPR 提交。

首差异是桥接能力配置错误：numeric 0 在固定 Spike 枚举中为 Sv32，未清旧 Sv57；
明确选择 IMPL_MMU_SBARE 后原输入重跑通过。完整差异包已落盘，未删用例/掩码。
ABI 原始导入仍仅是骨架，不宣称能完整恢复设备 IRQ；CPU 新软件位导致 native
checkpoint 布局变化，不在本目标中声称旧文件兼容。
下一检查点继续同步委派/返回；T008 总状态保持 active。
