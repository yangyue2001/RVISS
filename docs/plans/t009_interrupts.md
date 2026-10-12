# T009：中断全局权限、委派与边界优先级

状态：verified（2026-10-12）。依赖 T008 trap/xRET verified（27fcd99），属于持续 Linux/Spike 目标。

固定 Priv-v1.12。边界上的 mie&mip：目标 M 在低于 M 或 MIE=1 时可响应；
已委派给 S 的位在 M 被屏蔽，在 U 或 SIE=1 的 S 可响应。目标 M 的中断优先于 S，
同一目标依 MEI > MSI > MTI > SEI > SSI > STI。故障 PC 处的中断先于取指，
独立事件不计尝试/退休，不修改 GPR 或访问 RAM，状态栈使用 T008 统一入口。

真实 IRQ 线高/低和软件 pending 都经过集中访问规则。共用输入新增事件边界 IRQ
变化，仅初始化/输入事件分别送两端，不依据 DUT 输出更新 REF。参考由固定 Spike
自己的中断判断与 trap 产生结果；工具只能观测和分类实际事件。

验收：目标级优先于数值位优先、全部 6 位 × M/S/U ×全局使能×deleg×独立使能；
电平取消、嵌套 handler 返回与无取指、事件/尝试/退休预算；unit + Spike 每事件
差分、现有 I/M/A/C/CSR/priv 回归及 UBSan。首次失败包保留，不跳过差异。

固定 Spike mip 存储不独立提供 SEIP 软件 B 和设备 E 两份状态（csrs.cc）；
T008 unit 已检验 RVISS 的 B|E/RMW。共同硬件 SEIP 用例要求 B=0、线高时不写
该软件位；软件 IRQ 用例另行通过 CSR。这个限制在产生用例前记录，不宣称整个
设备或 PLIC 功能已通过。实际 virt 设备和完整 Linux 在后续任务。

下一项 T010 guest counter/权限/mcountinhibit；随后 PMP/Sv39、F/D 与平台。


## 实际验证

中断 unit 4420 checks：全部位、目标、全局权限、单独使能、deleg、电平取消；
纯中断不取指/访问数据、尝试/退休保持 0，事件/逻辑时间递增。旧预算/CLINT
执行路径回归保留。MAC 差分 2652 cases / 13405 events，minimal 1606 / 3482；
新增硬件电平、软件 pending、target priority、enable/ret 边界、取消、无映射
fetch 抢先、U→S→M 中断后 MRET/SRET 返回、WFI 唤醒都匹配。
两个 profile 各 11 unit，minimal 15909 / MAC 16333 checks，各 3 smoke 和 CLI/UI 14；
UBSan intr 4420 + MAC 差分通过，无报告。重放及 3 种故意差异捕获通过。
实际命令、身份、范围限制见 docs/validation/t009_checks.json 与差分摘要。

无新增首语义差异。独立桥接使用 Spike 公开 MIP 输入接口，分类实际 trap cause，
不复制中断选择算法。IRQ 前工具的取指仅限当前无副作用 Bare RAM profile；
Sv39 前必须重新检查，不能借工具预取改变 A/D 位或设备读行为。
提交后继续 T010 counter，再 PMP/Sv39 和 Linux 所需剩余功能。
