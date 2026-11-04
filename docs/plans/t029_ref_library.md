# T029：外部 difftest ref 库

状态：verified。

## 目标

把 RVISS 做成可链接的 ref，供别人做 difftest。接口形状参考 NEMU 的
`difftest_init` / `memcpy` / `regcpy` / `exec` / `raise_trap`。中断和同步异常共用这一入口。

## 非目标

- 不恢复 Spike bridge、checkpoint、`include/rviss.h`。
- 不兼容 XiangShan 的 `CPU_state` 布局，不提供 store commit、guided exec、向量或 H 扩展。
- ref 不映射 CLINT、PLIC、UART，不在 `difftest_exec` 里自己采样异步中断。

## 约束

- 公开头是 `include/difftest.h`，不暴露内部 `CpuState`。
- `difftest_exec(n)` 的 n 是指令尝试次数。注入的中断和异常不计入尝试，也不退休。
- `regcpy` 是整份状态镜像，不走 CSR 指令的 WARL。`x0` 固定为 0。
- 注入陷阱不看 `mie` 和全局中断使能；委托仍按当前特权级和 `medeleg`/`mideleg`。
- 产物是 `build/librviss_ref.so`。RAM 范围用编译该库时的 `CONFIG_MBASE` / `CONFIG_MSIZE`。

## 验收

`make CC=/usr/bin/gcc ref-check` 链接该库并检查：

- 三条指令后的 GPR、PC、`minstret` 和 store 数据；
- WFI 后再次 exec 不前进，`raise_trap` 带中断位进入 `mtvec`；
- U 态委托的 ecall 进入 `stvec`；
- GPR/FPR/FCSR/satp/PMP/LR 镜像和再次 init 后的 RAM 清零。

## 假设

调用方在注入陷阱前自己对齐特权级和委托位。`mstatus.SD` 只在导出时按 FS/VS/XS 重算。

## 证据

`make CC=/usr/bin/gcc ref-check -j` 退出 0，输出 `ref api ok`。
`nm -D build/librviss_ref.so` 导出 init、memcpy、regcpy、exec、raise_trap、ram_base、ram_size、state_size。

未跑 `make check`：当前树没有 `tests/smoke`。`make CC=/usr/bin/gcc -j` 已重新链接 `build/rviss`，退出 0。
