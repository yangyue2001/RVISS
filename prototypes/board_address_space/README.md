# RVISS board/address-space prototype

这是一个独立的小原型，用来验证下面的分层关系：

```text
board config -> MemObject -> MemRegion -> AddressSpace -> physical read/write
```

原型不包含在 RVISS 主 Makefile 中，也不修改现有的 `src/`、`include/` 和测试。
它只使用 C11 标准库，便于先验证数据结构和配置格式，再决定是否迁移到正式实现。

## 配置文件

`configs/virt.board` 描述一台板子：

```text
board virt
reset_pc 0x80000000
object ram0 RAM
object uart0 UART
region ram_map ram0 0x80000000 0x20000 main rwx 1,2,4,8 4,8 split 1 1
region uart_map uart0 0x10000000 0x100 io rw 1 - trap 0 0
```

`object` 定义行为和状态实例；`region` 把对象放入物理地址空间，并为这段地址指定 PMA。

## 构建和运行

```sh
make CC=/usr/bin/gcc test
```

示例会验证：RAM 读写、UART 读写、PMA 宽度检查、未映射地址和区域重叠检查。

## 文件布局

| 文件 | 作用 |
|---|---|
| `include/memory_model.h` | `MemOps`、`MemObject`、`MemRegion`、`AddressSpace`、PMA 类型 |
| `include/board_config.h` | 板卡配置的内存表示和配置文件解析接口 |
| `include/board_runtime.h` | 根据配置实例化对象并建立地址空间 |
| `src/address_space.c` | 地址查找、重叠检查、PMA 检查和读写分发 |
| `src/board_config.c` | 简单文本配置解析器 |
| `src/board_runtime.c` | RAM、ROM、UART 对象实例化和区域装配 |
| `src/main.c` | 最小可运行验证 |

这个原型只负责物理地址空间，不实现虚拟地址翻译、PMP、CSR 或设备中断。
