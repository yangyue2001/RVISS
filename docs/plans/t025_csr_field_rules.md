# T025：CSR 清单与 WARL/WLRL/WPRI 访问体系

状态：slice-1 verified（2026-10-14）。本任务重建 CSR 的读写边界，保留当前 RV64
M/S/U、Zicsr、PMP、Sv39 和 Linux/Spike 调用方的行为契约；新扩展 CSR 仍未开放。

## 目标

将 CSR 处理拆成四层：

1. `csr_machine.h`、`csr_supervisor.h`、`csr_user.h` 只维护规范地址目录和位定义；
2. 实现清单明确当前 RVISS 真正可访问的 CSR，目录中未实现的地址不能因为生成了
   成员就自动变成可访问；
3. CSR 字段规则表达 RW、WPRI、WARL、WLRL 和 RO/ROZ；
4. 所有 CSR 指令、宿主读写接口、别名视图和副作用都经过同一个读写决策路径。

## 行为契约

- WPRI：未实现字段读取为零；写入同一 CSR 的其它字段时保留旧值。
- WARL：任意写值都不产生字段本身的非法指令；提交后读取值必须属于当前合法集合。
  本任务采用确定性的规范化函数，非法值优先保持旧的合法值，若旧值不合法则使用
  该字段的 reset/legal 默认值。
- WLRL：只保证合法写值读回合法。当前实现选择“非法写值保持旧值”的确定性策略，
  不把 WLRL 非法写入强制变成异常；以后若某个规范字段要求异常，可单独覆盖。
- RO/ROZ：CSR 地址编码决定整寄存器只读；未实现的只读/保留字段读取为零，写入
  整个只读 CSR 仍产生非法指令。
- 字段规则在候选值形成后一次性提交；字段处理失败不得留下部分写入，也不得写入
  CSR 指令的目的寄存器。
- 跨 CSR 的副作用（`misa`、`satp`、`mideleg`、PMP 等）继续使用显式处理函数，
  不塞进通用位掩码回调。

## 范围

首个代码切片覆盖当前已经存在真实语义和测试调用方的字段：

- `mstatus` / `sstatus`：MPP、FS、UXL/SXL、保留位；
- `misa`：支持扩展位、C 动态切换和 F/D 依赖；
- `mtvec` / `stvec`、`mepc` / `sepc`；
- `medeleg`、`mideleg`、`mie`、`mip`、`mcounteren`、`scounteren`；
- `satp.MODE/ASID/PPN` 的 Bare/Sv39 WARL；
- PMP 配置和地址的锁定、模式和权限规范化；
- `fcsr` 的已实现 F/D 字段。

新头文件目录中的向量、AIA、NMI、调试、Sstc、Svpbmt、Svnapot 等 CSR 在相应
扩展实现前保持不可访问，不因存在地址宏或 `CPU_state` 存储成员而宣称支持。

## 实现边界

`CsrEntry` 保留为地址/视图/存储索引；新增字段规则描述和统一的
`csr_rules_read/write()` 路径。单个 CSR 的特殊副作用仍集中在
`src/target/riscv64/csr/csr_access.c`，PMP 的字节级规则仍由 `pmp.c` 负责。
运行辅助状态（PMP entry 数量、软件中断影子位、Sv39 能力、LR/SC reservation）
不属于 CSR 字段，继续由 CPU/hart 运行状态单独保存。

## 验收

```text
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t025-csr check
```

必须覆盖：当前 CSR 单元测试、三种 CSR 指令形式、WPRI 保留位、WARL 非法值和
依赖关系、WLRL 非法值确定性、PMP/Sv39 回归；失败时记录首个差异，不调整期望
来取得通过。完成切片后在本文件补充实际命令、结果、未验证项并创建独立 Git 提交。

## Slice-1 实际结果

- 新 U/S/M 头文件作为规范地址目录；`RVISS_CSRS_OWNED` 继续作为当前实现能力
  清单，未实现目录项不会自动进入 `CPU_state` 或 CSR 访问表。
- 增加 `CsrFieldRule` 和统一 `csr_rules_read/write()`，实现 WPRI、WARL、WLRL、
  RO 字段的基本组合规则。
- `mstatus/sstatus`、`misa`、`mtvec/stvec`、`mepc/sepc`、`satp`、委派/中断使能、
  counteren 和 cause 字段接入规则路径；PMP 仍由其显式字节规则处理。
- 修复新清单迁移后的 CPU 复位初始化、Sv39/PMP reset 入口和 CSR 权限判断。

实际验证：

```text
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t025-csr check
```

结果：22 个单元测试（含 `test_csr_rules`）全部通过；单元检查合计 25,879，
smoke 3 个通过，CLI/UI 14 个通过。`git diff --check` 通过。

本切片尚未验证 Linux 启动或 Spike 长矩阵；尚未实现的新头文件 CSR（V、AIA、NMI、
调试、Sstc、Svpbmt、Svnapot 等）仍保持未映射/不可访问。

## 假设和后续

本任务不一次性实现新头文件列出的全部最新特权规范 CSR，也不把所有字段元数据
强行编码成宏参数。等当前 RV64GC/Linux 路径稳定后，再按扩展逐项增加字段规则和
访问测试。
