# T015：固定 Linux 冷启动与长跑验收

状态：verified（2026-10-12），依赖 T014 verified（b83d9b9）。

最终 GC O2 构建在固定输入上通过全部验收：三次冷启动各100轮，耗时
305.574 /348.680 /317.949s；一次1000轮长跑耗时2273.973s，退出码0，
精确轮数/PID1/BusyBox真实UART输入/probe/正常SBI-finisher关机全部通过。
1000轮退休5909812555、尝试5910514276、事件5910536302、逻辑ticks
138494890564；WFI idle advances10406、ticks132584354262。宿主耗时只用于
性能记录，不驱动客户机设备时间。无panic/Oops，未改镜像、标记或轮数。

完整固定 Spike 矩阵66293 cases /132231 events、reset重放与4种故意差异
捕获通过；17 unit /25676 checks、3 smoke、CLI14与相关UBSan通过。
最终二进制、Spike bridge、所有Linux工件和原始log/input的哈希重新核对一致。
证据为 [汇总](../validation/t015_completion.json)、
[100轮](../validation/t015_linux_100.json)、
[1000轮](../validation/t015_linux_1000.json)、
[Spike](../validation/t015_final_spike.json)。原始串口日志与输入事件在
`docs/validation/t015_linux_logs/`，工件manifest保存在t015_linux_manifest.json。

本任务及用户的Linux/Spike总目标complete。Spike比较范围是指令/架构状态
矩阵，没有对Linux整机执行每条指令进行Spike对拍；完整checkpoint恢复、
NEMU/RTL共享库ABI、多hart/H、UART电气错误时序、finisher reboot7777未验收。
最终报告和状态更新创建完成提交；此前阶段提交168f159、981ff83、8e6bd32。

## 契约与历史检查点

以下active/待运行文字记录各检查点当时的状态，最终状态以上述结果为准。

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

实际最终验收已启动（保持build/t015-linux二进制不变）：
- `python3 tools/linux/run_rviss.py --rviss build/t015-linux/rviss --boots 3 --rounds 100 --timeout 1800 --max-instr 4000000000`
- `python3 tools/linux/run_rviss.py --rviss build/t015-linux/rviss --boots 1 --rounds 1000 --timeout 7200 --max-instr 20000000000`
- `python3 tools/spike/check.py --dut build/t015-linux/rviss_dut --suite mac --csr --priv --irq --counters --pmp --sv39 --fp fd --exhaustive-c --fp-exhaustive-c --out build/t015-linux/difftest-final-exhaustive`

指令/宿主预算基于T014完整链路实际成本扩大，固定工件、用户态代码、轮数、
标记规则和返回码要求未改。结果未完成前不能verified。

长跑期间测得100/1000各已执行额外60轮的probe；基于成本使用现有Kconfig
O3/LTO选项评估同一GC profile的release构建（不开host native、不改源语义）。
configs/gc_release_defconfig只改变优化方式；先完整make check/固定输入重放/
Spike，再依据实际耗时选择是否用于后续验收。正在运行的O2测试仍保留原
二进制，未经验证不替换其工件或认定优化有效。

O2第一冷启动100轮已通过（305.574s，769073480次尝试，769076065events，
15230890564逻辑ticks）；第二/三次与1000长跑继续。O3/LTO相同profile的
17unit/25676checks、3smoke/CLI14已通过，正在完整66293-case Spike与
固定输入整机重放；选择最终构建以实际结果为准。

O3/LTO完整输入重放通过，203680985次尝试/全部计数/去performance行后输出
与O2相同，78.551s。短10M实际O2 3.687s、O3/LTO3.240s；完整重放相对
O2 82.284s仅约5%额外收益。保持正在验收的GC O2构建，O3/LTO作为可
复现可选配置/验证结果保留；最终100/1000不切换二进制。

最终GC O2完整Spike矩阵verified：66293 cases /132231events，包含所有49152
压缩16位编码和8192压缩FP编码，44 CSR/全GPR/FPR/store/RAM/退休；独立
进程reset重放与4种故意差异捕获通过。DUT/ref哈希固定，记录
t015_final_spike.json。第二100轮冷启动也通过（348.680s），计数/输入记录
与第一轮完全相同。第三100轮和1000轮长跑仍运行，不能标总目标complete。

冷启动检查点verified：3次各100轮，305.574/348.680/317.949宿主秒。
全部PID1/probe/BusyBox输入/精确100轮/正常SBI关机通过；三次输入文件
与所有计数相同，去宿主performance行后完整输出相同。每次769073480
次尝试、769076065events、15230890564逻辑ticks。证据t015_linux_100.json
和t015_cold_checkpoint.json。O3/LTO完整Spike66293/132231也通过，记录
t015_release_spike.json。1000轮仍运行，总目标/T015仍active。
