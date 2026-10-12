#!/usr/bin/env python3
"""运行同一 FW_PAYLOAD/DTB，或自动验证 PID 1、BusyBox 交互和重复 workload。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def verify_boot(cmd, log, rounds, timeout):
    start = time.monotonic()
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ)
    output = bytearray()
    sent = False
    error = None
    try:
        with log.open("wb") as f:
            while True:
                if time.monotonic() - start > timeout:
                    raise RuntimeError(f"启动/工作负载超时 ({timeout}s)")
                for key, _ in selector.select(0.2):
                    data = os.read(key.fileobj.fileno(), 65536)
                    if not data:
                        selector.unregister(key.fileobj)
                        continue
                    output.extend(data)
                    f.write(data)
                    f.flush()
                text = output.decode(errors="replace").replace("\r", "")
                if "Kernel panic" in text or "Oops:" in text:
                    raise RuntimeError("内核 panic/Oops")
                if not sent and re.search(r"^RVISS_BUSYBOX_READY$", text, re.M):
                    # 串口回显不能算成功；匹配独立的输出行，失败链路不打印通过标记。
                    command = ("/bin/busybox uname -m && "
                               "/bin/busybox echo RVISS_INTERACTIVE_OK && "
                               f"/bin/rviss_workload {rounds} && "
                               "/bin/busybox echo RVISS_LINUX_OK && "
                               "/bin/busybox poweroff -f\n")
                    proc.stdin.write(command.encode())
                    proc.stdin.flush()
                    sent = True
                if proc.poll() is not None and not selector.get_map():
                    break
            required = [r"^RVISS_PID1_OK pid=1$", r"^riscv64$",
                        r"^RVISS_PROBE_OK fp32 fp64 amo lrsc mmap cow fork timer$",
                        r"^RVISS_INTERACTIVE_OK$",
                        rf"^RVISS_WORKLOAD_OK rounds={rounds}$", r"^RVISS_LINUX_OK$"]
            absent = [p for p in required if not re.search(p, text, re.M)]
            if absent or proc.returncode != 0:
                raise RuntimeError(f"验收标记缺失 {absent}；QEMU exit={proc.returncode}")
    except (RuntimeError, OSError) as e:
        error = str(e)
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        selector.close()
        proc.stdin.close()
        proc.stdout.close()
    result = {"passed": error is None, "rounds": rounds,
              "elapsed_seconds": round(time.monotonic() - start, 3),
              "exit_code": proc.returncode, "log": str(log),
              "log_sha256": sha(log)}
    if error:
        result["error"] = error
        print(output.decode(errors="replace")[-8000:], file=sys.stderr)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/linux")
    parser.add_argument("--qemu", default="/home/yang/soft/qemu/qemu/build/qemu-system-riscv64")
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--boots", type=int, default=3)
    parser.add_argument("--rounds", type=int, default=100)
    parser.add_argument("--timeout", type=int, default=180,
                        help="每次运行的宿主秒数预算；1000 轮建议 300")
    args = parser.parse_args()
    if args.boots < 1 or not 1 <= args.rounds <= 100000 or args.timeout < 1:
        parser.error("boots/rounds/timeout 超出范围")
    out = args.out.resolve()
    art = out / "artifacts"
    manifest_file = art / "manifest.json"
    manifest = json.loads(manifest_file.read_text())
    for name, item in manifest["artifacts"].items():
        if sha(art / name) != item["sha256"]:
            raise RuntimeError(f"工件哈希不匹配：{name}，请先重新构建")
    cmd = [args.qemu, "-machine", "virt", "-accel", "tcg", "-cpu",
           manifest["qemu_cpu"], "-smp", "1", "-m", "256M", "-display", "none",
           "-monitor", "none", "-serial", "stdio", "-bios", str(art / "fw_payload.bin"),
           "-dtb", str(art / "rviss_virt.dtb")]
    if not args.verify:
        print("进入串口交互；退出 QEMU 可在 shell 执行 poweroff -f。", flush=True)
        return subprocess.call(cmd)
    (out / "logs").mkdir(exist_ok=True)
    results = []
    for i in range(args.boots):
        log = out / "logs" / f"qemu_{args.rounds}_{i + 1}.log"
        result = verify_boot(cmd, log, args.rounds, args.timeout)
        results.append(result)
        print(json.dumps(result, ensure_ascii=False), flush=True)
        if not result["passed"]:
            break
    report = {"platform": "QEMU（RVISS 尚未验收）", "manifest_sha256": sha(manifest_file),
              "qemu_version": subprocess.check_output([args.qemu, "--version"], text=True).splitlines()[0],
              "command": cmd, "requested_boots": args.boots, "runs": results,
              "passed": len(results) == args.boots and all(r["passed"] for r in results)}
    report_file = out / f"acceptance_qemu_{args.rounds}.json"
    report_file.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(f"验收报告：{report_file}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, ValueError, subprocess.CalledProcessError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
