# T022：RV64 指令执行路径说明

状态：verified（2026-10-13）。

## 任务契约

根据当前源码写出一份可从头跟读的说明，覆盖：

- `monitor -> engine -> riscv_step -> riscv_decode -> exec -> complete_event` 调用链；
- `Decode`、`Operand`、`InstrEntry` 和 `StepResult` 每个字段的用途；
- 扁平指令表、pattern 编译、opcode/funct3 bucket 和完整匹配；
- 16 位 C 指令展开为 32 位执行编码的过程；
- 访存、同步异常、异步中断、宿主错误和退休之间的边界；
- 说明 `Decode` 字段多的原因及当前实现边界。

主文档：[rv64_instruction_execution.md](../rv64_instruction_execution.md)。

## 检查点与证据

| 检查点 | 结果 |
| --- | --- |
| 源码检查 | 已核对 `include/cpu/decode.h`、`include/cpu/step.h`、`src/target/riscv64/decode.c`、`src/target/riscv64/step.c`、`src/engine/interpreter/interp.c` 和代表性指令/访存实现 |
| 译码边界 | 文档覆盖 16/32 位取指、第二 halfword fault、非法编码和 C 展开 |
| 提交边界 | 文档区分 target 的 `riscv_step()` 与引擎的 `complete_event()` |
| 回归 | `make CC=/usr/bin/gcc BUILD_DIR=build/t022 check` |
| 文档格式 | `git diff --check` |

本任务只增加说明文档，没有改变模拟器代码或测试期望。
