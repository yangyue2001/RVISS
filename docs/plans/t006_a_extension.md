# T006：RV64 A 2.1 的 LR/SC 与 AMO

状态：verified（2026-10-12）。依赖 T005 verified；Linux/Spike 总目标继续。

## 目标与契约

CONFIG_RV_A 开启后真实 engine_step 执行 LR.W/D、SC.W/D、9 种 AMO 的 W/D，
关闭时为非法。编码和 aq/rl 集中维护，单 hart 顺序功能模型中所有 aq/rl 合法
组合有相同可见结果；不引入宿主并行。核心 C；独立 Spike C++ bridge 不入核心。
固定依据 Ratified-IMAFDQC/src/a.tex A 2.1，官方 blob
582fbe287c4d6d5f1bf8940ecb93f4db565ce1ad 已读取。

访问检查集中于 target/riscv64/mmu/access.c，拆出无数据读写的 prepare，
让 SC 无 reservation 时也检查对齐、翻译和 store 权限；AMO 检查成功后才
读改写、最后写 rd。W 的旧值符号扩展，写数据取低 32 位；signed min/max
用相应操作宽度比较。LR 是 load fault；SC/AMO 是 store/AMO fault。
此 profile 原子仅 RAM，MMIO 不执行 callback 并产生 access fault。

reservation 属于 CPU 状态，reset 清零，每次成功 LR 替换，任意 SC 消耗。
单 hart 无 DMA 当前采用同 hart 普通 store/AMO 保留 reservation 的合法策略，
匹配固定 Spike；以后添加外部 RAM 写入必须接入明确失效接口。
按 LR 的物理起始地址和宽度保存，只保证相同地址/宽度的受限序列成功；
混合宽度/虚拟别名的非受限序列不作为 Spike 必须相同的 oracle。
不能在差异出现后临时掩码 SC 结果。fault/reset/非法 LR 的行为另有固定断言。

## 验收

- [x] 22 编码与 4 个 aq/rl、rd/rs alias/x0、W/D 边界经真实路径执行。
- [x] LR→SC 成功、无 LR/第二 SC/不同地址/新 LR 替换/复位失效；失败 SC 无写。
- [x] 对齐/未映射/越界/MMIO/非法 rs2 fault 不写 rd/RAM，不退休；SC 无 reservation
  仍走访问检查，MMIO callback 没被调用。
- [x] 固定 Spike AMO 边界/受限 LRSC/故障、重复 replay 与已有 I/M 回归通过。
- [x] minimal / MA make check、UBSan 实跑，保留命令、身份、首失败和修复。

不实现 PMP/Sv39（prepare 留明确集中接点），C/F/D/设备完整功能。
新增 MA 配置并在完成后恢复 minimal；CPU_state 增加 reservation 使原生
checkpoint 布局改变，完整 checkpoint 本次总目标不要求，不能宣称兼容。
完成并提交后 T007 C 2.0；随后按总路线继续，直到 Linux 验收完成。

## 实际结果

22 条 A 指令、所有 aq/rl、W/D、rd alias/x0，LR/SC 的成功、失败、不同地址、
最新 LR、第二 SC、reset，以及同 hart 普通 store 保留 reservation 的合法策略
已验证。访问 prepare 与实际读写分开；StepResult.memory.write_performed 记录
成功实际 store，不能把失败 SC 的权限检查当作 store。DUT observer 已更新调用方。

实际命令、哈希、日志与返回码在 docs/validation/t006_checks.json：
minimal 9 unit / 6619 checks；MA 9 unit / 6742 checks；A-only 9 unit / 6934 checks；
三配置各 3 smoke、CLI/UI 14 checks。MA 独立 Spike 982 cases / 9172 events，
A-only 665 / 5156；新进程 replay 一致，三种差异注入仍捕获。
MA 边界组合、所有 alias/排序位、受限 LRSC、fault/非法与 500 条随机 AMO 通过；
结果为 docs/validation/t006_ma_difftest.json 与 t006_a_difftest.json。
UBSan 的 test_a 4540 checks 与完整 I/M/A 差分均退出 0，无报告。

首失败：minimal 的测试常量在关闭 A 时未使用；修复为配置条件声明，未改断言。
首次 AMO 差分在 a-boundary-8-6-3 event 13 发现 raw/length 差异：生成器的
rd=x10 覆盖了数据基址 x10，接着写进程序区，引入长非法指令。双方 RAM/寄存器
此前一致，T004 已排除长指令；本项应是数据 AMO。首包已耐久保存，再将基址
改为与目的寄存器独立的 x31，补全部顺序退休断言；未掩码差异或改变运算期望。

对非整字 RAM 尾部，失败 SC 仍进行完整字宽检查；独立常量单测验证无部分写。
Spike 的无 reservation SC 仅以起始 PA 询问 reservable，不能作为该非整字 PMA
边界的全宽检查 oracle，所以此规则以实际 RVISS 边界测试另记；未扩大 runtime skip。
无 DMA/其它 hart，未做并发 memory ordering 认证。完整 PMP/Sv39/FP/C 尚未验证。
根配置已恢复 minimal；提交后执行 T007 C，总目标 active。
