#!/usr/bin/env python3
"""生成 compile_commands.json（clangd / 其它 LSP 用的编译数据库）。

由 `make compiledb` 调用，参数全部来自 Makefile（尤其是真实的 CFLAGS），
因此数据库与真实构建**永远一致** —— 不需要 bear/compiledb 之类的抓取工具，
也不会记录到 ccache 包装或重复条目。

设计要点：
  - 收录**全部** src/ 与 tests/ 下的 .c（含被当前 Kconfig 排除的文件），
    这样即使某个设备/模块当前没编进去，clangd 也能正确索引它。
  - 用 JSON 的 "arguments" 数组而不是 "command" 字符串：无需处理 shell 转义。
  - 每条都带上真实的 -I 路径与 -D，clangd 因此能解析 <device/...> 这类包含。
"""

import argparse
import json
import shlex
import sys


def main() -> int:
    ap = argparse.ArgumentParser(description="生成 compile_commands.json")
    ap.add_argument("--dir", required=True, help="项目根目录（JSON 里的 directory）")
    ap.add_argument("--cc", required=True, help="编译器，如 gcc")
    ap.add_argument("--flags", default="", help="编译选项（来自 Makefile 的 CFLAGS）")
    ap.add_argument("--files", default="", help="源文件列表（空白分隔）")
    ap.add_argument("--out", required=True, help="输出路径")
    args = ap.parse_args()

    flags = shlex.split(args.flags)
    files = args.files.split()
    if not files:
        print(
            "错误: 源文件列表为空。请先运行 `make xxx_defconfig`（或 menuconfig）生成 .config。",
            file=sys.stderr,
        )
        return 1

    entries = [
        {
            "directory": args.dir,
            "file": f,
            "arguments": [args.cc, *flags, "-c", f],
        }
        for f in files
    ]

    with open(args.out, "w", encoding="utf-8") as fp:
        json.dump(entries, fp, indent=2)
        fp.write("\n")

    print(f"compile_commands.json: {len(entries)} 个条目 -> {args.out}")
    print(f"  cc      : {args.cc}")
    print(f"  flags   : {' '.join(flags)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
