# T010：客户机 counter CSR 与平台时钟分离

状态：verified（2026-10-12）。依赖 T009 verified（ccc1005）；持续 Linux/Spike 目标。

固定 Priv-v1.12 / 同一快照 counters.tex（blob 76fbf9b5921e16021888a300a1d3caf2c8282939）。
实现 mcycle/minstret、只读 cycle/instret/time、mcounteren/scounteren 访问权限；
RV64 不提供 *h 影子。计数器显式写优先于该指令递增，RS/RC x0 只读仍正常递增；
别名和目的/源重叠正确，trap/IRQ 不计 instret。累计 sim.instret 不受 guest 写影响。

实现选择先固定：mcountinhibit 采用 Priv-v1.12 许可的硬连线 0，写无效果；
29 个 M HPM counter/event 也硬连线 0，无 Zihpm 用户影子，不宣称有可用 HPM。
固定 Spike 这一版本同样选择这些只读零规则。可写 CY/IR 是以后能力，不属于
Linux 必要输入。mcycle 当前每已完成事件 +1（功能模型逻辑周期），Spike CPI=1
按退休 +1；跨 fault/IRQ 的 cycle 数值不能用作强制匹配。共同 cycle 差分仅使用
全退休序列；instret/写后可见值/权限/time 仍逐事件比，不能运行中强制同步计数器。

板级将 time CSR 接到同一 CLINT mtime 只读回调，设备不读 CPU CSR。没有板时默认
0；测试传入独立时间值/事件，两端分别读取输入，不能从 DUT 输出来更新 REF。
平台 time/事件/预算/累计退休与 guest counter 写入严格独立。

参考退休观察从 guest minstret delta 改为固定 Spike 的实际 commit log 是否增加，
独立累计工具计数器；写 minstret 不能把退休指令误判成异常。仅使用公开 API，无
参考源码补丁；初始化 mcycle/minstret 按该版本写 API 的即将递增约定恢复共同值，
并比较初始状态。保留原 I/M/A/C/CSR/priv/IRQ 对拍、真实路径 unit 和 UBSan。

验收：全宽/溢出/零值/源目的别名、写优先、M/S/U 权限和只读写请求、RV32 CSR
非法、HPM/抑制 WARL 读零；真实 CLINT 读写/tick 与 CSR time 一致；非法/IRQ/写
counter 后的累计退休、逻辑时间、预算保持正确。首差异保留并修复，不删期望。

完成提交后继续 PMP/Sv39（包括 SFENCE.VMA 合法执行），然后 F/D、平台与 Linux。


## 实际验证

MAC 全部有限用例 3249 cases / 15178 events，minimal 2203 / 5255；23 CSR
含 guest minstret 与 mcountinhibit 每事件比较。两端新进程重放和 3 种故意差异
捕获通过。500 条连续写/读确认全部退休、最终 guest minstret=1、累计=500，
显式给定初始 counter 的全部初始快照一致。TIME 外部事件/权限/读写拒绝通过。
1238 counter unit 检查包括 CLINT mtime 读写/tick 和 time 同源，guest 写不影响
time/事件/累计退休。minimal 17147 / MAC 17571 checks，各 12 unit/3 smoke/CLI14。
UBSan counter 1238 与 MAC 全部差分通过，无报告。
命令、二进制/参考身份、初始 API 约定、合法策略范围见 docs/validation/t010_checks.json。

首次构建失败是新测试误用 legacy paddr_read 三参数，按已存在 checked API
修正为 paddr_try_read；原错误输出已存证，未改期望。没有首语义差异。
固定参考源码未修改；退休观察用 commit 输出增长取代 guest counter delta，
不再受写/溢出/初始值干扰。布局新增 counter 元信息，native checkpoint/旧 ABI
兼容仍未验证，不能把本工具协议当作 simulator ABI 完成。
下一任务 T011 PMP，随后 Sv39/合法 SFENCE、F/D 和固定 Linux，持续执行。
