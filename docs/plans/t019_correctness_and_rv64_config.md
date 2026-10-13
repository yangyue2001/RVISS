# T019：核心边界加固与 RV64 Linux/OpenSBI 配置

状态：active（2026-10-13；配置、镜像边界、AMO 和 checkpoint 切片已完成）。

## 目标

在保留当前 RV64 单 hart 功能模型行为的前提下，修复已经确认的高风险边界，
并让 RVISS 的 Kconfig 能直接表达固定 Linux/OpenSBI RV64GC 平台。

## 修改范围

### A. 配置契约

- 新增 `RV64_LINUX` 配置预设，只面向 RV64，不建立 RV32 或通用 ISA 抽象。
- 预设选择 Linux 6.11.4 的 `ARCH_RV64I + MMU + FPU + C + SMP=n` 对应能力：
  RV64 I/M/A/F/D/C、Zicsr、PMP、Sv39，以及 S/U 和现有计数器/栅栏实现。
- 预设选择 OpenSBI generic 使用的 UART8250、CLINT MTIMER/MSWI、PLIC 和
  SYSCON test-finisher 对应设备。
- 新增 `linux_rv64gc_defconfig`，RAM 默认 256 MiB，与固定 DTB 一致。
- 不新增 RV32、Sv48、V、H、Sstc 或其它未实现能力。

### B. 镜像与内存边界

- ELF loader 检查 program header、文件段范围、`p_filesz/p_memsz` 关系和地址溢出。
- 统一使用可表达 `size_t` 的范围检查，禁止错误的窄化转换。
- 保持 raw/ELF 单次读文件行为。

### C. 原子访问正确性

- AMO 必须同时通过读和写权限检查，并继续限制为 RAM 原子访问。
- LR/SC 的既有 reservation 和故障提交行为保持不变。

### D. checkpoint 校验边界

- 校验覆盖固定头部、CPU 状态和 RAM，避免 CPU 状态损坏后校验仍通过。
- 不在本阶段伪装支持设备/逻辑时间/事件序号的完整恢复；格式版本递增并明确限制。

### E. ABI 诚实性与后续边界

- 先记录 `rviss.h` 原生 API、`difftest_store_commit` 和空 difftest hook 的现状。
- 本阶段不凭空实现多实例 `rviss_t` 或未经 consumer 契约定义的 NEMU store-log 行为。
- 增加检查，避免将“符号存在”误认为 ABI 已可用；完整 ABI 另立任务。

## 非目标

- 不实现 RV32、多 hart、Sv48、TLB、V/H、完整 checkpoint 恢复或新的设备。
- 不修改固定 Linux/OpenSBI 输入工件，不修改 Spike 参考源码和期望结果。
- 不把旧架构文档的大规模重写混入核心语义修复；只在本任务记录当前配置来源和差异。

## 验收

1. `make CC=/usr/bin/gcc linux_rv64gc_defconfig` 生成的配置包含固定 RV64GC/Sv39/
   PMP/设备能力，且不出现 RV32/Sv48/V/H 选项。
2. `make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t019-linux check` 通过。
3. `make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t019-gc check` 通过，作为既有 GC 回归。
4. 新增边界测试覆盖：坏 ELF 段、地址溢出、CPU checkpoint 损坏、AMO 读权限失败。
5. 运行一个固定 Linux 启动 smoke，确认新的 Linux 配置与现有 256 MiB DTB 地址图一致。
6. 每个完成的切片更新本文件状态和实际命令，并建立独立 Git 提交。

## 当前已知限制

- `include/rviss.h` 中的原生 `rviss_*` API 仍没有完整实现；共享库 difftest 的多个
  高级 hook 仍是骨架，不能因本任务的回归通过而宣称 ABI 完整。
- checkpoint 当前仍不保存设备和模拟器运行时状态；本任务只修复校验边界并提高失败可见性。

## 配置切片验证

- `make CC=/usr/bin/gcc linux_rv64gc_defconfig` 成功生成 `CONFIG_RV64_LINUX=y`、
  `CONFIG_MSIZE=0x10000000`、UART/CLINT/PLIC/test-finisher、M/A/F/D/C、PMP 和
  Sv39；配置中没有 RV32、Sv48、V、H 或 Sstc 能力。
- `make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t019-config check`：17 个单元、25,676
  项检查，3 个 smoke 和 14 个 CLI/UI 检查全部通过。

## 边界切片验证

- ELF loader 现在先验证完整 program-header 表和所有 `PT_LOAD` 段，再写入 RAM；检查
  `p_offset + p_filesz`、`p_filesz <= p_memsz`、RAM 范围和地址溢出，raw/ELF 仍各只读
  一次文件。`tests/unit/test_image.c` 覆盖坏 magic、截断/错端序 ELF、坏段和部分装载，
  108 项检查通过。
- `isa_mem_prepare_atomic` 先走读权限再走写权限，AMO 仍强制 RAM-only，并把读路径失败
  映射为 store/AMO fault；LR/SC 保持原路径。A/Sv39 定向回归和 `test_sv39` 的 12 项
  新增检查通过。
- checkpoint 升级到版本 2，校验覆盖固定头、`CPU_state` 和 RAM；加载时先在临时映射
  中校验，坏 CPU、坏 RAM、截断文件都不会改写当前 CPU/RAM。`test_checkpoint` 的 48
  项检查通过；设备、平台时间和事件序号仍明确不在恢复范围内。
- `make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t019-config check`：19 个单元、25,844
  项检查，3 个 smoke 和 14 个 CLI/UI 检查全部通过。
- 切换到 `linux_rv64gc_defconfig` 后，原验收命令
  `make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t019-linux check` 同样通过：19 个单元、
  25,844 项检查，3 个 smoke 和 14 个 CLI/UI 检查全部通过；随后已恢复
  `gc_defconfig` 作为工作区默认配置。
- `make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t019-gc check`：同一 19 个单元、25,844
  项检查，3 个 smoke 和 14 个 CLI/UI 检查全部通过。
- 固定工件 Linux smoke：
  `python3 tools/linux/run_rviss.py --rviss build/t019-config/rviss --out build/linux
  --boots 1 --rounds 1 --timeout 180 --max-instr 4000000000` 通过；PID1、BusyBox
  交互、RV64/FP/AMO/Sv39/timer probe 和 finisher shutdown 均通过，退出码 0，
  退休 203,623,674，日志 SHA256 为
  `f6e076c469f55eb76b9ce2a2a793f4338017c6d5bc29ac38aa94d4176835f3af`。

## ABI 现状记录

- `make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t019-share CONFIG_SHARE=y` 成功生成
  `build/t019-share/librviss_ref.so`，但 `nm -D --defined-only` 显示它只导出当前
  difftest 骨架符号；`include/rviss.h` 声明的 `rviss_create`、`rviss_step` 等原生
  API 没有实现，`difftest_store_commit` 也没有实现。没有把共享库“能链接”当成 ABI
  验收通过。
- `difftest_uarchstatus_*`、`update_dynamic_config` 和 `difftest_guided_exec` 当前仍是
  空 hook；它们保留在后续独立 ABI 契约任务，不能作为本切片的完成条件。

## AMO 差分回归

- `make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t019-gc check-diff DIFF_SUITE=a` 通过：
  Spike 对拍 665 cases / 5,156 events，且 3 种故意注入差异均被捕获。
