# T029: 客户机 vaddr 到 paddr

Status: slice-verified

## Objective

- Trigger: 客户机取指、load、store 从虚拟地址进入。
- Observable result: 先判断对齐；取指的非对齐直接异常，load/store 由
  `CONFIG_RV_MISALIGNED_TRAP` 或 `CONFIG_RV_MISALIGNED_NATIVE` 决定。
  允许的非对齐访问在访问宽度的对齐边界拆成两段。每一段由 `vaddr_unit`
  决定是否翻译，需要翻译时先检查规范地址，再按运行时 `satp.MODE`
  走 Sv39、Sv48 或 Sv57。页表项和最终数据都走 `paddr_access`。
  它先按 `MemRegion` 查找物理地址，RAM 和外设同一张表。页表访问使用 `kMemPteRead`，只能命中 RAM。`riscv_mmu_translate` 成功时把物理地址放在
  `MemResult.data`。RAM 和 MMIO 在这一层相同。

## Non-goals

- 不改 `MemAccessType` 和 `MemRequest` 的字段。
- 不把 `codex/vaddr-paddr-access` 的编译期 Sv56、逐字节拆分或旧 `MemAccess` 带回 main。
- 不在这一片重接 decode、step 和还在使用旧 `MemAccess` 的 unit。
- 不实现 TLB、Svpbmt、Svnapot，也不在这里接 LR/SC/AMO。

## Constraints

- 成功的 load/fetch 把读到的值放在 `MemResult.data`。store 和异常不提供数据，`data` 为 0。
  `vaddr_access` 和 `paddr_access` 都只接收 `MemRequest*`。虚拟地址或物理地址
  都是 `req->addr`，不另传 `paddr`、`ram_only` 或 `perform`。
- 取指不看 MPRV。load/store 的有效特权由调用方放入 `req->privilege`，
  `vaddr_read/write` 在 M 模式且 `MPRV` 置位时使用 `MPP`。
- 页表访问按 S 模式做 PMP，必须在 RAM，不再进入虚地址翻译。
  请求里的地址就是页表项的物理地址。失败按原访问报 access fault。
  未实现 Svadu。叶页表项缺 A，或 store 缺 D，报 page fault，不改 PTE。
  页表项地址不在 RAM 区域时，由 `paddr_access` 报 access fault。
- 非对齐的两段按顺序调用 `vaddr_unit`。前一段完成后，后一段仍可能异常。
  不做地址空间回绕检查。MMIO 和 RAM 都走这两段。
- 两段长度可以不是 2 的幂。8 字节落在 +1 时是 7 字节和 1 字节。

## Acceptance

- [x] 默认 `CONFIG_RV_MISALIGNED_TRAP`：`/tmp/test_vaddr_trap` 256 checks，0 failures。
- [x] `RVISS_TEST_MISALIGNED_NATIVE`：`/tmp/test_vaddr_native` 282 checks，0 failures。
      两次编译都加了 `-fsanitize=undefined`。缺 A、store 缺 D 为 page fault，PTE 不变。
- [x] 访存文件和 `cpu.c` 用 `-Wall -Wextra -Werror` 通过语法检查。
- 全树 `make check` 仍被无关的进行中改动挡住，不能把切片通过写成整机通过。

## Assumptions

- 未实现的 `satp.MODE` 在需要翻译时视为 page fault，不做恒等映射。
- 非法 `MPP=2` 不是 M，翻译规则按非 U 处理。CSR 写路径仍应拒绝这个编码。
- 当前 profile 的 PMP 是 16 entry。`cpu_reset` 在启用 PMP 时恢复为全 OFF，
  因此 S/U 默认不能访问，直到软件写入 entry，或测试明确把 entry 数设为 0。
