# T028：CSR 描述符、地址检查与别名

状态：slice-1 active（2026-10-20）。本任务替换“地址表 + 大 switch”的访问路径。
已验证的 CSR 语义仍以 T008/T025 为准；本切片只建立新的定义和调用约定，
不宣称尚未迁过来的 CSR 已经重新验收。

## 目标

定义一条 CSR 只写清单里的一行。访问固定走：

```text
csr_addr_legal(addr, write)     地址类是否允许
        | 失败 -> 非法，不再查表
        v
csr_lookup(addr) -> CsrDesc*    这个地址有没有实现
        | NULL -> 非法
        v
desc->check                     地址位表达不了的额外检查；NULL 表示通过
        v
desc->read / desc->write        NULL 则走默认读写
```

外部仍保留 `csr_read(addr)` / `csr_write(addr)` / `csr_peek(addr)`。
指令路径以后先拿到 `CsrDesc`，再调用 `csr_desc_read/write`；不要在每个
CSR 函数里重复做特权级和只读编码检查。

## 参考里采用和不采用的部分

Spike `csr_t::verify_permissions`（`riscv/csrs.cc`）先用地址位判断
`csr[9:8]` 特权和 `csr[11:10]==3` 只读，具体值的 WARL 放在 `unlogged_write`。
`sstatus` 不是第二份状态：`sstatus_proxy_csr_t` 持有 `mstatus`，读用
`sstatus_read_mask`，写合并后再调用 `mstatus->write`。有虚拟化时外面再包
一层 `virtualized_csr_t`。这套别名方向采用；C++ 类层次不采用。

QEMU `riscv_csr_operations` 是按 12 位地址直接索引的函数表，`read`/`write`
为空时有统一回退。表驱动和“函数指针为空走默认”采用。QEMU 的检查顺序是
“表项存在 -> 只读编码 -> predicate -> 特权级”；本任务按上面的顺序，先做
地址类检查。当前没有 H，两种顺序对 M/S/U 都表现为非法指令。引入 H 时要按
特权规范重排 virtual instruction 与 illegal instruction 的优先级，不能沿用
本顺序。

不采用 QEMU 的 4096 项大型 ops 对象，也不把未实现地址注册成空操作。
未实现就是查表得到 NULL。

## 结构

```c
typedef struct CsrDesc {
  const char *name;
  word_t addr;
  word_t *field;          /* 真正的存储；计算型 CSR 为 NULL */
  word_t read_mask;       /* 默认读：*field & read_mask */
  word_t write_mask;      /* 默认写：只替换这些位 */
  CsrCheckFn check;       /* NULL：无额外检查 */
  CsrReadFn read;         /* NULL：csr_default_read */
  CsrWriteFn write;       /* NULL：csr_default_write */
} CsrDesc;
```

默认读写要求 `field != NULL`，并且 CSR 值的位和存储里的位位置相同。
`frm` 这种需要移位的视图、`sie` 这种写掩码依赖 `mideleg` 的视图、
以及任何 WARL/副作用，都必须写自己的函数，不能只填静态掩码。

清单形态：

```c
#define RVISS_CSR_IMPL(X)                                             \
  X(mstatus, CSR_MSTATUS, &cpu.mstatus, ~0ull, ~0ull, NULL,           \
    csr_read_mstatus, csr_write_mstatus)                              \
  X(sstatus, CSR_SSTATUS, &cpu.mstatus, ~0ull, ~0ull, NULL,           \
    csr_read_sstatus, csr_write_sstatus)                              \
  X(fflags, CSR_FFLAGS, &cpu.fcsr, 31, 31, csr_check_fp, NULL,        \
    csr_write_fp)
```

同一份宏展开两次：一次生成 `CsrDesc` 常量，一次生成
`const CsrDesc *csr_map[4096]` 的指定初始化。地址重复会在编译期冲突。
规范地址目录仍在 `include/cpu/csr_*.h`；目录里有名字不等于已经实现。

## mstatus 与 sstatus

只有 `cpu.mstatus` 这一份存储，不存在 `cpu.sstatus`。

- `sstatus` 读：`csr_read_mstatus()` 再与 `SSTATUS_MASK` 相与。
- `sstatus` 写：先 `(old & ~SSTATUS_WRITABLE_MASK) | (value & SSTATUS_WRITABLE_MASK)`，
  然后调用 `csr_write_mstatus()`。M 侧位因此不会被 S 视图写掉。
- WARL/派生位只写在 `csr_write_mstatus` / `csr_status_value`：MPP 的保留编码
  保持旧值，UXL/SXL 固定 RV64，SD 由 FS==Dirty 派生，VS/XS/endian 当前不实现。
- `fflags`/`frm`/`fcsr` 同样指向 `cpu.fcsr`。`fflags` 位位置一致，可以用默认读；
  `frm` 要移位，所以读写都是自定义函数。写任一视图把 FS 置 Dirty。

`sie`/`sip` 以后按同一模式指向 `mie`/`mip`，但掩码含 `mideleg`，不能用
`CSR` 静态 `write_mask`。`cycle` 指向 `mcycle` 的只读地址视图时，写在
`csr_addr_legal` 就被拒绝，不必再写一个空的 write。

## 非目标

- 不在本切片迁移 PMP、counter、deleg、tvec/epc、satp 和 CSR 指令执行。
  这些仍要保留 T008/T025 的可见行为，等对应存储字段和 worktree 里的
  decode 接口稳定后逐项挂到 `RVISS_CSR_IMPL`。
- 不把目录里的 H/V/AIA/调试 CSR 注册进表。
- 不把字段规则引擎塞进默认读写。默认函数只做掩码；WARL 留在该 CSR 的写函数。
- 不改客户机可见的特权编码。地址类检查没有新的异常原因。

## 验收

当前 worktree 还在改 CPU 状态和解码，整机 `make check` 不是本切片的通过条件。
2026-10-20 已用 `-Wall -Wextra -Werror -fsyntax-only` 编译 `csr.c`，并用独立
驱动程序检查了地址特权、只读地址、未实现地址、`sstatus` 不改 M 位、SD 派生和
`fflags`/`frm` 共享 `fcsr`。`make check` 未跑。后续每迁入一类 CSR，补对应 unit，
再跑：

```text
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t028-csr check
```

必须覆盖：M/S/U 对地址特权位的拒绝、只读地址的写拒绝、未实现地址拒绝、
`sstatus` 写不改变 M 侧位、`sstatus` 读不到 M 侧位、FS Dirty 派生 SD。
不得用放宽旧断言来获得通过。
