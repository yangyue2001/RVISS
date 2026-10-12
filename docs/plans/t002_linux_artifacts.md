# T002：固定 RV64GC Linux 启动工件

状态：verified（系统工件与 QEMU 验收；RVISS 尚未通过）。
该任务与 RVISS 的 T001 基线修复独立推进。

## 任务契约

目标：从 `/home/yang/study_dir/riscv_embedded` 的本地源码准备单 hart、
RV64GC/Sv39 Linux 6.11.4、静态 BusyBox 1.34.1 initramfs，打包为 OpenSBI
FW_PAYLOAD；在 QEMU 证明 PID 1、BusyBox 交互与重复 workload 可用。

范围：`configs/linux/`、`tools/linux/`、相关指导文档、隔离的 `build/linux/`。
原目录的 Makefile/.config/已编译产物保留，源码在本工作区复制后清理和构建。
OpenSBI 使用 git archive 固定提交，不纳入原目录的未提交补丁。

非目标：这一步不实现 RVISS 缺失的 M/A/C/F/D、Sv39、特权语义或设备，
不把 QEMU 成功记成 RVISS 成功，不增加磁盘/网络/SMP。

## 验收

- [x] 检查原目录配置：Makefile 默认为 RV32，自定义板；已有内核配置也是 RV32。
- [x] 保存 RV64GC 最小功能配置、最小 DTB、PID 1 与 BusyBox workload。
- [x] 保存独立源码快照和 SHA256；OpenSBI commit 固定为
  `69a0f0245f39ea3af4685cab4cb2dda90acd17cd`。
- [x] 构建静态 RV64GC/lp64d BusyBox，核对 ELF 静态链接和 ISA 属性。
- [x] 构建单 hart Linux，核对完整 Kconfig 与 Image header/内存区间。
- [x] 内核内置 initramfs；FW_PAYLOAD 包含内核和 DTB。
- [x] QEMU 上 3 次冷启动，各通过 PID 1、BusyBox 交互和 100 轮 workload。
- [x] QEMU 上 1 次 1000 轮 workload；记录宿主预算、日志和退出码。
- [x] 保存工件 manifest、验收报告；更新 agent 执行入口。

## 实施检查点

构建通过；命令：`python3 tools/linux/build.py --jobs 8`。
Image 2,413,976 bytes，image_size 2,576,392 bytes，FW_PAYLOAD 4,511,144 bytes。
已核对 raw 文件 0x200000 偏移的 payload 与 Image 逐字节一致、内嵌 DTB 存在、
导出的 initramfs 解压后与内核生成的 cpio 一致。
BusyBox ELF64 little-endian、RVC/double-float ABI、无 INTERP，属性为 I/M/A/F/D/C。

已处理的配置兼容问题：BusyBox 1.34.1 的 allnoconfig 会重置片段布尔值，
因此先 allnoconfig，再覆盖请求项并 oldconfig，最后逐项核对实际结果。
不通过放宽配置检查掩盖丢失选项。
Linux 配置需 EXPERT 关闭 block；NONPORTABLE 避免强制 EFI；
RISCV_ALTERNATIVE 是架构强制选入的框架，记录实际值而非声称已经关闭。
源码隔离时曾误把 vmlinux* 过滤应用于子目录，遗漏链接脚本头；
已将过滤限定为根目录构建产物并重新生成源码快照，随后内核完整构建通过。

三次冷启动命令：
`python3 tools/linux/run_qemu.py --verify --boots 3 --rounds 100 --timeout 180`。
耗时分别为 29.592 / 29.418 / 29.056 秒，退出码均为 0。
报告：[acceptance_qemu_100.json](../../build/linux/acceptance_qemu_100.json)。
三次串口日志 SHA256 相同；该事实仅覆盖当前固定输入和测试输出。

1000 轮命令：
`python3 tools/linux/run_qemu.py --verify --boots 1 --rounds 1000 --timeout 600`。
260.778 秒完成，退出码为 0，PID 1、交互和精确轮数标记均通过。
报告：[acceptance_qemu_1000.json](../../build/linux/acceptance_qemu_1000.json)。

DTB 编译日志保留三个 warning：poweroff/reboot 节点没有 simple-bus reg，
CPU interrupt provider 未声明 #address-cells。当前 dtc 和实际启动通过，
未进行完整 dt-schema 验证；不能把运行通过称为 schema 认证。

下一步：按 [Codex 任务入口](../prompts/rviss_linux_next.md) 实现 RVISS。
保持这套系统工件固定。2026-10-12 的 [T001](t001_baseline.md) 已 verified；
[T003：精确执行边界](t003_execution_boundary.md) 随后已 verified；
下一项为 [T004：最小 Spike 差分](t004_spike_minimal.md)。
