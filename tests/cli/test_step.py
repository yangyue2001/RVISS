#!/usr/bin/env python3
"""用真实 CLI/UI 路径验证预算及暂停后的统计；不依赖嵌入 API 骨架。"""
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    binary = str(Path(sys.argv[1]).resolve())
    checks = 0
    with tempfile.TemporaryDirectory(prefix="rviss-step-cli-") as directory:
        image = Path(directory) / "case.bin"

        def run(instructions, args, stdin=None):
            image.write_bytes(b"".join(struct.pack("<I", insn) for insn in instructions))
            result = subprocess.run([binary, "--memory", "4K", *args, str(image)],
                                    input=stdin, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=10)
            assert result.returncode == 0, result.stdout
            return result.stdout

        def totals(output, retired, attempts, events):
            nonlocal checks
            expected = f"retired={retired} attempts={attempts} events={events} logical_ticks={events}"
            assert expected in output, output
            checks += 1

        # 取指 fault 循环耗尽尝试预算，不能假报成功退出或退休。
        output = run([0xFFFFFFFF], ["-b", "--max-instr", "5"])
        totals(output, 0, 5, 5)
        assert "STOPPED" in output and "HIT GOOD TRAP" not in output, output
        assert "event=1 retired=0 synchronous_trap" in output, output
        assert "cause=0x0000000000000002 tval=0x00000000ffffffff" in output, output
        assert "event=2 retired=0 synchronous_trap" in output, output
        assert "cause=0x0000000000000001 tval=0x0000000000000000" in output, output
        checks += 5

        output = run([0xFFFFFFFF], ["-b", "--max-events", "3"])
        totals(output, 0, 3, 3)
        output = run([0xFFFFFFFF], ["-b", "--max-events", "4", "--max-instr", "2"])
        totals(output, 0, 2, 2)

        output = run([0x0005006B], ["-b", "--max-instr", "1"])
        totals(output, 1, 1, 1)
        assert "HALTED" in output and "HIT GOOD TRAP" in output, output
        checks += 1

        # si 每次消耗一个尝试；故障后仍可用 info r 检查目的寄存器未提交。
        output = run([0x04200293, 0x00033283], [], "si 1\nsi 1\ninfo r\nq\n")
        totals(output, 1, 2, 2)
        assert "retired=1 attempts=2 events=2" in output, output
        assert re.search(r"x5\s*/t0\s*= 0x0000000000000042", output), output
        assert "HALTED" not in output and "HIT GOOD TRAP" not in output, output
        checks += 3

    print(f"CLI/UI execution boundary: {checks} checks, 0 failures")


if __name__ == "__main__":
    main()
