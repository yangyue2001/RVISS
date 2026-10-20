# RVISS 目录与文件说明

2026-10-12 T003 更新：当前执行接口为 include/cpu/step.h 的 StepResult，
target/riscv64/step.c 负责单个架构事件，engine/interpreter/interp.c 负责预算、
计数、设备逻辑时间和最近事件副本。访存返回 MemAccess，RV64 的有效特权/对齐/
故障归类集中在 mmu/access.c。下文旧“只用 RTL 对话/JIT 不改 target”及退休数
驱动 mtime 的说法已经过时；当前只做 RV64 解释器，准确契约见
[T003](plans/t003_execution_boundary.md) 和 [profile](rv64_profile.md)。

T015当前源码：`instr/m,a,c,fp`已实现，FPR/FCSR及SoftFloat C算术位于target/
third_party，空rtl_fp.c已移除。集中访问路径含16-entry PMP与Sv39硬件A/D；
CSR与trap支持M/S/U委派/返回/IRQ/counter。板级装配UART→PLIC source10→
M/S输出、CLINT与test finisher；设备不读CPU CSR。MMIO接受规则在副作用前
检查，legal WFI可按设备deadline推进逻辑时间，事件/退休/guest counters独立。
`host/uart_input.c`按event记录/重放输入；host wallclock仅做性能统计。
`tools/spike`是固定独立C DUT/C++参考进程，已经实际执行GC矩阵；对外
`rviss.h`与原生checkpoint仍有未验收部分，不能据符号认定ABI完成。
下文旧目录树与路线描述作为历史记录；当前行为与边界以源码及T004–T015
验收证据为准。

> 本文回答两个问题：**目录为什么这样分**、**每个文件负责什么**。
> 实现路线见 [implementation_plan.md](implementation_plan.md)。

---

## 一、设计原则（所有目录划分的依据）

1. **引擎 / 客体 ISA 分离**
   指令语义写成引擎无关的 `rtl_*` 原语；`target/riscv64` 只说"做什么"，
   `engine/interpreter` 只管"怎么做"。将来加 JIT 引擎时 target 一行不改。

2. **公开接口 vs 私有头**，判据只有一条：*这个头会不会被别的子系统 include？*
   - 会 → `include/<模块>/xxx.h`
   - 不会 → 放在模块目录里（如 `src/machine/machine.h`、`src/target/riscv64/cpu_state.h`）

3. **只有 RV64，不做 ISA 抽象**
   没有 `src/isa/<isa>/` 这一层，也没有 `MUXDEF(CONFIG_ISA64,...)` 这类位宽分支。
   `CPU_state` 直接就是 RV64 的状态（放在 `include/cpu/cpu.h`，因为引擎要读 `pc`）。

4. **一条指令的元信息只写一遍**
   `instr.def`（X-macro）是唯一事实来源，同时生成执行函数声明、译码表、助记符。
   对比 NEMU：加一条指令要改 `isa-all-instr.h` + `decode.h` + `exec.h` 三处。

5. **确定性**
   客户机时间（CLINT mtime）由指令条数驱动，不读宿主时钟；宿主相关的系统调用
   全部收敛在 `src/host/`。同一镜像两次运行结果完全一致（checkpoint/difftest 的前提）。

6. **可测试**
   `tests/` 从第一天就在：单元测试直接链接核心库，冒烟测试跑真实汇编程序。

---

## 二、目录树

```
ISS/
├── Makefile                ★ 构建入口（自定位 RVISS_HOME，不依赖外部环境变量）
├── Kconfig                 ★ 顶层 Kconfig，source 各子系统
├── README.md               项目介绍（含当前实现状态）
├── configs/                预设配置：minimal / debug / ref 的 _defconfig
├── scripts/
│   ├── config.mk           Kconfig 目标：menuconfig / %defconfig / savedefconfig / distclean
│   └── build.mk            通用编译规则（主工程与 tools 共用，含 fixdep 增量依赖）
├── tools/                  构建工具（移植自 Linux/NEMU 的 kconfig 与 fixdep，GPL-2.0）
│   ├── kconfig/            conf（非交互）与 mconf（menuconfig 界面）前端
│   └── fixdep/             把 .d 依赖精确到"这个文件用到了哪些 CONFIG_"
├── docs/
│   ├── architecture.md     ← 本文
│   ├── implementation_plan.md  实现方案与里程碑
│   ├── trace_and_uarch_plan.md trace 与微架构结构的架构规划
│   ├── device_model_review.md  外设系统解耦评估（对照 QEMU）与迁移步骤
│   └── coding_style.md     编码规范（依赖规则、命名、错误处理…）
│
├── include/                【跨模块接口】只放会被多个子系统 include 的头
│   ├── rviss.h             对外 API + difftest ABI（嵌入/被 dlopen 的稳定接口）
│   ├── common.h            基础聚合头（被上面四个子头组成）
│   ├── common/types.h      word_t / vaddr_t / paddr_t / reg_t
│   ├── common/bits.h       BITS / BIT / SEXT
│   ├── common/macro.h      str / CONCAT / likely / unlikely
│   ├── common/assert.h     panic / Assert（内部 bug 的出口，noreturn）
│   ├── cpu/cpu.h           ★ 体系结构状态 CPU_state + cpu_reset + cpu_raise_trap
│   ├── cpu/csr-list.h      ★ CSR 清单（唯一事实来源）：地址/掩码 + 生成存储与访问表
│   ├── cpu/interrupt.h     异常号 EX_* 与中断号 IRQ_*（设备层也用，故放公共区）
│   ├── cpu/decode.h        Decode / Operand / InstrEntry（引擎与 target 共享）
│   ├── rtl/rtl.h           ★ 引擎与 target 的唯一契约：rtl_add/rtl_lm/rtl_j ...
│   ├── util/log.h          分类(category)+分级(level)日志宏
│   ├── util/state.h        仿真器运行状态 SimState（HALTED/STOPPED...）
│   ├── util/config.h       运行期配置 RuntimeConfig + 命令行解析
│   ├── host/host.h         宿主抽象：时间、匿名内存、读文件
│   ├── memory/paddr.h      物理地址：RAM + MMIO 分发
│   ├── memory/vaddr.h      虚拟地址访问 + isa_mmu_* 后端钩子
│   ├── memory/image.h      镜像加载（ELF / raw 自动探测）
│   ├── device/             （与 src/device 同构：框架在顶层，设备按类别分目录）
│   │   ├── device.h        框架：MmioRegion（不含地址）+ bus_init/mmio_map + devices_tick
│   │   ├── irq.h           中断线对象 IrqLine（设备只 raise/lower，接线由板子做）
│   │   ├── char/uart16550.h   各设备的公开接口（实例结构体 + init/reset）
│   │   ├── timer/clint.h      只被板级代码包含；守护宏含完整路径
│   │   └── intc/plic.h        （RVISS_DEVICE_CHAR_UART16550_H 等）
│   ├── monitor/monitor.h   monitor_init / monitor_run
│   ├── difftest/difftest.h difftest 内部辅助接口（对外 ABI 在 rviss.h）
│   └── checkpoint/checkpoint.h  cpt_save / cpt_load
│
├── src/
│   ├── main.c              入口：monitor_init → monitor_run → 退出码
│   │
│   ├── util/               基础设施实现
│   │   ├── log.c           日志实现（等级过滤 + 分类开关）
│   │   ├── state.c         SimState 与退出码换算
│   │   ├── panic.c         rviss_panic（内部错误）
│   │   └── config.c        命令行解析（--max-instr/--memory/--log/-t...）
│   ├── host/host_time.c    宿主调用实现（gettimeofday/mmap/读文件）
│   │
│   ├── engine/             【引擎】= 指令怎么执行
│   │   ├── engine.h        引擎接口：engine_init / engine_exec / engine_flush
│   │   └── interpreter/
│   │       ├── interp.c    ★ 解释器主循环：取指→译码→cpu.pc=snpc→exec→中断轮询
│   │       ├── rtl_basic.c RTL 原语实现（= 客户机指令在宿主上的落地方式）
│   │       └── rtl_fp.c    浮点后端占位（M5 二选一：softfloat / host FPU）
│   │
│   ├── target/riscv64/     【客体 ISA】= 指令是什么语义
│   │   ├── Kconfig         RISC-V 扩展开关（Zicsr 已实现；M/A/F/D/C/V/H 为规划项）
│   │   ├── cpu.c           cpu_reset / cpu_priv_name / 寄存器名
│   │   ├── cpu_state.h     转发头 → include/cpu/cpu.h（保持 target 内部书写习惯）
│   │   ├── decode.c        ★ 取指 + 扁平表译码 + 操作数填充（pattern_decode）
│   │   ├── local_include/  target 私有头（对外不可见）
│   │   │   ├── encoding.h  指令位域与 I/S/B/U/J 立即数、加载扩展方式
│   │   │   ├── intr.h      陷阱接口（riscv_raise_trap / mret / sret）
│   │   │   ├── csr.h       CSR 地址与位、csr_read/csr_write/csr_execute
│   │   │   └── rtl.h       RV64 私有 RTL 组合原语（*W 字运算，含易错点说明）
│   │   ├── instr/
│   │   │   ├── i/instr.def       RV64I 指令表（模式 + 操作数格式）
│   │   │   ├── i/exec.c          RV64I 指令语义（用 rtl_* 写）
│   │   │   ├── system/instr.def  特权指令表（ecall/mret/csr*/sfence/nemu_trap...）
│   │   │   └── system/exec.c     特权指令语义
│   │   ├── csr/csr_access.c      CSR 表驱动访问（含特权检查与写后副作用）
│   │   ├── mmu/
│   │   │   ├── mmu.c       地址翻译（当前直通；Sv39/48 见 TODO）
│   │   │   ├── tlb.c       TLB 占位（翻译缓存）
│   │   │   └── pmp.c       PMP 占位（物理内存保护）
│   │   └── trap/
│   │       ├── trap.c      ★ riscv_raise_trap（统一陷阱入口）+ mret/sret
│   │       └── intr.c      中断查询：设备线 → mip → mie/mstatus 判定
│   │
│   ├── memory/             【物理与虚拟内存】不认识 RISC-V
│   │   ├── paddr.c         RAM 分配 + MMIO 分发 + 小端读写
│   │   ├── vaddr.c         翻译 → 物理访问（唯一调用 isa_mmu_* 的地方）
│   │   └── image.c         ELF64 / 裸镜像加载
│   │
│   ├── device/             【设备】只认识 device.h；不认识 ISA、不知道自己的地址
│   │   ├── Kconfig         设备开关：HAS_CLINT / HAS_UART16550 / HAS_PLIC
│   │   ├── bus.c           设备框架：映射表 + 地址冲突检查 + 设备节拍注册表（总是编入）
│   │   ├── char/           字符设备  （QEMU: hw/char/）
│   │   │   └── uart16550.c Uart16550State：串口（写 THR → stdout）
│   │   ├── timer/          定时器    （QEMU: hw/timer/）
│   │   │   └── clint.c     ClintState：mtime/mtimecmp/msip（指令数驱动的确定性时钟）
│   │   └── intc/           中断控制器（QEMU: hw/intc/）
│   │       └── plic.c      PlicState：极简 PLIC（enable/pending/threshold/claim）
│   │
│   ├── machine/            【整机装配】唯一知道"这台机器有什么"的地方
│   │   ├── Kconfig         机器参数：MBASE / MSIZE / PC_RESET_OFFSET
│   │   ├── machine.h       MachineConfig（内存图/设备地址/复位向量）+ machine_get
│   │   ├── machine.c       按配置建内存、装设备（受 CONFIG_HAS_* 控制）、复位 CPU
│   │   └── boards/minimal.c 最小板（内存图取自 Kconfig，设备地址 QEMU virt 风格）
│   │
│   ├── monitor/            【人机接口】
│   │   ├── monitor.c       流程编排：配置→机器→镜像→运行→统计
│   │   └── ui.c            交互调试器：c / si [n] / info r / x ADDR / q
│   │
│   ├── difftest/difftest_api.c  NEMU 兼容的 difftest_* ABI 实现（librviss_ref.so）
│   └── checkpoint/checkpoint.c  checkpoint 存取（头部+校验和+整块 RAM，稀疏化见 TODO）
│
└── tests/
    ├── (无构建文件)         测试由顶层 Makefile 的 check 目标驱动
    ├── unit/test.h         极简单测框架（CHECK/CHECK_EQ）
    ├── unit/test_bits.c    位操作单测
    ├── unit/test_decode.c  译码单测（真实走取指+译码路径）
    ├── unit/test_bus.c     总线单测（地址冲突/压 RAM 必须被拒绝）
    ├── unit/test_intr.c    中断线单测（拉高/拉低、mip 不粘滞）
    ├── unit/test_rv64w.c   *W 字运算单测（移位量 &31、算术右移、符号扩展）
    ├── smoke/start.S       冒烟程序：UART 打印 + nemu_trap
    ├── smoke/irq.S         中断程序：CLINT 定时器中断 → 陷阱 → mret
    ├── smoke/link.ld       钉在 0x80000000 的链接脚本
    └── smoke/run.sh        编译→运行→校验输出与退出码
```

---

## 三、依赖方向（只允许箭头向下）

```
        common (types/bits/macro/assert)
            │
        util / host
            │
        memory ── device          ← 都不认识 RISC-V
            │        │
        rtl(接口) ← engine(实现)    ← 两者互不认识，只通过 rtl.h 对话
            │
        target/riscv64            ← 只 include common/util/memory/rtl
            │
        machine → monitor → main
            │
        difftest / checkpoint     ← 高层工具，谁都可以调
```

唯一允许的"下层反调上层"：`memory/vaddr.c` 调用 target 提供的
`isa_mmu_translate()`（后端钩子，等价于 NEMU 的 `include/isa.h`），
这是"ISA 后端接口"的标准形态，已在该头里注明。

`src/cpu/cpu_exec.c` 是历史遗留的空文件，不参与构建，可以删除。

---

## 四、常见任务怎么做

| 任务 | 要动的地方 |
|---|---|
| 加一条 RV64I 指令 | `instr/i/instr.def` 加一行 + `instr/i/exec.c` 实现 `exec_<名字>` |
| 加一个扩展（如 M 扩展） | 新建 `instr/m/{instr.def,exec.c}`，在 `decode.c` 的两处 include 加上 |
| 加一个 CSR | `include/cpu/csr_list.h` 管理地址/存储；WARL、别名、权限例外和副作用在 csr_access.c 显式实现并验证 |
| 加一个设备 | 写 `src/device/xxx.c` 实现 `MmioRegion`，在 `device.h` 声明 init，在 `machine.c` 里实例化 |
| 加一块板子 | `machine/boards/` 加一个 `MachineConfig`，在 monitor 里选择 |
| 加一种 trace | 见 `docs/trace_and_uarch_plan.md`：core 只发事件，收集器是订阅者 |
| 加微架构部件（PHR/预测器/cache） | 同上：`src/uarch/<部件>/` 订阅事件流，永不改核心状态 |
| 实现 Sv39 | 只改 `target/riscv64/mmu/mmu.c`（+ tlb.c），上层零改动 |
| 加一个配置项 | 在对应 Kconfig 里加 `config`，代码里 `#ifdef CONFIG_X`，Makefile 里可用 `SRCS-$(CONFIG_X)` 增减源文件 |
| 加一块新板子 | `machine/boards/` 加文件 + 在 monitor 里按配置选择（`--machine` 待做） |

---

## 五、与 NEMU 的对照（为什么这样改）

| NEMU 的做法 | RVISS 的做法 | 原因 |
|---|---|---|
| `include/` 与 `src/` 同名子目录 | 公共接口进 `include/`，私有头留模块内 | 避免"两个树对着看" |
| `utils.h` 杂物袋 | log/state/config 分文件 | 依赖清晰 |
| 一条指令改三处 | `instr.def` 单一事实来源 | 可维护性 |
| opcode 两级译码表 | 扁平表（一条指令一个完整模式） | 可读性优先；表大了再换 hash |
| `priv.c` 4000 行 switch | CSR 表驱动 + 少量副作用钩子 | 清单管理元信息，显式处理非平凡语义 |
| `cpu-exec.c` 混入 tcache/checkpoint/profiling | 引擎循环保持纯净 | 关注点分离 |
| Kconfig 全量编译期配置 | 板级默认编译期（Kconfig）+ 运行期覆盖（命令行） | 兼顾"板子定义清晰"与"一套二进制多种跑法" |
| 宿主时钟驱动设备 | 指令数驱动 | 确定性 |

---

## 六、构建系统（Makefile + Kconfig）

与 NEMU/Linux 同构：**Kconfig 管编译期配置，Makefile 只管编译**。

```
make menuconfig          交互配置（tools/kconfig 的 mconf）
make minimal_defconfig   套用 configs/minimal_defconfig
make -j                  编译 → build/rviss（或 CONFIG_SHARE 时 build/librviss_ref.so）
make check               单元测试 + 冒烟测试
make run IMG=xx.elf      运行；make gdb IMG=xx.elf 用 gdb 跑
make compiledb           生成 compile_commands.json（clangd 用，参数取自真实构建）
make distclean           清掉 build/ 与全部配置产物
```

配置产物流向：

```
.config ──conf --syncconfig──┬─→ include/config/auto.conf      （Makefile 读，决定编哪些 .c）
                             ├─→ include/generated/autoconf.h  （C 代码读，#define CONFIG_xxx）
                             └─→ include/config/<symbol>.h     （空标记文件，给 fixdep 用）
```

三个要点（也是与"随便写个 Makefile"的区别）：

1. **自定位**：顶层 Makefile 用 `RVISS_HOME := $(abspath ...)` 定位自己，工具 Makefile
   同样自定位 —— 不依赖、也不受外部 `NEMU_HOME` 之类的环境变量干扰。
2. **精确增量**：每个对象编译完调用 `fixdep`，把 `.d` 里宽泛的 `autoconf.h` 依赖
   换成"这个源文件实际用到的 CONFIG_ 标记文件"。改一个配置项只重编真正相关的文件
   （实测：关掉 `CONFIG_HAS_UART16550` 只重编了 `machine.c` 一个文件）。
3. **配置即代码开关**：`#ifdef CONFIG_HAS_UART16550` 决定代码是否编入，
   `SRCS-$(CONFIG_HAS_UART16550) += src/device/uart16550.c` 决定文件是否参与编译。
