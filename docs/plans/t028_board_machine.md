# T028：板卡注册与 machine_update

状态：in_progress（设备注册已验证，整机检查被既有译码重构挡住）

## Objective

- Trigger: `machine_init` 装配一台机器；引擎每走一个事件先进入板卡。
- Observable result: Kconfig 打开的外设在 `machine_init` 里注册成功才出现在地址空间和中断线上；关掉的外设不注册。外设状态结构体不出现在设备头文件里，CPU 只通过 MMIO read/write 访问。每个事件由 `machine_update` 先刷新 UART、CLINT、PLIC 的输出，再查询中断：有可响应中断则进入中断，否则执行一条指令。

## Non-goals

- 不把 RAM 改成 `MemObject`，不接 `src/device/ram/ram.c`。
- 不改变 mtime 每 128 个逻辑 tick 加 1、WFI 快进、UART/PLIC 寄存器语义。
- 不在本任务修理解码表、`cpu_state.h` 或其它与板卡无关的未完成改动。
- 最小板仍然只配置一个 UART；实例池允许再注册，不在本任务加第二块板级串口。

## 现状缺口

已有 `machine_init`、`mmio_map` 和 `IrqLine`，但还不满足这条控制流：

- `ClintState`、`Uart16550State`、`PlicState`、`TestFinisherState` 放在公开头里，板级直接改 `uart.mcr`、`clint.mtimer_irq` 等字段。
- 引擎在指令之后调用匿名的 `devices_tick`。中断查询在 `riscv_step` 内部，板卡没有“更新外设再决定中断或执行”的入口。
- 当前工作区的 `machine_init` 没有使用传入配置，设备 read/write 也未形成可编译的封闭实现。

## 决定

- 外设 `.c` 用固定池保存实例，对外只给不透明指针。结构体不含 `MmioRegion`。UART 字段只有中断线、发送回调和寄存器/FIFO。多个串口是多个池槽，不是 machine.c 里的结构体。
- `MemObject` 由板卡填好后交给 `mmio_map`，总线拷贝这份描述。里面是名字、窗口大小、ctx、read、write。read/write 失败表示访问宽度或偏移非法，且不得产生副作用。基址只存在于总线映射。
- `machine_init` 不调用 `sim_reset` 或 `cpu_reset`。它初始化内存图和总线，然后 `machine_add_*`：创建实例、映射 MMIO、连接中断。CLINT 到 `INT_MTI`/`INT_MSI`，PLIC 到 `INT_MEI`/`INT_SEI`，每路 UART 按配置里的 `plic_source` 接线。`monitor_init` 负责仿真器和 CPU 复位。
- 时间顺序保持已验证行为：事件结束后 `machine_advance` 推进逻辑时间；下一次 `machine_update` 只根据已经推进的设备状态刷新输出并查询中断。因此某次 advance 拉高的中断在下一条指令边界处理，而不是提前一条。
- `bus_init` 清掉板卡登记的实例列表和 MMIO，但不销毁设备池里的对象。之后 `machine_update` / `machine_advance` 不再驱动它们；测试仍可对旧指针调用 `clint_reset`。

## Acceptance

- [x] 打开的设备 attach 成功后，MMIO 命中；重叠地址 attach 失败。
- [x] 设备头文件不再定义外设状态结构体。
- [x] `engine_step` 调用 `machine_update`：先 `clint_update`、`uart16550_update`、`plic_update`，再 `riscv_query_intr`；非 0 走中断，否则执行指令。代码已接上，尚未在整机执行中跑到。
- [ ] `test_devices.c` 已改为 MMIO 加实例指针，不再读内部字段。文件本身还编不过，没有跑。
- [ ] 全量 `make check`。当前被既有 `MemAccess` / `Decode` / `CpuState` 重构挡住，不能当作本任务通过。

## Evidence

- `/usr/bin/gcc -std=c11 -Wall -Wextra -Werror` 编译通过：`bus.c`、`clint.c`、`plic.c`、`uart16550.c`、`test_finisher.c`、`machine.c`、`minimal.c`、`test_board.c`、`test_bus.c`。
- `/tmp/test_board`：`tests/unit/test_board.c: 55 checks, 0 failures`。覆盖 CLINT 注册成功、重复地址失败、MMIO 时间、`bus_init` 后板卡不再推动中断线，以及两个 UART 各自收发、地址重叠被拒绝。
- `/tmp/test_bus`：`tests/unit/test_bus.c: 13 checks, 0 failures`。
- `tests/unit/test_devices.c` 不能编译：`step.h` 的 `MemAccess` 和当前 `CpuState` 缺字段。这是板卡改动之前就有的译码重构缺口。全量 `make check` 未跑。

## Assumptions

- 单 hart。每种外设最多 8 个实例，板卡配置里的 UART 最多 4 路。最小板仍是 1 路，PLIC source 10。
- 宿主 UART 输入仍由板卡在 `machine_advance` 里 poll，读 `sim.event_seq`。finisher 的停机回调仍调用 `sim_halt`。这两处是运行时的板级边，不是 `machine_init` 复位仿真器。
- 字节进入 UART 的宿主入口不是 MMIO，因为宿主机不是总线主设备。
