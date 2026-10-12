# T006：RV64 A 2.1 的 LR/SC 与 AMO

状态：active（2026-10-12）。依赖 T005 verified；Linux/Spike 总目标继续。

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

- [ ] 22 编码与 4 个 aq/rl、rd/rs alias/x0、W/D 边界经真实路径执行。
- [ ] LR→SC 成功、无 LR/第二 SC/不同地址/新 LR 替换/复位失效；失败 SC 无写。
- [ ] 对齐/未映射/越界/MMIO/非法 rs2 fault 不写 rd/RAM，不退休；SC 无 reservation
  仍走访问检查，MMIO callback 没被调用。
- [ ] 固定 Spike AMO 边界/受限 LRSC/故障、重复 replay 与已有 I/M 回归通过。
- [ ] minimal / MA make check、UBSan 实跑，保留命令、身份、首失败和修复。

不实现 PMP/Sv39（prepare 留明确集中接点），C/F/D/设备完整功能。
新增 MA 配置并在完成后恢复 minimal；CPU_state 增加 reservation 使原生
checkpoint 布局改变，完整 checkpoint 本次总目标不要求，不能宣称兼容。
完成并提交后 T007 C 2.0；随后按总路线继续，直到 Linux 验收完成。
