# RVISS Linux 启动基线与 Codex 执行入口

## 固定目标

最终 RVISS 验收为：真实 OpenSBI → S-mode Linux → Sv39 → 第一个用户进程
`/init`（PID 1）→ BusyBox shell，串口输入有效且可重复运行工作负载。
这一目标需要 ISA、特权、MMU、时钟、中断和 UART 同时正确。

本任务先交付可用的系统工件，用 QEMU 将工件/配置错误与 RVISS 实现错误分开。
QEMU 验收报告不表示 RVISS 已通过。完整 RV64GC 正确性还需架构测试与 Spike 对拍，
Linux 启动及这个 smoke probe 都不能替代它们。

## 源码与配置

| 项目 | 固定输入 |
| --- | --- |
| Linux | `/home/yang/study_dir/riscv_embedded/linux-6.11.4`，本地源文件快照及内容哈希 |
| BusyBox | `/home/yang/study_dir/riscv_embedded/busybox-1.34.1`，本地源文件快照及内容哈希 |
| OpenSBI | `/home/yang/study_dir/riscv_embedded/opensbi`，commit `69a0f0245f39ea3af4685cab4cb2dda90acd17cd` 的 clean archive |
| 工具链 | `/usr/bin/riscv64-linux-gnu-`；本机 GCC 11.4.0 |
| QEMU | `/home/yang/soft/qemu/qemu/build/qemu-system-riscv64`；CPU 参数保存在 manifest |
| RV64 | I/M/A/F/D/C + Zicsr/Zifencei；另外提供 Zicntr、M/S/U、Sv39 |
| 用户态 ABI | `lp64d`，静态链接，无动态加载器/共享库依赖 |
| 初期平台 | 1 hart、256 MiB RAM、CLINT、M/S PLIC contexts、8250 UART |

`configs/linux/rv64gc_min.config` 以 allnoconfig 为基底，保留 ELF、脚本、
FPU 用户上下文、initramfs、proc/sysfs/tmpfs/devtmpfs、8250 console/OF 驱动。
RISC-V Kconfig 自动选入 CPU INTC/PLIC/SBI timer。它是用于这个目标的精简配置，
不声称是形式上的最小二进制。网络、块设备、PCI、virtio、模块、SMP、V、B、
缓存块扩展与厂商扩展关闭。Linux 的普通 C 代码按内核规则使用整数 ISA，
F/D 汇编用于浮点上下文；静态 BusyBox/probe 使用 `-march=rv64gc -mabi=lp64d`。
Linux 6.11 的 PORTABLE 会选入 EFI，因此本固定板级配置启用 NONPORTABLE；
EXPERT 用于关闭 block 等默认功能。RISCV_ALTERNATIVE 被架构强制选入，
保留该框架不等于开启 V/B/缓存块等额外扩展。

GC 是非特权指令组合；能运行 Linux 还需要上述特权与平台能力。
不通过宣称 H/Sstc/Sv48 等未实现功能来规避启动问题。

原目录已存在 RV32 配置和构建产物。构建器先复制有效源文件到
`build/linux/source/`，只清理本工作区副本，再使用独立 O= 输出。
不会在原目录运行 mrproper 或改动它的 .config。Linux/BusyBox 是本地源文件快照，
不能称为已确认未修改的上游 release；快照内容摘要保存在 `sources.json`。
OpenSBI 原目录的 CSR/平台未提交修改不纳入此基线。
已有输出目录会复用首次快照；需要升级源码时换一个 `--out`，形成新基线。

## 打包与装载

```text
BusyBox + /init + probe + workload + console 设备节点
                       ↓ Linux 内置 initramfs
                 Linux arch/riscv/boot/Image
                       ↓ OpenSBI FW_PAYLOAD
             fw_payload.bin / fw_payload.elf
```

DTB 也内置到 OpenSBI。构建参数固定：

```text
PLATFORM=generic
PLATFORM_RISCV_XLEN=64
PLATFORM_RISCV_ISA=rv64imac_zicsr_zifencei
PLATFORM_RISCV_ABI=lp64
FW_TEXT_START=0x80000000
FW_PAYLOAD=y
FW_PAYLOAD_OFFSET=0x200000
FW_PAYLOAD_PATH=<artifacts>/Image
FW_FDT_PATH=<artifacts>/rviss_virt.dtb
FW_PAYLOAD_FDT_ADDR=0x8f000000
```

| 区域 | 地址/要求 |
| --- | --- |
| RAM | `0x80000000 .. 0x8fffffff` |
| OpenSBI raw image / 初始 PC | `0x80000000`；a0=0；内嵌 DTB 覆盖传入 a1 |
| Linux payload | `0x80200000`，2 MiB 对齐；检查 Image image_size 覆盖含 BSS 的有效区间 |
| 交给 Linux 的 DTB | `0x8f000000`，OpenSBI 搬移 |
| CLINT | `0x02000000`，MSWI/MTIMER，timebase 10 MHz |
| PLIC | `0x0c000000`，hart0 M context 0 / S context 1，UART source 10 |
| UART | `0x10000000`，ns16550a，1 字节寄存器步长，clock 3686400 Hz |
| test finisher | `0x00100000`，用于 SBI shutdown 的 syscon，写 0x5555 表示正常退出 |

RVISS raw loader 只需把整个 fw_payload.bin 放到 RAM 起点；包内已经有内核和
initramfs，不重复装载。ELF 模式须尊重 PT_LOAD 的 p_paddr/filesz/memsz/entry。
OpenSBI 会修补 DTB 的固件保留区。不要把整个包含 payload 的文件范围都保留为
不可用固件区，否则会排除 Linux 自己所在的内存。

内核交接 a0=hartid、a1=DTB、satp=0 的规则见
[Linux boot requirements](https://docs.kernel.org/arch/riscv/boot.html)；
内嵌 payload/FDT 参数见
[OpenSBI FW_PAYLOAD](https://github.com/riscv-software-src/opensbi/blob/master/docs/firmware/fw_payload.md)。

## 实际命令

在 RVISS 根目录运行：

```sh
python3 tools/linux/build.py --jobs 8
python3 tools/linux/run_qemu.py
```

交互终端可输入 `busybox ls /`、`cat /proc/cpuinfo`、`/bin/rviss_workload 100`。
`poweroff -f` 通过 SBI/test finisher 退出 QEMU。

自动验收：

```sh
python3 tools/linux/run_qemu.py --verify --boots 3 --rounds 100 --timeout 180
python3 tools/linux/run_qemu.py --verify --boots 1 --rounds 1000 --timeout 600
```

启动链路检查 `RVISS_PID1_OK pid=1`；通过 BusyBox 命令输入后检查
`RVISS_INTERACTIVE_OK`；每轮做 cp/cmp/grep、64 KiB 文件读写和删除，
每十轮运行用户态 probe 并 sleep 一秒。
probe 执行 F/D、AMO、LR/SC、mmap、fork/COW/wait 和计时等待。
最终检查精确轮数的 `RVISS_WORKLOAD_OK`、`RVISS_LINUX_OK` 与正常退出。
只接受独立输出行，不把串口回显命令中的标记算作通过。
超时、panic、Oops、用户态返回非零都会导致失败。

工件在 `build/linux/artifacts/`：fw_payload.bin/elf、Image、vmlinux、
BusyBox、initramfs.cpio.gz、DTB、三个完整 config 和 manifest.json。
manifest 保存来源、输入/工件哈希、工具链、ISA/ABI、地址及 CPU 参数；
构建日志、ELF 检查在 `build/linux/logs/`，验收报告在
`build/linux/acceptance_qemu_100.json` 和 `acceptance_qemu_1000.json`。
报告记录实际 QEMU 版本、CPU 命令、每次耗时、退出码和日志摘要。

2026-10-09 实测结果：构建成功，FW_PAYLOAD 为 4.302 MiB，Image 为 2.302 MiB。
三次冷启动各 100 轮均通过（29.592 / 29.418 / 29.056 宿主秒），
另一次 1000 轮通过（260.778 宿主秒），均正常退出。
详情见 [T002 验收记录](plans/t002_linux_artifacts.md)。
这证明当前工件在固定 QEMU/profile/预算上的行为；RVISS 尚未通过此验收，
后续仍需相同工件的冷启动、交互、长稳与 checkpoint 恢复证据。

## 给 Codex 的任务

本机 CLI 支持 `-C` 和 `exec`；当前 ISS 尚无 Git，自动模式加
`--skip-git-repo-check`。正常交互入口：

```sh
codex -C /home/yang/work/ISS "阅读 AGENTS.md 和 docs/prompts/rviss_linux_next.md，执行其中的当前任务，实际验证并更新任务状态。"
```

自动执行当前任务：

```sh
codex exec -C /home/yang/work/ISS --sandbox workspace-write --skip-git-repo-check "阅读 AGENTS.md 和 docs/prompts/rviss_linux_next.md，执行当前任务并保存检查点。"
```

后续每次给一个明确的可验证目标，例如：

> 按 docs/plans/ 中最新的待办契约执行下一项。最终 Linux 工件固定在
> build/linux/artifacts/，不要改变镜像和期望来掩盖模拟器错误。
> 完成本项的相关测试，更新支持状态、失败证据和下一任务。常规实现无需逐步确认。

顺序为基线 → 精确执行/异常 → 早期 Spike 对拍 → M/A/C/F/D →
M/S/U/CSR/Sv39/PMP → virt 设备/OpenSBI → 同一镜像进入 PID 1/BusyBox → 长稳/恢复。
任务大小按可验证行为划分，不机械按“三个功能”或某个文件数划分。
RVISS 的冷启动、串口交互、100/1000 轮与 checkpoint 恢复分别保存报告；
每阶段保留当前 PC/异常/设备状态与第一个差异，避免盲目修改多处后重跑到失败。
