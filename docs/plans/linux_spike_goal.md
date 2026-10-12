# RVISS Linux 与 Spike 连续实现目标

状态：complete（2026-10-12）。用户授权的本地Git建库、配置origin、逐任务/
阶段提交、持续实现RVISS启动固定Linux和真实Spike difftest已完成。
最终结果见[T015](t015_linux_acceptance.md)与
[可核对报告](../validation/t015_completion.json)。

## 完成条件与实际结果

- 本地Git已建立，main，origin=git@github.com:yangyue2001/RVISS.git；各阶段
  都已提交。尚未push，远端仓库内容没有发布更新。
- 固定官方Spike da2f415bb62bec3e93b33b379d787b4262aade9f，源码未修改。
  最终GC O2矩阵66293 cases /132231 events通过，含49152压缩16位编码和
  8192压缩FP编码；全GPR/FPR、44 CSR、PC/raw/priv、显式store、完整RAM
  哈希与退休比较，reset重放与4种故意差异捕获通过。报告与允许策略差异
  见[t015_final_spike.json](../validation/t015_final_spike.json)。
- 相同T002固定FW_PAYLOAD/DTB/内核/BusyBox/init/probe/workload，从OpenSBI
  进入RVISS S-mode Linux/Sv39、PID1/BusyBox；真实UART命令和用户态
  F/D、AMO/LRSC、mmap/fork/COW/wait/timer probe通过。
- 三次冷启动各100轮通过，305.574/348.680/317.949s；输入、所有计数和
  去宿主performance行后完整输出相同。一次1000轮长跑2273.973s通过，
  5909812555退休、5910514276尝试、5910536302事件、138494890564逻辑
  ticks。全部精确轮数标记、正常SBI-finisher关机、退出码0，无panic/Oops。
- 最终GC回归17 unit /25676 checks、3 smoke、CLI14通过；相关UBSan
  PMP1518检查与GC8949 cases /74887 events通过。可选O3/LTO配置也通过
  完整Spike矩阵和固定输入重放；最终Linux验收始终使用同一个GC O2二进制。
- 实际命令、预算、二进制/工件/log/input哈希及原始串口记录已保存，最终
  重新核对一致；首失败和修复证据保留在各任务记录。QEMU与RVISS报告分开。

## 实际最终命令

```sh
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t015-linux check build/t015-linux/rviss_dut
python3 tools/spike/check.py --dut build/t015-linux/rviss_dut --suite mac --csr --priv --irq --counters --pmp --sv39 --fp fd --exhaustive-c --fp-exhaustive-c --out build/t015-linux/difftest-final-exhaustive
python3 tools/linux/run_rviss.py --rviss build/t015-linux/rviss --boots 3 --rounds 100 --timeout 1800 --max-instr 4000000000
python3 tools/linux/run_rviss.py --rviss build/t015-linux/rviss --boots 1 --rounds 1000 --timeout 7200 --max-instr 20000000000
```

所有命令实际运行并退出0。GC配置、固定工件与来源见
[profile](../rv64_profile.md)和[Linux基线](../linux_boot_baseline.md)。
最后一次长跑的timeout7200s/尝试20B预算基于已测解释器成本，不更换输入、
降低轮数或放宽成功标记。客户机设备不用宿主墙钟驱动。

## Git 阶段记录

T001–T013的任务/验证/提交可沿各docs/plans/t*.md及Git历史核对。
最近阶段：F/D d319684；设备/固定Linux1轮与整机重放b83d9b9；
PMP局部性能检查点168f159；完整GC Spike矩阵981ff83；三次100轮冷启动
和已验证release配置8e6bd32。最终1000轮、原始证据与complete状态另建
完成提交，当前文档随该提交保存。

## 验收边界与后续

本次所有完成条件已满足，无未完成的授权验收项。Spike对拍是指令/架构
状态矩阵，没有对Linux整机每条指令进行Spike对拍；mcycle/cycle计时策略
排除项和设备范围见报告。SoftFloat与Spike共用上游来源，独立IEEE字面值
边界unit补充差分覆盖，不能宣称完全独立的浮点算术oracle或规范认证。
完整checkpoint恢复、NEMU/RTL共享库ABI、多hart/H、UART电气错误时序和
finisher reboot7777未验收。后续范围按用户的新请求建立契约，不能凭符号
或本次Linux/Spike结果宣称这些能力已支持。
