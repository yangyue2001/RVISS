# T009：中断全局权限、委派与边界优先级

状态：pending。依赖 T008 trap/xRET 检查点，属于持续 Linux/Spike 目标。

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
