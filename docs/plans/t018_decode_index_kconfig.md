# T018：扁平指令表索引与分页能力配置

状态：verified（2026-10-12）。

## 范围

- 保留 RV64 指令的扁平 X-macro 表，继续让每条指令只维护一行完整 pattern。
- 在 `riscv_decode_init()` 中从 pattern 生成 opcode/funct3 候选索引。
- 译码时只遍历对应候选桶，最后仍用完整 key/mask 匹配并保持表序优先级。
- 让 Kconfig 文案准确表达 Bare/Sv39 的编译期能力选择；不暴露尚未实现的 Sv48 开关。

## 验收

- GC/Sv39 配置的译码单元和完整 `make check` 通过。
- 关闭 `CONFIG_RV_SV39` 的 Bare-only 配置完整检查通过。
- 初始化日志报告候选池和最大桶大小，证明运行时不再默认扫描整表。
- 任务完成后记录实际命令、结果和未验证项，并提交独立 git 记录。

## 验证记录

- `make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t018-decoder check`：17 个单元、25,676
  项检查，3 个 smoke 和 14 个 CLI/UI 检查全部通过。
- `build/t018-decoder/rviss -b -n 1`：日志报告 `161 instructions, 434 candidate
  entries, max bucket 38`。
- `make CC=/usr/bin/gcc minimal_defconfig` 后执行
  `make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t018-bare check`：Bare-only 配置的 17
  个单元、smoke 和 CLI/UI 检查全部通过；随后恢复 `gc_defconfig`。

未验证项：没有新增 RVV/Sv48 支持；索引对当前 RV64 指令表验证，未来加入新扩展时由
初始化阶段的 32 位 pattern 和固定 opcode 检查及时暴露不符合约束的条目。
