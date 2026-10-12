# RVISS 编码规范

> 规范的目的不是"好看"，而是三条可验证的性质：
> **① 依赖有向无环**（改动不扩散）　**② 加功能只加文件**（不改别人的代码）　
> **③ 零警告 + 有测试**（错误当场暴露）。
> 每条规则后面都写了理由；**不知道怎么选时，选"让依赖更少"的那个**。

---

## 1. 语言与编译

- **C11（gnu11）**，不引入 C++（当前全 C；工具除外）。理由：模拟器不需要抽象机制，
  C 让"数据在哪、谁分配"一目了然。
- **零警告**：`-Wall -Wextra -Werror`（Kconfig 的 `CONFIG_CC_STRICT`，默认开）。
  实测价值：`-Werror` 在迁移构建系统时一次抓出 `memcpy` 缺声明和注释里 `/*` 两个真问题。
- 只用 **Kconfig** 做编译期开关（不用裸 `#define` 开关、不用宏魔法做配置）。
- 不写平台相关的内联汇编/编译器扩展（`SEXT` 的 statement expression 是唯一的例外，
  已在 `common/bits.h` 内封装）。

## 2. 命名

| 对象 | 规范 | 例子 |
|---|---|---|
| 文件（含目录名） | `snake_case.c/.h`，一个文件一个主题，**统一用下划线、不用连字符** | `csr_access.c`、`host_time.c`、`local_include/` |
| 类型 | `PascalCase`（结构体）/ `xxx_t`（枚举与简单别名） | `CPU_state`、`LogLevel`、`sim_state_t` |
| 函数 | `模块_动词_名词`，全局入口用 `模块_动词` | `mem_init`、`riscv_raise_trap`、`mmio_register` |
| 宏常量 | `UPPER_SNAKE` | `MAX_MMIO_REGIONS`、`MSTATUS_MXR` |
| 局部变量 | 短、小写；数学/硬件量按惯例 | `addr`、`len`、`pte`、`mtimecmp` |
| 枚举成员 | 前缀归属 + `UPPER_SNAKE` | `LOG_CAT_MEM`、`PRV_M`、`EX_II` |
| Kconfig | `CONFIG_模块_特性` | `CONFIG_HAS_UART16550`、`CONFIG_TRACE_MEM` |

**不要**：`data`、`tmp2`、`flag` 这类无信息名；`g_` 前缀（本项目单例只有 `cpu`/`sim`/`rt_cfg`，一眼可辨）。

**为什么文件名用 `_` 而不是 `-`**：Linux / QEMU / NEMU 其实都是**两种混用、没有成文规定**
（同一棵树里 `page_alloc.c` 与 `memory-failure.c` 并存），所以这不是"遵循谁"的问题，
而是选一套并坚持。我们选下划线的理由：
① 与代码里的标识符体系（`csr_read`、`mmio_map`）是同一套词汇；
② 连字符有实际风险——Python 模块名必须是合法标识符（`import gen_compiledb`）、
`-` 开头的文件名在 shell 里会被当选项、部分工具要求文件名可作标识符。
**唯一例外**：`tools/` 下从 Linux/NEMU 移植的 kconfig/fixdep 是第三方代码，保持原样不改名。

## 3. 文件与目录

- **一个模块 = 一个目录**；目录里的文件按主题拆，单文件 ≤ ~500 行，超了就拆。
- **公开接口进 `include/<模块>/`，私有头留在模块目录内**（`machine.h`、
  `target/riscv64/local_include/*.h`）。判据只有一条：*会被别的子系统 include 吗？*
- 新子系统（如 `src/trace/`、`src/uarch/`）默认同样是
  `include/<子系统>/`（对外）+ `src/<子系统>/`（实现）的布局。
- 单例状态集中在少数文件里声明并 `extern`（`CPU_state cpu`、`SimState sim`、
  `RuntimeConfig rt_cfg`）；**其它任何全局可变状态都不允许**。

## 4. 头文件

- 保护宏：**`RVISS_<路径>_<文件名>_H`**，全大写、不含前导/尾随连写双下划线。
  理由（详见 Kconfig 那次的讨论）：C 标准 §7.1.3 把 `__xxx` / `_Xxx` 保留给实现，
  程序里定义属 UB；Google 规范用 `<PROJECT>_<PATH>_<FILE>_H_`，QEMU 用同思路的
  路径名并专门写脚本清理保留写法。我们是 `RVISS_CHECKPOINT_CHECKPOINT_H` 这种。
- `#endif /* 保护宏 */` 要写注释（长文件里好定位）。
- **每个头自包含**：单独 include 它能编译通过（不靠"先包含者"提供类型）。
- **只 include 你直接用到的东西**。不靠传递依赖（例子：`image.c` 用 `memcpy`
  就老老实实依赖 `common.h` 里统一提供的 `<string.h>`，而不是"某处顺手带进来的"）。
- 头里不做重量级计算、不放可变定义（变量一律 `extern`）。
- 注释里**不要出现 `/*` 序列**（如写 `boards/*.c`），会提前闭合块注释 —— `-Werror=comment` 会拦。

## 5. 依赖方向（最重要的一节）

```
common → util/host → memory/device → rtl(接口) ← engine(实现)
                                        ↑
                                 target/riscv64
                                        ↑
                        machine → monitor → main
                        difftest / checkpoint / trace / uarch（分析层，只能向下看）
```

硬规则：

1. **下层不许 include 上层**。唯一例外是 `memory/vaddr.c → isa_mmu_translate()`，
   这是"ISA 后端钩子"，已在 `include/memory/vaddr.h` 注明 —— 新增例外必须在文档里登记。
2. **engine 使用 RV64 公开的单步结果**（include/cpu/step.h），不直接处理译码/
   trap 私有语义。RTL 原语仍用于运算；memory/vaddr.c 的 target 访问入口为
   isa_mem_access，规则集中在 RV64。旧“互不认识/JIT 无需改 target”约定已过时。
3. **分析层（trace/uarch）只能订阅事件**，不许 include `target/`。需要新信息时
   改 `include/observe/event.h`（补齐事件字段），而不是去摸 ISA 私有头。

自查手段：`grep -rn "#include \"" src/<子系统>/` 看有没有越界。

## 6. 注释

- 注释解释 **为什么**，不解释"是什么"（代码自己说是什么）。
- 每个文件头写：这个文件负责什么、和谁交互、有什么坑。
- 复杂/反直觉处**必须**注释（例子：`decode.c` 里"`'0'` 位也要置 mask"、
  `machine.c` 里"设备由 Kconfig 决定是否编入"）。
- 未完成的规划用 `TODO(<里程碑>)` 标记（如 `TODO(M3)`），与
  `docs/implementation_plan.md` 的里程碑对应，便于全局检索：
  `grep -rn "TODO(M3)" src/`。
- 中文注释可以（本项目语境）；标识符、日志输出用英文。

## 7. 错误处理：三种错误，三条路

| 错误类型 | 处理方式 | 例子 |
|---|---|---|
| **程序内部 bug**（不可能发生 / 状态坏了） | `panic()` / `Assert()`，带上下文，立即 abort | `mem_in_ram` 断言失败 |
| **客户机错误**（非法指令、缺页、非法 CSR） | 走 ISA 的异常路径（`riscv_raise_trap`），**绝不 abort** | `EX_II`、`EX_LPF` |
| **可预期的外部失败**（文件打不开、内存分配失败） | 返回 `bool`/`NULL`，由调用方决定（日志 + 退出或降级） | `host_read_file` 返回 false |

**绝不静默吞掉**：未实现的路径要么 panic 要么如实报错。
反例（我们明确避免的）：`isa_mmu_translate` 在 Sv39 未实现时**不会**返回原地址假装成功，
而是 panic —— 因为"看起来能跑、语义是错的"比崩溃危险得多。

## 8. 日志

- 用分类宏（`LogInstr`/`LogMem`/`LogTlb`/`LogTrap`/`LogDevice`/`LogDiff`），
  **不要裸 `printf`**（除了交互调试器的用户界面输出）。
- 新增 trace 类别 = 在 `util/log.h` 加宏 + 在 Kconfig 加选项，**不要在 core 里加 `if (config.xxx)`**。
- 高频日志（每指令/每访存）必须放在编译期开关后面（见 `CONFIG_TRACE_*`）。
- 需要被机器解析的统计输出 → CSV/JSON（见第 11 节），不要塞进日志文本。

## 9. 内存与所有权

- **谁分配谁释放**，成对出现在同一个文件里（`mem_init`/`mem_shutdown`、
  `host_read_file` 由调用方 `free`）。
- 不用"延迟释放/全局缓存"这类聪明写法；模拟器的性能瓶颈不在 malloc 次数。
- 可选大数据（RAM、predictor 表）用 `host_mmap_anon`，不走 `malloc`。

## 10. 接口设计：解耦与"优雅"的具体含义

- **窄接口**：一个模块对外暴露的函数越少越好。`engine.h` 只有 3 个函数、
  `device.h` 一个结构体 + 8 个函数 —— 这是刻意压出来的。
- **表驱动优先于分支**：一条 CSR 一行、一条指令一行、一个设备一个 `MmioRegion`。
  加东西 = 加一行/一个文件，而不是加 `else if`。
- **单一事实来源**：指令元信息只在 `instr.def`；事件定义只在 `event.h`；
  机器参数只在板级文件 + Kconfig。改一处就够，改不动的说明抽象错了。
- **回调/观察者用于"变化点"**（trace、uarch、引擎替换），
  **直接调用用于"稳定点"**（内存读写、陷阱）。别为将来可能的扩展提前抽象。
- **可关闭**：任何新增的横切功能（observe/uarch）都要有 Kconfig 开关，关掉时零开销。

## 11. 测试

- 新功能 = 新测试，先写测试再写实现（哪怕是最小的一条 `CHECK_EQ`）。
- 单元测试放 `tests/unit/`（一个文件一个 `main()`）；
  端到端放 `tests/smoke/`（真实汇编 → 运行 → 校验输出）。
- 涉及执行语义的测试用**真实路径**：`test_decode.c` 是写到内存里再走
  `riscv_decode()`，不是调内部小函数 —— 这样测的是真链路。
- 功能不变性检查：加观察者/微架构模型后，必须验证"与关闭时逐指令一致"
  （用 `-t` 日志 diff 即可）。

## 12. 构建与配置

- 加 C 文件不用改 Makefile（按目录 `find`），**但要确认它在某个 `DIRS-y` 里**；
  若该文件受配置控制，用 `SRCS-$(CONFIG_X) += ...`。
- 加配置项：写 Kconfig（带 help 文案）→ 代码 `#ifdef CONFIG_X` → 需要时在 defconfig 里定型。
- 工具（tools/）与主工程的 Makefile 都**自定位 `RVISS_HOME`**，不依赖外部环境变量。

## 13. 禁止事项清单

| 禁止 | 原因 |
|---|---|
| `__XXX_H__` / `_Xxx` 形式的标识符 | C 标准保留命名空间（UB） |
| core 里 include 分析/模型的头 | 依赖倒挂，改动扩散 |
| 让微架构预测反向影响客户机状态 | 功能正确性与性能模型纠缠，验证爆炸 |
| 注释里写 `/*` | 提前闭合块注释（编译错误） |
| 用 `printf` 做 trace | 无法分类/过滤/关闭 |
| 静默 fallback（未实现却返回"看起来对"的值） | 假成功比失败危险 |
| 无测试的新功能、无开关的横切功能 | 不可验证 / 不可关闭 |
