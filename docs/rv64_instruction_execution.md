# RVISS 中一条 RV64 指令是怎样执行的

状态：verified（2026-10-13）。本文按当前源码解释单 hart RV64 功能模型的真实路径，
不把流水线、预测器、译码缓存或周期模型假设进去。

## 先看完整路径

RVISS 将“执行一条指令”定义成一个可以提交给引擎和观察者的事件。批处理模式下，
`monitor_run()` 反复调用 `engine_step()`；交互模式的单步命令也走同一个入口。

```mermaid
flowchart TD
    A[monitor_init: 配置/机器/镜像/engine_init] --> B[engine_step]
    B --> C{是否允许继续}
    C -- 否 --> Z[无新事件，返回 false]
    C -- 是 --> D[riscv_step]
    D --> E{有待处理的异步中断?}
    E -- 是 --> I[记录中断事件并进入 trap]
    E -- 否 --> F[创建 Decode{pc=cpu.pc}]
    F --> G[riscv_decode]
    G --> H[取指、C 展开、候选匹配、填操作数]
    H --> J{译码成功?}
    J -- 是 --> K[先把 cpu.pc 置为 snpc]
    K --> L[调用 Decode.exec]
    J -- 否 --> M[取指故障/宿主错误/非法指令]
    L --> N[捕获访存、CSR、跳转和执行结果]
    I --> O[填充 StepResult]
    M --> O
    N --> O
    O --> P[complete_event]
    P --> Q[事件号、退休数、设备时间、最近历史、日志、RVTR]
```

源码入口和职责如下：

| 阶段 | 入口 | 作用 |
| --- | --- | --- |
| 程序启动 | `src/monitor/monitor.c:monitor_init` | 解析配置、建立机器、加载镜像、初始化译码表 |
| 引擎边界 | `src/engine/interpreter/interp.c:engine_step` | 检查暂停/预算，执行一个事件并完成统一记账 |
| 体系结构一步 | `src/target/riscv64/step.c:riscv_step` | 优先处理中断，否则取指、译码、执行并分类结果 |
| 译码 | `src/target/riscv64/decode.c:riscv_decode` | 取指、压缩展开、指令匹配、操作数提取 |
| 指令语义 | `src/target/riscv64/instr/*/exec.c` | 修改 CPU 状态，或在 `Decode` 中记录失败 |
| 访存原语 | `src/engine/interpreter/rtl_basic.c` | 调用虚拟地址访问路径并回填 `Decode.memory` |
| 事件提交 | `src/engine/interpreter/interp.c:complete_event` | 更新事件序号、退休数、设备时间和观察者 |

`engine_init()` 只初始化译码索引和最近事件环；它不会复位机器。机器复位由
`machine_init()` 完成，CPU 的 reset PC、内存和设备先准备好，之后才开始执行。

## 1. `Decode` 到底是什么

`Decode` 定义在 [include/cpu/decode.h](../include/cpu/decode.h)。它不是“只有译码结果的
结构体”，而是一条指令从取指开始到执行结束所用的临时上下文。它同时携带：

1. 取指输入和顺序 PC；
2. 原始编码和规范化后的执行编码；
3. 译码出的寄存器/立即数操作数；
4. 要调用的执行函数；
5. 访存、陷阱、CSR 计数器和 WFI 等执行副作用；
6. 宿主错误和客户机同步异常。

`riscv_step()` 每次在栈上创建一个零初始化实例：

```c
Decode s = {.pc = cpu.pc};
```

零初始化很重要：指令不产生访存、trap、浮点写回或等待请求时，相关字段天然为
`false`/空值；只有真正发生该副作用的路径才设置它。

### `Decode` 每个字段

| 字段 | 含义 | 为什么需要 |
| --- | --- | --- |
| `pc` | 本条指令的虚拟地址 | 取指、相对跳转、异常 `epc` 和日志都以它为基准 |
| `snpc` | 顺序下一地址，成功取指后为 `pc + ilen` | 引擎在调用执行函数前先把 CPU PC 推进到这里；非跳转指令无需重复写 PC |
| `instr` | 客户机看到的原始指令位；16 位指令只使用低 16 位，32 位指令使用完整 32 位 | 保留原始字节用于 `mtval`、trace、difftest 和错误诊断 |
| `encoding` | 执行用的 32 位编码 | 普通指令等于 `instr`；C 指令先展开为等价的 32 位基础指令，语义函数可以复用 RVI 实现 |
| `ilen` | 体系结构判断的指令长度，目前为 2 或 4 | 决定 `snpc` 和结果中的 `instruction_length` |
| `raw_bytes` | 本次实际成功取到的原始字节数，目前为 0、2 或 4 | 与 `ilen` 不完全重复：32 位指令第二个 halfword 取指失败时，`ilen=4` 而 `raw_bytes=2`；第一段失败时二者都是 0 |
| `instruction_valid` | 指令的预期字节是否全部取得 | 表示取指字节完整，不表示编码一定合法；完整取得的非法编码仍会置为 `true` |
| `trap_pending` | target 已经发现客户机同步异常 | 防止译码失败后被错误地再次当成非法指令；也保证执行中只保留第一处故障 |
| `host_error` | 宿主执行路径失败，例如模拟器无法完成物理访问 | 宿主错误不能伪装成客户机 page fault，也不能退休 |
| `trap_cause` | 待注入的 RISC-V 异常原因 | 由取指、访存、CSR 权限、对齐或指令语义填写 |
| `trap_tval` | 待注入异常的 `tval` | 保存故障地址或规范要求的附加值 |
| `counter_write_cycle` | 当前指令显式写 `mcycle` | `riscv_update_counters()` 需要避免在同一事件结束时又自动加一，破坏 CSR 写入结果 |
| `counter_write_instret` | 当前指令显式写 `minstret` | 与上面相同；客户机 CSR 计数和引擎累计退休数是两套独立概念 |
| `fp_register_write` | 语义写了 FPR | `dest.preg` 只指向 GPR，浮点写回必须由 `riscv_step()` 单独提取并放入 `StepResult` |
| `wait_request` | 合法的 `WFI` 已退休，请求引擎推进到下一个设备事件 | WFI 不是异常；设备和中断调度由引擎在事件完成后处理 |
| `memory` | 最近一次显式数据访问，或取指故障的 `MemAccess` | 记录访问类型、宽度、VA/PA、数据、MMIO、fault 和实际写入；当前一个事件最多保存一个显式数据访问 |
| `rd` | 指令编码中的目的寄存器编号 | 通用提交和 trace 需要知道写的是哪个寄存器 |
| `rs1` | 第一源寄存器编号 | 操作数提取和调试信息使用 |
| `rs2` | 第二源寄存器编号 | R/S/B/AMO 等指令使用 |
| `exec` | `EHelper` 函数指针，例如 `exec_addi` | 译码只决定“执行哪个入口”，具体语义放在扩展目录的 `exec.c` |
| `dest` | 目的操作数槽 | 通常保存目的 GPR 指针；S/B 格式借用 `dest.imm` 保存 store/branch 偏移 |
| `src1` | 第一源操作数槽 | 通常保存 GPR 指针；U/J/Z 等格式也会把立即数放在这里 |
| `src2` | 第二源操作数槽 | R/B/S 使用 GPR 指针，I/X 使用立即数 |
| `mnemonic` | 静态助记符字符串 | 人类日志和 `StepResult.operation` 使用；C 指令保留 `c_addi` 这样的原始助记符 |

这里的“借用”是有意的：`Operand` 是小型 union-like 结构，没有额外的 C union，
由格式字母保证当前读取哪一成员。`is_imm` 区分立即数和寄存器；执行宏知道自己
应该读取 `preg` 还是 `imm`。

### `Operand` 为什么保存指针

```c
typedef struct {
  rtlreg_t *preg;
  sword_t imm;
  bool is_imm;
} Operand;
```

寄存器操作数保存的是 `cpu.gpr[]` 中元素的地址，所以 `rtl_add()`、`rtl_li()` 等原语
可以直接读写架构状态，不需要再用寄存器编号查一次。`rd=x0` 是例外：目的操作数
指向 `decode.c` 中的 `zero_reg` 哑元，写入不会改变 `cpu.gpr[0]`；每个事件结束时
`riscv_step()` 还会再次把 `cpu.gpr[0]` 归零。

源寄存器 `rs1/rs2` 即使为 x0，也直接指向 `cpu.gpr[0]`，因为读 x0 本身是安全的。

## 2. 译码表如何生成

每个扩展的 `instr.def` 是唯一指令元信息来源。例如 `addi` 的一行是：

```c
INSTR(addi, "??????? ????? ????? 000 ????? 0010011", 'I')
```

`decode.c` 多次包含这些 `.def` 文件，每次把 `INSTR` 定义成不同的宏：

1. 第一次生成 `void exec_addi(Decode *)` 等执行函数声明；
2. 第二次生成 `InstrEntry` 表项，表项包含名字、pattern、操作数格式和函数指针；
3. `exec.c` 再用同名函数实现语义。

因此新增普通指令时，编码、名字和操作数格式只在 `.def` 写一行，执行入口通过
名字自动关联。编译期 `CONFIG_RV_M`、`CONFIG_RV_D`、`CONFIG_RV_ZICSR` 等宏会让
相应表项是否存在与 Kconfig 保持一致；没有编译该扩展时，编码会走非法指令路径。

### pattern 如何变成 key/mask

`pattern_decode()` 把每个字符转换为两个 64 位值：

- `'0'`：`key` 位为 0，`mask` 位为 1；
- `'1'`：`key` 位为 1，`mask` 位为 1；
- `'?'`：`mask` 位为 0，表示不参与匹配；
- 空格只用于提高可读性。

运行时匹配的核心是：

```c
((encoding >> shift) & mask) == key
```

尾部连续通配位被记录到 `shift`，以缩短比较；中间的通配位仍由 `mask=0` 忽略。

### 为什么不是每次扫描整张扁平表

扁平表仍然是可读性和优先级的唯一来源，但 `riscv_decode_init()` 会为每个表项
建立 `(opcode, funct3)` 候选索引：

1. 从 pattern 提取 opcode 位 `[6:0]` 和 funct3 位 `[14:12]`；
2. 把一条指令放入匹配的 bucket；funct3 通配时最多放入 8 个 bucket；
3. 运行时只访问当前 `opcode/funct3` 的 bucket；
4. 对 bucket 内候选再做完整 `key/mask` 匹配。

候选按原始 `instr.def` 顺序追加，所以完整匹配仍保留表序优先级。这个设计同时保留
了“一条指令一行”的维护方式和接近分级译码的运行时成本；它不是把扁平表改成
每次都从头遍历。

## 3. 从 `pc` 开始怎样取指

`riscv_decode()` 首先检查 `pc` 是否满足当前 `misa.C` 决定的 IALIGN：

- 启用 C 时，PC 至少 2 字节对齐；
- 未启用 C 时，PC 必须 4 字节对齐。

非法对齐立即设置 instruction-address-misaligned trap。

之后调用 `fetch_halfword()` 读取 `pc` 处的 2 字节。这个访问不是直接读宿主数组，
而是经过：

```text
vaddr_ifetch
  -> isa_mem_access
      -> 有效特权/对齐/PMP/Sv39 翻译
          -> paddr_try_read 或 MMIO
```

访问失败时，`Decode.memory` 保存访问结果；客户机 fault 设置 `trap_pending`，宿主
失败设置 `host_error`，译码立即返回 `false`。

取到低 halfword 后，RISC-V C 扩展通过低两位判断长度：

```c
if ((instr & 0x3) != 0x3) {
    ilen = 2;
} else {
    ilen = 4;
}
```

当前实现只支持 16 位和 32 位指令。判断为 32 位时再取 `pc+2` 的 halfword；第二次
取指失败时，`instr` 仍保留已取到的低 halfword，`ilen=4`、`raw_bytes=2`、
`instruction_valid=false`，这样异常记录不会错误地声称完整指令已经取到。

## 4. C 指令为什么有两个编码

对 32 位指令：

```text
instr == encoding
```

对 16 位 C 指令：

```text
instr     = 原始 16 位编码
encoding  = 展开后的等价 32 位编码
```

`src/target/riscv64/instr/c/expand.c` 先按 16 位 `key/mask` 找到 `c_addi`、`c_lw` 等
压缩表项，再重排立即数和寄存器字段，生成基础指令编码。例如 C.ADDI 会变成普通
ADDI 的 32 位编码。随后返回 `riscv_decode()`，继续使用同一套 opcode/funct3 bucket
和基础指令 `.def` 表匹配。

这样做有三个目的：

- 语义实现只需要一份，C.ADDI 不必再写一套 ADDI 算术逻辑；
- `instr` 仍能用于 `mtval`、trace 和调试，保留客户机实际看到的编码；
- `mnemonic` 在展开前保存为 `c_addi`，因此 trace 能区分客户机执行的是压缩指令，
  即使它最终复用了 `exec_addi`。

C 表目前是一个独立的小表，C 识别本身是线性匹配；展开后才进入主表的快速索引。

## 5. 完整匹配后怎样填操作数

`fill_operands()` 先从 `encoding` 提取：

```text
rd  = bits[11:7]
rs1 = bits[19:15]
rs2 = bits[24:20]
```

然后按格式字母填充 `dest/src1/src2`：

| 格式 | 典型指令 | `Decode` 中的布局 |
| --- | --- | --- |
| `R` | `add`, `mul` | `dest.preg=rd`，`src1.preg=rs1`，`src2.preg=rs2` |
| `I` | `addi`, `ld`, `jalr` | `dest.preg=rd`，`src1.preg=rs1`，`src2.imm=IMM_I` |
| `X` | `slli`, `srai` | `dest.preg=rd`，`src1.preg=rs1`，`src2.imm=SHAMT` |
| `Z` | `csrrwi` 等 | `dest.preg=rd`，`src1.imm=ZIMM5` |
| `S` | `sd`, `sw` | `dest.imm=IMM_S`，`src1.preg=rs1`，`src2.preg=rs2` |
| `B` | `beq`, `bltu` | `dest.imm=IMM_B`，`src1.preg=rs1`，`src2.preg=rs2` |
| `U` | `lui`, `auipc` | `dest.preg=rd`，`src1.imm=IMM_U` |
| `J` | `jal` | `dest.preg=rd`，`src1.imm=IMM_J` |
| `N` | `fence`, `ecall`, FP 统一入口 | 不预填通用操作数，执行函数按自身编码读取 |

`S`/`B` 把偏移放在 `dest.imm`，不是把它当成真正的目的寄存器；这是当前
`Operand` 结构的复用约定。执行函数和格式必须配套，不能把一个格式的槽位拿给
另一个格式使用。

以 `0x00a00293`（`addi t0, x0, 10`）为例：

```text
instr/encoding = 0x00a00293
ilen           = 4
snpc           = pc + 4
rd             = 5
rs1            = 0
src2.imm       = 10
dest.preg      = &cpu.gpr[5]
src1.preg      = &cpu.gpr[0]
exec           = exec_addi
mnemonic       = "addi"
```

此时译码已经完成，但还没有把 `cpu.pc` 或目的寄存器改成执行结果。

## 6. 执行函数怎样改变 CPU 状态

`riscv_step()` 在译码成功后先做：

```c
cpu.pc = s.snpc;
s.exec(&s);
```

因此每个执行函数默认从“顺序下一条 PC”开始。只有控制流指令需要通过
`riscv_jump()` 改写 PC；这也使异常发生时可以把 `s.pc` 作为当前指令地址保存。

### 算术指令

`exec_addi()` 由 `EXEC_I(addi, rtl_addi)` 宏生成，最终调用：

```c
rtl_addi(s->dest.preg, s->src1.preg, s->src2.imm);
```

RTL 原语在解释器中直接执行宿主算术并写目的 GPR。执行层只描述 RV64 语义，
`rtl_basic.c` 提供解释器实现；未来替换为其他引擎时，target 语义接口不必重写。

### Load/store

`exec_ld`、`exec_sw` 等调用 `rtl_lm`/`rtl_sm`。原语再调用 `vaddr_read`/
`vaddr_write`，由统一访问路径完成有效特权、对齐、Sv39、PMP、RAM/MMIO 检查。

成功的 load 才执行 `*dest.preg = ...`；失败时 `record_access()` 把 fault 写入
`Decode`，目的寄存器保持原值。store 只有物理访问成功才会设置
`memory.write_performed=true`，因此异常路径不会伪造一次写入。

### 分支和跳转

- 条件分支只在条件成立时调用 `rtl_j(s, target)`；不成立时 PC 保持 `snpc`。
- `jal`/`jalr` 先检查目标对齐和目标 PC，再写回链接地址 `snpc`。
- `jalr` 先计算并对齐目标，再写目的寄存器，保证 `rd == rs1` 时仍符合语义。

`riscv_jump()` 发现目标违反当前 IALIGN 时，只设置 `trap_pending`，不会先写入错误
的 PC 或链接寄存器。

### CSR、系统和浮点指令

系统指令执行函数可以直接调用 CSR/陷阱辅助函数。例如 `ecall` 只设置对应的
同步异常；`mret`/`sret` 检查权限后恢复 PC；合法 `wfi` 设置 `wait_request`；
`sfence.vma` 调用 MMU TLB 失效接口。

FP 统一入口会根据编码决定 F/D 运算，并把 `fp_register_write` 置位。它不能只靠
`dest.preg` 表示写回，因为 FPR 和 GPR 是两组不同的架构寄存器。

## 7. 异常、宿主错误和退休怎样区分

`riscv_exec_trap()` 只记录第一处同步异常：

```c
if (s->trap_pending) return;
s->trap_pending = true;
s->trap_cause = cause;
s->trap_tval = tval;
```

`riscv_step()` 随后按以下优先级分类：

1. **异步中断**：在译码前检查；不尝试指令、不退休，PC 直接进入 trap vector；
2. **宿主错误**：模拟器无法完成访问或内部执行路径失败；事件为 `STEP_HOST_ERROR`，
   不退休，模拟器进入 aborted；
3. **同步异常**：取指、非法编码、权限、对齐、page fault、CSR 或语义检查失败；
   进入 `STEP_SYNCHRONOUS_TRAP`，不退休；
4. **正常执行**：执行完成，标记 `retired=true`；如果是退出指令，结果为
   `STEP_HALTED`，但仍可同时 `retired=true`。

译码返回 `false` 本身不等于非法指令。它可能是：

- `trap_pending=true`：取指 fault 或其他已知客户机异常；
- `host_error=true`：宿主路径失败；
- 两者都为假：编码没有匹配表项，此时 `riscv_step()` 才补一个 illegal-instruction
  trap。

这就是为什么 `Decode` 必须同时有 `trap_pending` 和 `host_error`，而不能只返回一个
布尔值。

## 8. `Decode` 怎样变成 `StepResult`

`Decode` 是 target 内部的可变暂存区；`StepResult` 是一个完成事件的只读快照，定义在
[include/cpu/step.h](../include/cpu/step.h)。`riscv_step()` 从前者复制结果信息，
然后 `complete_event()` 补齐引擎才知道的字段。

### `StepResult` 字段分组

| 字段 | 填写者 | 语义 |
| --- | --- | --- |
| `event_seq` | 引擎 | 所有事件的序号，中断/trap 也占一个事件 |
| `retired_total` | 引擎 | 到本事件为止成功退休的指令数 |
| `outcome` | target/引擎 | `RETIRED`、同步 trap、中断、halt、stop 或宿主错误 |
| `instruction_attempted` | target | 是否真正尝试了客户机指令；独立中断为假 |
| `retired` | target | 指令是否完成架构退休；trap/中断为假 |
| `wait_request` | target | 合法 WFI 是否请求设备时间推进 |
| `pc_before/pc_after` | target | 事件前后的 PC；异常后 `pc_after` 是 trap vector |
| `privilege_before/after` | target | 事件前后的特权级 |
| `raw_instruction` | target | 原始客户机编码，不是 C 展开后的 `encoding` |
| `instruction_length/raw_bytes` | target | 预期长度和实际取到的字节数 |
| `instruction_valid` | target | 原始指令字节是否完整 |
| `operation` | target | 静态助记符指针；适合日志，不是可序列化 ABI |
| `register_write` | target | 是否有可观察的 GPR/FPR 写回 |
| `register_is_fp` | target | 写回属于 FPR 还是 GPR |
| `rd/register_value` | target | 写回寄存器编号和值；x0 不报告为有效 GPR 写入 |
| `counter_write_*` | target | 当前事件是否显式写客户机计数器 |
| `memory` | target | 访存摘要，含 fault 或是否真的写入 |
| `trap` | target | 有效时保存 cause、epc、tval |

观察者只拿到 `StepResult` 的副本。最近事件环、文本指令日志和 RVTR trace 都在
`complete_event()` 之后读取它，不参与指令语义，也不能通过观察接口修改 CPU。

## 9. `complete_event()` 为什么不能省略

`riscv_step()` 只负责 target 的体系结构动作；引擎统一完成跨模块的事件规则：

1. 增加 `sim.event_seq` 和逻辑时间；
2. 按 `instruction_attempted` 增加尝试数；
3. 按 `retired` 增加引擎退休数；
4. 更新客户机 `cycle/minstret`，但尊重当前指令对 CSR 计数器的显式写入；
5. 推进设备时间并重新采样 IRQ；
6. 处理 WFI 的确定性等待；
7. 保存最近 16 个事件；
8. 输出人读文本日志和结构化 RVTR trace；
9. 最后检查下一步是否达到预算。

因此 `StepResult.retired_total` 必须由引擎在记账后填写，不能在 target 中提前填写。
`event_seq`、尝试数、退休数和逻辑 tick 也不能混成一个计数器：同步异常和中断会
消耗事件，但不会退休指令；WFI 还可能推进设备时间而不执行更多指令。

## 10. 四个具体例子

### 例 1：`addi t0, x0, 10`

1. 取到 4 字节，`instr=encoding=0x00a00293`。
2. bucket 按 opcode `0010011`、funct3 `000` 找到 `addi`。
3. `fill_operands()` 得到 `rd=5`、`rs1=0`、立即数 10。
4. CPU PC 先从 `pc` 变成 `pc+4`。
5. `exec_addi` 写 `cpu.gpr[5]`。
6. `riscv_step()` 标记 `retired=true`，引擎把事件和退休数各加一。

### 例 2：`lw` 的页故障

1. 指令本身完整译码，PC 暂时推进到 `snpc`。
2. `rtl_lm()` 调用虚拟地址读取。
3. Sv39 或权限检查返回 `MEM_FAULT`。
4. `Decode.memory` 保存访问地址和 cause，`trap_pending=true`。
5. load 不写目的寄存器。
6. `riscv_step()` 设置 `STEP_SYNCHRONOUS_TRAP`，用原指令 PC 进入 trap；不退休。

### 例 3：`c.addi`

1. 低两位不是 `11`，先按 2 字节取得原始 `instr`。
2. C 表识别 `c_addi`，把压缩立即数重排成普通 ADDI 的 `encoding`。
3. `mnemonic` 保留 `c_addi`，主表匹配 `exec_addi`。
4. `snpc=pc+2`，执行语义和普通 ADDI 相同，trace 仍能看到原始 16 位编码。

### 例 4：异步中断

1. `riscv_step()` 在创建/执行 `Decode` 前查询 IRQ。
2. 有中断时不取指，`instruction_attempted=false`、`instruction_valid=false`、
   `retired=false`。
3. 生成 `STEP_INTERRUPT` 事件并进入 trap vector。
4. 引擎增加 `event_seq`，但不增加尝试数和退休数。

## 11. 为什么 `Decode` 看起来这么长

它长的根本原因是它跨越了多个阶段，但这些字段并不是重复表达同一件事：

- `instr`/`encoding`：原始客户机字节和内部规范化执行编码；C 扩展必须同时保留两者；
- `ilen`/`raw_bytes`/`instruction_valid`：预期长度、已取字节数、字节是否完整，能精确描述
  第二个 halfword 取指失败等边界情况；
- `trap_pending`/`host_error`：客户机可观察异常和模拟器自身失败必须分开；
- `exec`/`Operand`：译码与执行解耦，执行入口不用重新解析编码；
- `memory`/`counter_write_*`/`fp_register_write`/`wait_request`：执行结果不仅是一个 GPR
  值，还要让统一提交边界知道发生了什么；
- `mnemonic`/寄存器编号：日志、difftest 和 trace 需要元信息，但语义执行只需指针。

当前结构是“每条指令一个临时事务上下文”，不是 CPU 的永久状态，也不会随指令累积。
把它拆成 `FetchInfo`、`OperandInfo`、`EffectInfo` 三个结构可以改善视觉分组，但会增加
执行函数和异常辅助函数的参数层级；在当前 RV64 单引擎规模下，保持一个上下文更容易
保证“任何失败都能回到同一个事件”。如果后续增加多执行引擎或更严格的提交检查，拆分
应以接口契约为依据，而不是仅仅为了减少字段数量。

## 12. 当前实现的边界

- 这是顺序解释器：没有流水线、错误路径执行、真实 cache 或周期延迟。
- 当前没有译码缓存；`fence.i` 在单 hart 功能模型中是可识别的空操作，未来加入译码
  缓存时才需要真正刷新。
- 一个事件的 `MemAccess` 只有一个显式访问摘要；当前实现的 ISA 语义按此契约设计。
- 指令长度目前只有 2/4 字节；更长指令格式尚未进入 RV64 目标范围。
- `StepResult.operation` 指向静态字符串，只能用于当前进程中的日志和观察，不是 checkpoint
  或 RVTR 的稳定序列化字段。
- 事件观察、日志和 trace 只能读取提交结果，不能决定分支、修改寄存器或代替异常处理。

## 13. 如何自己跟读和验证

建议用同一条指令沿以下顺序设置断点或打印：

```text
engine_step
  -> riscv_step
      -> riscv_decode
          -> fetch_halfword
          -> riscv_expand_compressed（仅 C）
          -> fill_operands
      -> s.exec(&s)
  -> complete_event
```

已有测试会走真实取指和译码路径，而不是直接调用一个“伪译码”函数：

- `tests/unit/test_decode.c` 检查助记符、长度、顺序 PC 和操作数；
- `tests/unit/test_c.c` 检查原始 16 位编码、展开执行和半字取指故障；
- `tests/unit/test_step.c` 检查退休、访存元信息、异常不写回和事件记账；
- `tests/unit/test_trace.c` 检查 `StepResult` 到 RVTR 记录的序列化。

本次文档没有改变执行语义。完成检查使用：

```sh
make CC=/usr/bin/gcc BUILD_DIR=build/t022 check
git diff --check
```
