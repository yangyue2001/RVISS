# T021：诊断日志与性能 Trace 通道

状态：设计完成，待实现（2026-10-13）。

## 结论

当前 RVISS 的 `Log*` 系统适合启动信息、配置、异常、设备和停止统计，
但不适合作为长时间性能 trace：`LogInstr` 是格式不稳定的文本，日志后端
每条记录都 `fflush()`，并且 UART 客户机输出与默认 stdout 混在一起。

因此必须把两个目标分成独立通道：

1. 诊断日志回答“RVISS 为什么这样运行或为什么失败”。它面向人阅读，保留
   等级、类别和文本格式。
2. 性能 Trace 回答“客户机执行了哪些架构事件”。它面向脚本和性能模型，使用
   结构化二进制记录、批量写入和可选块压缩，不能依赖解析诊断文本。

## 当前实现审计

### 诊断日志

- `Log/Logw/Loge` 是 general 类别的 INFO/WARN/ERROR。
- `LogTrap/LogDevice/LogDiff` 是 DEBUG 级别的分类日志。
- `--log FILE` 只控制 RVISS 日志；UART 输出仍直接写 stdout。
- `CONFIG_DEBUG=n` 会在编译期删除所有普通日志，包括 `Loge`。
- `--log-level` 是全局等级过滤；类别开关已有 API，但没有命令行入口。

这满足低频诊断的基本需要，但后续应将错误诊断与 debug 详细信息分开，避免
关闭 DEBUG 后完全没有可见错误；默认输出也应考虑使用 stderr，避免和客户机
串口输出混流。

### 当前 trace

- `CONFIG_TRACE_INST`/`CONFIG_TRACE_INST_DASM` 控制 `LogInstr` 是否编译。
- `-t` 只把运行时等级设为 TRACE，不能恢复编译时删除的调用。
- `TRACE_INST` 与 `TRACE_INST_DASM` 当前实际输出字段相同，后者没有真正增加
  可序列化的操作数信息。
- `TRACE_MEM` 目前主要记录 MMIO，`TRACE_MMU` 没有完整观察点。
- `log_printf()` 每条消息都刷新文件，长时间运行会严重放大宿主 I/O 开销。

所以现有 trace 只适合短程序排错，不应作为 Linux 长跑性能数据格式。

## 目标接口

### 诊断接口

保留现有宏，兼容已有调用点；新增配置语义：

```text
--diag-log FILE       诊断日志文件
--log-level 0..4      诊断等级
--log-category CAT    可重复指定类别（可选）
```

`--log` 可以暂时作为 `--diag-log` 的兼容别名。诊断后端采用有缓冲文本输出，
错误和启动失败在退出前显式刷新。客户机 UART 输出单独保留 stdout 或由未来的
`--uart-output FILE` 重定向。

### 性能 Trace 接口

建议新增独立参数，不再用 `-t` 表示性能数据文件：

```text
--trace-stats FILE       聚合统计，JSON/CSV，适合常开
--trace-file FILE        结构化流式 trace
--trace-events LIST      retire,branch,mem,trap 的事件选择
--trace-sample N         每 N 个退休事件采样一次
--trace-ring N           只保留最近 N 条，异常时转储
```

`-t` 可继续作为短期兼容选项，表示打开人读的文本指令 trace；文档应明确它
不是高吞吐性能采集接口。

## 二进制格式选择

首版使用带版本头的 little-endian RVTR 格式：

```text
Header {
  magic[4] = "RVTR";
  version;
  header_size;
  profile;          /* RV64GC/Sv39 等 */
  config_hash;
  image_hash;
  record_size;
  flags;            /* 事件类型、采样、压缩标志 */
}
Record {
  event_seq;
  retired_total;
  outcome;
  privilege;
  pc_before;
  pc_after;
  raw_instruction;
  instruction_length;
  flags;            /* retired/trap/memory/register */
  memory_type;
  memory_length;
  memory_vaddr;
  memory_paddr;
  memory_data;
  trap_cause;
  trap_tval;
}
```

固定布局记录优先保证写入路径简单和解析稳定；文件空间再通过以下顺序优化：

1. 只记录用户选择的事件类型，性能分析默认 `retire`；
2. 使用 1--4 MiB 内存缓冲，满后一次 `write`/`fwrite`，禁止每条记录刷新；
3. 记录内部对事件序号、PC 和重复字段做 delta/varint 编码；
4. 按 1--4 MiB 分块使用 Zstandard level 1 压缩。

压缩不能在每条指令上调用；应以块为边界压缩，减少函数调用和锁/系统调用。
如果暂时不引入 `libzstd` 链接依赖，首版可以输出 `.rvtr`，运行结束后执行：

```sh
zstd -T0 -1 run.rvtr -o run.rvtr.zst
```

后续再提供内置 zstd sink。不要把宿主 `gzip`/`zstd` 的输出文本当作架构格式；
压缩层必须包在有版本的 RVTR 数据层外面。

## 性能分析的三个采集级别

| 级别 | 数据 | 推荐用途 |
| --- | --- | --- |
| 统计 | 指令类别、热 PC、分支计数、访存计数 | Linux 长跑、回归、热点概览 |
| 采样流 | 每 N 条退休事件和必要访存/分支 | 热点和趋势分析 |
| 完整流 | 每个退休、trap、branch、memory 事件 | 对拍诊断、离线 cache/预测器模型 |

完整流不能宣称提供真实 IPC、周期或 wrong-path 行为；RVISS 是功能模型，
这些结果必须由外部性能模型根据事件流推导并注明假设。

## 实施切片和验收

1. **S1：通道分离**。诊断文本与 trace 文件不再共享 `log_printf` 的输出路径，
   默认运行行为和 Linux/Spike 状态保持一致。
2. **S2：RVTR retire 流**。新增固定头、固定记录、1 MiB 缓冲和解析器；短程序
   记录数、PC、事件序号和停止状态必须与 `StepResult` 一致。
3. **S3：统计与采样**。先实现 JSON/CSV 聚合，再实现采样和 ring；长跑只输出
   小文件，不能因为打开统计改变客户机状态。
4. **S4：压缩 sink**。优先块级 zstd；比较原始 RVTR 与 `.zst` 的大小、写入时间、
   解码记录数和校验值。缺少 zstd 开发库时保留外部压缩流程，不阻塞功能模型。

每个切片都要执行相关 unit/smoke，并运行一次 Linux 启动和 Spike difftest，
证明 trace 开关不会改变体系结构执行。
