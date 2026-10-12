# RVISS

RVISS 是 C / Makefile / Kconfig 的单 hart RV64 功能模型。目标为
RV64GC + M/S/U + Sv39、真实 OpenSBI/Linux、Spike 对拍、RTL reference、
trace 与 checkpoint；当前实现和验收范围如下。

## 当前实现与验证状态（2026-10-12）

[T001 基线](docs/plans/t001_baseline.md) 已 verified：从无旧产物的源码副本
重建 Kconfig/fixdep、CLI 和 ref 共享库；minimal 下 6 个 unit（101 checks）
及 UART/CLINT 两个 smoke 通过。ref 下同样的 unit 通过，CLI smoke 在 minimal
运行。测试通过只覆盖现有用例，不代表完整 ISA/特权一致性。

[T003 精确执行边界](docs/plans/t003_execution_boundary.md) 已 verified：
minimal/ref/debug 源码隔离构建均通过，7 个 unit 共 729 checks；minimal/debug
另通过 3 个汇编 smoke 和 14 项 CLI/UI 检查。异常不退休、失败 load/store
不提交写入；timer 使用独立逻辑时间，中断单独记录。trace 开关的计数及 UART
输出等价已验证。

[T004 最小 Spike 差分](docs/plans/t004_spike_minimal.md) 已 verified：固定官方
Spike da2f415bb62bec3e93b33b379d787b4262aade9f，29 cases / 343 events
比较完整 GPR、PC、trap/priv CSR、退休和 RAM；3 种故意差异均捕获，重复执行一致。
[T005 M 扩展](docs/plans/t005_m_extension.md) 已 verified：13 指令、配置关闭时非法，
minimal/M 各 8 unit（2394/2202 checks）、3 smoke、CLI/UI 14 checks 通过；
I/M Spike 346 cases / 4359 events 及 UBSan 通过。
[T006 A 扩展](docs/plans/t006_a_extension.md) 已 verified：22 指令、LRSC、AMO 与
精确故障；I/M/A Spike 982 cases / 9172 events、UBSan、三配置回归通过。
[T007 C 扩展](docs/plans/t007_c_extension.md) 已 verified：整数压缩、mixed 16/32、
IALIGN 与 misa.C。全部 49152 个 16 位编码与 Spike 比较通过；连同其它用例
50215 cases / 59406 events，另有三配置回归与 UBSan。
[T008 CSR/trap](docs/plans/t008_csr_trap.md) 和
[T009 中断](docs/plans/t009_interrupts.md) 已 verified：MAC/CSR/priv/IRQ
2652 cases / 13405 events，minimal/MAC 回归与 UBSan。
[T010 counter](docs/plans/t010_counters.md) verified：3249 / 15178，12 unit、
CLINT 同源 time 与 UBSan。下一项 T011 PMP，再 Sv39、F/D 和 Linux。
用户授权的 [Linux/Spike 总目标](docs/plans/linux_spike_goal.md)
仍在执行，每个验证阶段提交 Git 后继续。

| 能力 | 当前源码行为 | 验证范围 / 下一步 |
| --- | --- | --- |
| RV64I / Zifencei | 整数、访存、分支、fence/fence.i 执行函数存在；无缓存 | 位操作、部分译码、*W 运算及短汇编 smoke；完整一致性未验证 |
| Zicsr / 特权 | 基础 CSR 访问表、权限/只读检查、部分 S 视图；trap 一律进 M，mret/sret 存在，WFI 为 NOP | CSR 29 checks；委派、WARL、counter 权限与 xRET 规则待完善 |
| 执行边界 | 单步区分退休、同步 trap、中断、停机/暂停和 host error；精确 Bare fault；最近 16 个事件只读记录 | T003 回归和 T004 I/RAM/同步异常独立 Spike 差分通过；完整特权/GC 尚待扩展 |
| M | CONFIG_RV_M 开启后支持 13 条 M 2.0 指令；m_defconfig 可验证 | T005 边界/别名/除零/overflow/*W 与 Spike、UBSan 通过；misa 对应编译支持位已固定 |
| A | CONFIG_RV_A 开启后支持 LR/SC 与 9 种 AMO 的 W/D，RAM 原子；SC 明确记录是否 store | T006 aq/rl、alias、reservation、fault、Spike 与 UBSan 通过；无多 hart/DMA |
| C | RV64 整数压缩、NOP/HINT/reserved、混合取指；misa.C 可切换，增大 IALIGN 的非法写被抑制 | T007 全 16 位编码与定向/随机 Spike；关闭 C 保留 IALIGN32，D 压缩访存等 D 任务 |
| F/D | 尚无执行实现 | 待实现及独立差分 |
| MMU / PMP | RV64 集中访问规则，仅 Bare；有效非 Bare 访问返回 host error；旧直接翻译入口仍 panic，PMP/TLB 为占位 | M 访问忽略 satp、MPRV 数据访问路径已测；Sv39/PMP 未实现 |
| 设备 | UART 输出；CLINT 每 128 个平台逻辑 tick 推进 mtime；PLIC pending/claim 恒为 0 | UART、MTIP、中断独立事件及时间不受 guest counter 写入影响已测；UART 输入/PLIC/设备合法宽度待实现 |
| 镜像 / CLI | ELF64/raw 装载；--max-instr 为尝试预算，--max-events 为事件预算；si 保持尝试口径 | CLI/UI 14 checks；真实 UART/异常/中断程序 trace 开关等价；完整研究 trace 未实现 |
| checkpoint | 原生 CPU_state + 全量 RAM；不保存设备/sim/事件号/平台时间，CPU 不在校验覆盖内；明确输出限制 | cycle/reservation 改变原生结构大小，旧布局不兼容；完整恢复等价、损坏拒绝未验证 |
| difftest / 嵌入 API | 独立 C DUT 与 C++ Spike bridge 已用于真实逐事件比较；ref 共享库的多个 difftest 函数仍为空或部分实现 | T004 只验收 I/RAM/同步异常；NEMU/RTL ABI 和 rviss_create 未验收 |
| Linux 工件 | 固定 FW_PAYLOAD/Image/BusyBox/initramfs | [T002](docs/plans/t002_linux_artifacts.md) 的 QEMU 验收已通过；RVISS 尚未启动 Linux |

固定规范版本、目标参数与当前差异见 [RV64 profile](docs/rv64_profile.md)。
开发主线是 [P0–P8](docs/ai_coding_rv64gc_linux.md)；旧
[implementation_plan.md](docs/implementation_plan.md) 的 M0–M8/TODO 为历史定位，
其勾选状态不代表当前验证结果。

## 构建与测试

依赖：GNU Make 4.3+（分组生成 Kconfig parser）、GCC、bison、flex；
CLI smoke 还需 riscv64-linux-gnu-gcc 和 timeout。当前 PATH 中 gcc 可能为
ccache 包装器，验证使用显式 /usr/bin/gcc。

```sh
make CC=/usr/bin/gcc minimal_defconfig
make -j4 CC=/usr/bin/gcc check
```

产物是 build/rviss 与 build/*.test。可用 BUILD_DIR 指向独立目录，
避免 make clean 删除同在 build/ 下的 Linux 工件。

```sh
make CC=/usr/bin/gcc ref_defconfig
make -j4 CC=/usr/bin/gcc check
```

ref 产物为 build/librviss_ref.so 和 build/tests-ref/*.test；共享库链接及
unit 通过不代表 difftest consumer 可用。切回 CLI 使用 minimal_defconfig。
本次验证保持原工作区配置，ref 在隔离源码副本中构建。

```sh
./build/rviss -b --max-instr 1000000 image.elf
./build/rviss -b --max-instr 1000000 --max-events 2000000 image.elf
./build/rviss -b --memory 256M image.bin
make CC=/usr/bin/gcc debug_defconfig
make -j4 CC=/usr/bin/gcc
./build/rviss -b -t image.elf
make CC=/usr/bin/gcc compiledb
```

## 当前平台和接口限制

固定 Spike 差分入口（源码/库放 build/spike，不修改外部仓库）：

```sh
python3 tools/spike/build.py --jobs 4
make CC=/usr/bin/gcc check-diff
```

29 个 case 在新进程重复运行并保存首差异；结果位于 build/difftest/。
这个入口不验收 NEMU/RTL 共享库 ABI。

minimal 板只映射以下地址，默认 RAM 为 128 MiB、复位 PC 为 0x80000000。
Linux 的目标 RAM 为 256 MiB；完整 virt 装配仍待实现。

| 设备 | 当前基址 |
| --- | --- |
| UART16550 | 0x10000000 |
| CLINT | 0x02000000 |
| PLIC 骨架 | 0x0c000000 |

没有 UARTLite 或其它 NEMU 地址别名。include/rviss.h 的结构/符号声明不能作为
ABI 验收证据：当前 difftest_init 未完成整机初始化，set_ramsize 忽略参数，
regcpy 仅传 GPR/PC/priv，csrcpy 使用本项目结构而非 NEMU 的 4096-CSR 数组；
store_commit 无定义，status 恒为 0。需要独立契约测试和真实 consumer smoke。

任务入口见 [rviss_linux_next.md](docs/prompts/rviss_linux_next.md)。
验证命令、退出码、日志及修改前备份保存在 build/baseline/t001/，
[T001](docs/plans/t001_baseline.md) 记录具体证据与未验证项。
T003 的命令/结果和开关 trace 比较见 build/baseline/t003/results.json、verify.py
及 [T003 任务记录](docs/plans/t003_execution_boundary.md)。
