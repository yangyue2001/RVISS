# T001：恢复 RVISS 可复现基线

状态：verified（2026-10-12）。仅基线构建与现有 unit/smoke；
不表示完整 RV64GC、特权语义、Linux 或 difftest ABI 已验证。

## 任务契约

目标：从当前源码构建 CLI/ref，实际运行现有测试，修复阻塞构建/运行的公共头、
直接依赖与测试 runner，固定工具/profile 并更新真实支持状态。
范围为 include/cpu 公共定义、Makefile/scripts/configs 的直接构建问题及任务文档。
不实现 M/A/C/F/D、Sv39/Linux、Spike bridge、新 checkpoint 或变更公共 difftest ABI。
路线采用 [P0–P8](../ai_coding_rv64gc_linux.md)；旧 M0–M8/TODO 仅作历史定位。

## 首个失败及修复

2026-10-09 的审查：PATH gcc 经 ccache 在 /run/user/1000/ccache-tmp 失败；
显式 GCC 首先缺 cpu/interrupt.h，未能运行测试。
2026-10-12 的重确认命令：
make BUILD_DIR=/tmp/rviss-t001-baseline CC=/usr/bin/gcc check。
退出 2，首个路径为 src/machine/machine.c → cpu.h → csr_list.h →
缺 cpu/interrupt.h；[原始首差异摘录](../../build/baseline/t001/baseline-first-failure.log)。

本次修复：

- 新增 cpu/interrupt.h：按 Priv-v1.12 定义 M/S/U cause、IRQ 编号和 RV64 IRQ_BIT；
  公开板级 cpu_irq_input 声明。设备仍只接 IrqLine，不读取 CPU CSR。
- csr_machine.h 与 csr_list.h 原来共用保护宏，造成互相屏蔽；给 machine 独立保护宏，
  csr_list 直接包含唯一的 MSTATUS 位定义。CSR 地址/存储清单仍在 csr_list，
  不重复枚举、不扩展支持位或改变 CSR 语义。
- fixdep 的依赖从 app 挂到实际对象，check/并行首次构建均可用；
  子工具固定自己的 BUILD_DIR，避免主工程隔离目录传递导致产物路径错误。
- ref 配置原先把 unit 用 -shared 链接，首个 test_bits 执行段错误，make 返回 2：
  [失败日志](../../build/baseline/t001/ref-check-first.log)。unit 现用可执行链接，
  ref 测试放 tests-ref，check 必须构建共享库，并明确 CLI smoke 在 minimal 运行。
- unit 纳入生成的头/配置依赖；保留失败退出传播和全部原期望。
  Kconfig parser 使用 GNU Make 4.3 的分组目标，避免 -j 下 bison 同时写两次输出；
  [原并行日志](../../build/baseline/t001/parallel-yacc-first.log) 可见两次 YACC，修复后一次。

## 验收结果

- [x] 搜索直接调用方、现有源码/配置/测试；恢复缺失公共头。
- [x] 核对 CSR 定义所有权；4 个公共头单独包含、两种包含顺序均通过。
- [x] 原工作区 make CC=/usr/bin/gcc check，退出 0。
- [x] 无 .config / 生成头 / build / 旧工具的源码副本重建 Kconfig/fixdep，
  minimal_defconfig 后完整 -j4 check，退出 0。
- [x] minimal 6 个 unit、101 checks 全过；2 个 smoke 实际编译并执行。
  旧审查记录的“7 个 C 测试”误计了 test.h，实际 C main 是 6 个。
- [x] ref_defconfig 后全量共享库构建和同样 101 checks，退出 0；
  file 确认 so 是共享对象、unit 是 PIE 可执行文件；nm 只记录符号事实。
- [x] 失败 unit 返回 17 → make 返回 2；缺失交叉编译器 → make 返回 2，
  未输出 smoke 通过。没有修改/跳过原测试。
- [x] 更新 README 支持矩阵；固定 [profile v1](../rv64_profile.md) 和工具版本。
- [x] 记录 Spike 来源未确认及下一项 [T003/P1](t003_execution_boundary.md)。
- [x] 修改前源码归档保留在 build/baseline/t001/source-before.tar.gz；
  原 .config 和 Linux 工件保持，未使用旧可执行文件替代源码重建。

初始归档覆盖核心/include/scripts/configs 与当时所读任务文档；
tools/kconfig/Makefile 的修改前原文件另存为 kconfig-Makefile-before。
最终源码/文档归档为 source-after.tar.gz，摘要见 summary.json；这些不是 Git 历史。

| unit | checks | failures |
| --- | --- | --- |
| test_bits | 13 | 0 |
| test_bus | 13 | 0 |
| test_csr | 29 | 0 |
| test_decode | 24 | 0 |
| test_intr | 9 | 0 |
| test_rv64w | 13 | 0 |
| 合计 | 101 | 0 |

UART smoke 输出 Hello from RVISS! 并 HIT GOOD TRAP，退出 0；
CLINT smoke 在一次 handler 内 HIT GOOD TRAP，退出 0。
分别报告当前计数 97 / 1034（这些尚不是精确退休数）。IRQ 程序成功路径在 handler
直接退出，并没有执行 mret；不能根据旧注释宣称 mret 已被此 smoke 验证。

## 实际命令与证据

统一记录：[results.json](../../build/baseline/t001/results.json) 包含命令参数、cwd、
退出码、期望退出码与日志名；[hashes.json](../../build/baseline/t001/hashes.json)
记录配置/可执行文件 SHA256。隔离源码路径为 /tmp/rviss-t001-final-w9ag2bda。

实际完整验证脚本：python3 build/baseline/t001/verify.py。
其中执行的主要命令为（RVISS 根目录的原配置未切换）：

```sh
# cwd=/tmp/rviss-t001-final-w9ag2bda，无旧配置/工具/对象
make -j4 CC=/usr/bin/gcc BUILD_DIR=/tmp/rviss-t001-final-w9ag2bda/out minimal_defconfig
# 删除副本中刚构建的 fixdep，验证 check 自己的直接依赖
make -j4 CC=/usr/bin/gcc BUILD_DIR=/tmp/rviss-t001-final-w9ag2bda/out check
make -j4 CC=/usr/bin/gcc BUILD_DIR=/tmp/rviss-t001-final-w9ag2bda/out ref_defconfig
make -j4 CC=/usr/bin/gcc BUILD_DIR=/tmp/rviss-t001-final-w9ag2bda/out check
# cwd=/home/yang/work/ISS
make CC=/usr/bin/gcc check
```

均退出 0。header 使用 /usr/bin/gcc -std=gnu11 -Wall -Wextra -Werror
-fsyntax-only 单独编译，无警告。验证日志：
[clean check](../../build/baseline/t001/final-clean-check.log)、
[ref check](../../build/baseline/t001/final-ref-check.log)、
[workspace check](../../build/baseline/t001/workspace-check.log)。
还按原 run.sh 的参数保存 ELF 和完整 stdout：
[UART](../../build/baseline/t001/smoke-smoke-run.log)、
[CLINT](../../build/baseline/t001/smoke-irq-run.log)，命令见 results.json。

负例日志：
[unit failure](../../build/baseline/t001/unit-failure-propagation.log)、
[cross failure](../../build/baseline/t001/cross-failure-propagation.log)。
第一次 unit 负例的临时文件漏 .test 后缀，被 -include 当作依赖文件读取，
它只验证了解析失败，不作为 runner 证据；修正后确认实际执行并返回 Error 1。
无效探针保留为 unit-probe-invalid-first.log。

工具实测：/usr/bin/gcc 与 riscv64-linux-gnu-gcc 均 11.4.0，GNU Make 4.3，
bison 3.8.2，flex 2.6.4。smoke 为 rv64im/lp64，成功不表示 M 已实现。
默认 gcc/ccache 未重测；本次所有 C 构建显式使用 /usr/bin/gcc。

## 未验证项与下一步

现有检查没有覆盖完整 I/Zicsr/特权语义、trace 等价、checkpoint 恢复、
NEMU/RTL ABI、Spike 对拍或 RVISS Linux；ref 配置不运行 CLI smoke。
menuconfig 的 curses 前端、其它配置/工具链和 sanitizer 未运行。
只读 .git 占位目录不是可用仓库；未初始化 Git，备份不等于 Git 历史。
Linux 的 QEMU 证据继续独立属于 T002，本次没有重跑 QEMU 或 Linux 构建。

README 已移除 M/A/C/Sv39/PMP、UARTLite/地址别名、稀疏完整 checkpoint、
NEMU ABI/嵌入 API 可用等与源码冲突的声明。旧 architecture/coding_style 的
“engine 与 target 互不认识”“JIT target 不改”不作为约束；实际 engine 调用
RV64 decoder/trap，主线仅做 RV64 解释器。编译当前为 -Wall/-Werror 和 GCC 默认
C dialect，不把 coding_style 写的 -Wextra/gnu11 当作全工程已启用事实。

已为下一项执行真实诊断：非法编码错误增加 instret；未映射 ld（含 x0）没有异常，
ld x5 错写 0；未映射取指误报 illegal；停机返回少 1。
[诊断源码](../../build/baseline/t001/p1_probe.c) 和
[输出](../../build/baseline/t001/p1-probe-run.log) 交给 T003，不在 T001 修复语义。
该探针首次错误 glob 原 build/ 目录中的重命名前旧对象，产生重复符号；
失败保存在 p1-probe-invalid-link.log，随后改用全新副本对象成功执行。
该诊断返回 0 不代表语义通过，也不是 make check 的失败。

当前检查点：T001 已 verified；无本次构建回归，核心语义缺口保持明确。
下一步只执行 [T003](t003_execution_boundary.md)，再进入 P2 最小 Spike 差分。
上述是 T001 交付时的检查点；随后 T003 已 verified，最新下一项为
[T004](t004_spike_minimal.md)。T001 的原命令/结果保持为历史证据。
