#!/usr/bin/env bash
# 冒烟测试：汇编小程序 → 装入 RAM → 执行 → 校验输出与退出状态
# 用法: run.sh <rviss 可执行文件>
#
# 用例：
#   smoke.elf  UART 输出 + 正常退出（覆盖取指/译码/执行/MMIO）
#   irq.elf    CLINT 定时器中断 → 陷阱 → mret（覆盖中断线接线与陷阱路径）

set -euo pipefail

RVISS=${1:?用法: run.sh <rviss 可执行文件>}
DIR=$(cd "$(dirname "$0")" && pwd)
CROSS=${CROSS_COMPILE:-riscv64-linux-gnu-}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# run_case <用例名> <源文件> <期望出现在输出里的字符串（可为空）>
run_case() {
    local name=$1 src=$2 expect=$3
    "${CROSS}gcc" -nostdlib -nostartfiles -no-pie \
        -march=rv64im -mabi=lp64 -mcmodel=medany \
        -Wl,--build-id=none -Wl,-T"$DIR/link.ld" -e _start \
        -o "$OUT/$name.elf" "$DIR/$src"

    set +e
    local output
    output=$(timeout 20 "$RVISS" -b -n 200000 "$OUT/$name.elf" 2>&1)
    local status=$?
    set -e

    if ! grep -q "HIT GOOD TRAP" <<<"$output"; then
        echo "冒烟测试失败（$name）：没有 HIT GOOD TRAP（退出码 $status）"
        echo "$output"
        exit 1
    fi
    if [ "$status" -ne 0 ]; then
        echo "冒烟测试失败（$name）：退出码 $status"
        exit 1
    fi
    if [ -n "$expect" ] && ! grep -q "$expect" <<<"$output"; then
        echo "冒烟测试失败（$name）：输出里没有 '$expect'"
        echo "$output"
        exit 1
    fi
    echo "  [$name] 通过"
}

run_case smoke start.S "Hello from RVISS!"
run_case irq   irq.S   ""
run_case fault fault.S ""

echo "冒烟测试通过（smoke：UART 输出；irq：定时器中断；fault：精确同步异常）"
