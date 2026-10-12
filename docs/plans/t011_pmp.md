# T011：真实取指观察与 16-entry PMP

状态：active（2026-10-12）。依赖 T010 verified（f274d04）；持续 Linux/Spike 目标。

固定 Priv-v1.12 Physical Memory Protection；PA=56 bits，粒度 4 bytes，16 entries，
OFF/TOR/NA4/NAPOT。RV64 pmpcfg0/2 的 8 字节打包、addr 54 位、W 无 R 清 W；
L 锁 cfg/addr，后继 locked TOR 锁前驱 addr，reset 解锁。编号小者优先，任一
字节匹配时必须完整覆盖访问；未锁 M 旁路，锁定 M 按权限，有 entry 时无匹配
拒绝 S/U。MPRV 只影响数据，PMP 拒绝返回原访问的精确 access fault，无数据效果。

Kconfig RV_PMP 关闭时 0 entries，开启时默认 16。独立工具硬件 profile 可以在
初始化选择 0/16，选择在客户机执行前完成；固定 Spike 同样使用 cfg.pmpregions。
普通旧 I/M/A/C/CSR/priv/IRQ/counter 用例保持共同 0-entry 输入；PMP 用例明确 16，
不能失败后复制许可状态。旧 unit 的 Bare/0-entry fixture 需明确该 profile。

先完成一个观察检查点：移除参考端主动 load_insn 预取，使用 Spike 公开 mmio_fetch
回调捕获实际字节和 memtracer FETCH 完成事件。该接口能区分取指与 PTE/data 读取，
允许跨页的两个 halfword 分别捕获；取指失败仅报告真正取得的字节，IRQ 不取指。
从而工具不会提前更新 A/D 或读 MMIO。保持固定官方源码未经修改，无源码补丁。
观察变更须原 3249 cases / 15178 events 和注入/重放全部通过后提交。

PMP 验收：CSR WARL/锁/打包/权限，边界含 partial match、空/反向 TOR、全 PA NAPOT、
MPRV；真实 fetch/load/store/LR/SC/AMO 无错误副作用。16-entry 固定 Spike 比较 cfg/addr
以及原完整事件字段/全 RAM；三个共同配置（minimal、MAC、MAC+PMP）回归，UBSan。
PTE 访问的 S 权限和 A/D 写权限在下一 Sv39 任务验收。首差异保留并修复，不跳过。

完成继续 T012 Sv39（含合法 SFENCE.VMA），然后 F/D、virt 设备与固定 Linux。


## 观察检查点：verified

固定源码保持未修改。真实 mmio_fetch + FETCH memtracer 替代主动 load_insn，
完整原 MAC/CSR/priv/IRQ/counter 3249 cases / 15178 events、两端重放和 3 种
差异注入通过（退出 0）。partial fetch、IRQ 抢先、WFI/CSR serialization、
自修改原子/store 和 mixed C/I 都保留原比较。报告与桥接身份：
docs/validation/t011_fetch_observer.json。PMP 语义实现接着执行，T011 总状态 active。
