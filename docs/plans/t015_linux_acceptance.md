# T015：固定 Linux 冷启动与长跑验收

状态：active，依赖 T014 verified（b83d9b9）。

固定 T002 的 FW_PAYLOAD/DTB/内核/BusyBox/init/probe/workload 身份不变。
完成3次冷启动各100轮和1次1000轮：PID1、probe、BusyBox真实UART输入、
精确workload标记和SBI/finisher关机。保留输入事件、预算、二进制/工件/log
哈希及实际性能。与独立Spike语义矩阵的验证分别记录。

已测性能依赖：T014一次额外1轮仍需203680985次尝试、134.785宿主秒，
同一输入重放124.176秒；10M采样中PMP检查52.72%、decode7.74%。
先做一个局部性能检查点：NAPOT低位连续1的计算改为GCC trailing-zero
intrinsic（加bit54哨兵，保持54位上界与非零输入）；不加TLB/缓存，不改变
CSR/访存/异常/优先级或任何可见状态。原16-entry PMP unit和独立Spike
Sv39/PMP/全部GC回归、UBSan，再以T014完整输入重放比对输出和全部计数。
性能测量用固定10M样本与完整重放，不靠推测声明加速。通过即提交检查点。

随后 `tools/linux/run_rviss.py --boots 3 --rounds 100` 和
`--boots 1 --rounds 1000` 实际执行。基于测得指令量选择明确预算，允许增加
宿主timeout以覆盖解释器成本；不改变输入或降低轮数。不执行make clean。
失败保存首个状态/最近事件/输入记录/预算，修复后复测。完成后更新总任务、
运行提示和支持声明，提交最终证据。完整checkpoint/RTL ABI/H不是本次条件。

性能检查点verified：只更换NAPOT低位计数，17unit/25676checks、3smoke、
CLI14、PMP UBSan1518；GC与UBSan固定Spike各8949/74887全部通过。
T014同一event输入整机重放通过，所有计数与去宿主performance行后的完整
输出不变；完整运行124.176→82.284宿主秒（观测1.51倍）。采样PMP调用数
35330351保持不变，自耗时1.84→0.89；采样总walltime受宿主竞争影响，没有
宣称该短采样整体加速。证据t015_performance_checkpoint.json与相关reports。
性能阶段提交后继续冷启动/长跑，T015和总目标仍active。
