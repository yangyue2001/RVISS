#!/usr/bin/env python3
"""从本地源码构建 RV64GC Linux/BusyBox/OpenSBI；仅写入独立输出目录。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "configs/linux"
OPENSBI_COMMIT = "69a0f0245f39ea3af4685cab4cb2dda90acd17cd"
CPU = ("rv64,priv_spec=v1.12.0,i=true,m=true,a=true,f=true,d=true,c=true,"
       "s=true,u=true,h=false,v=false,sv39=true,sv48=false,sv57=false,"
       "zba=false,zbb=false,zbc=false,zbs=false,zicbom=false,zicbop=false,"
       "zicboz=false,zihintntl=false,zihintpause=false,zawrs=false,zfa=false,"
       "sstc=false,svadu=false,svvptc=false,zihpm=false,pmu-mask=0")


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def tree_sha(path):
    h = hashlib.sha256()
    for p in sorted(path.rglob("*")):
        if p.is_symlink():
            value = "link:" + os.readlink(p)
        elif p.is_file():
            value = sha(p)
        else:
            continue
        h.update((str(p.relative_to(path)) + "\0" + value + "\n").encode())
    return h.hexdigest()


def run(cmd, log, env=None):
    cmd = [str(x) for x in cmd]
    print("+ " + " ".join(cmd), flush=True)
    with open(log, "ab") as f:
        f.write(("\n+ " + " ".join(cmd) + "\n").encode())
        result = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT,
                                env=env)
    if result.returncode:
        print(Path(log).read_text(errors="replace")[-10000:], file=sys.stderr)
        raise RuntimeError(f"命令失败 ({result.returncode})，日志：{log}")


def config_values(path):
    result = {}
    for line in path.read_text().splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            k, v = line.split("=", 1)
            result[k] = v
        elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
            result[line[2:-11]] = "n"
    return result


def verify_config(requested, actual):
    want, got = config_values(requested), config_values(actual)
    wrong = [f"{k}: 要求 {v}，实际 {got.get(k, 'n')}"
             for k, v in want.items() if got.get(k, "n") != v]
    if wrong:
        raise RuntimeError("Kconfig 未满足请求：\n" + "\n".join(wrong))


def snapshot(args, out, env):
    stamp = out / "sources.json"
    if stamp.exists():
        existing = json.loads(stamp.read_text())
        if existing.get("_snapshot_format") == 2:
            for name, dirname in [("linux", "linux-6.11.4"), ("busybox", "busybox-1.34.1"),
                                  ("opensbi", "opensbi")]:
                if existing[name]["original"] != str(args.sources / dirname):
                    raise RuntimeError("输出目录已经固定了不同源码路径，请使用新的 --out")
            return existing
    info = {"_snapshot_format": 2}
    for name, dirname in [("linux", "linux-6.11.4"),
                          ("busybox", "busybox-1.34.1")]:
        source, dest = args.sources / dirname, out / "source" / name
        if not (source / "Makefile").is_file():
            raise RuntimeError(f"源码不存在：{source}")
        dest.mkdir(parents=True, exist_ok=True)
        # 本地源码已有 RV32 构建产物。复制到本输出目录后清理，绝不清理原目录。
        cmd = ["rsync", "-a", "--exclude=.git", "--exclude=*.o",
               "--exclude=*.a", "--exclude=*.ko", "--exclude=*.dtb",
               "--exclude=*.dtbo", "--exclude=.*.cmd", "--exclude=/vmlinux*",
               "--exclude=System.map", "--exclude=Image", "--exclude=Image.gz",
               "--exclude=.config*", "--exclude=/include/config/",
               "--exclude=/include/generated/", "--exclude=generated/",
               "--exclude=_install/", str(source) + "/", str(dest) + "/"]
        run(cmd, out / "logs/snapshot.log", env)
        clean = ["make", "-C", dest, "HOSTCC=/usr/bin/gcc", "mrproper"]
        if name == "linux":
            clean += ["ARCH=riscv", f"CROSS_COMPILE={args.cross}"]
        run(clean, out / "logs/snapshot.log", env)
        info[name] = {"original": str(source), "snapshot": str(dest),
                      "tree_sha256": tree_sha(dest)}
    source = args.sources / "opensbi"
    dest = out / "source/opensbi"
    dest.mkdir(parents=True, exist_ok=True)
    archive = out / "source/opensbi.tar"
    run(["git", "-C", source, "archive", OPENSBI_COMMIT,
         "--output", archive], out / "logs/snapshot.log", env)
    run(["tar", "-xf", archive, "-C", dest], out / "logs/snapshot.log", env)
    info["opensbi"] = {"original": str(source), "snapshot": str(dest),
                       "commit": OPENSBI_COMMIT, "tree_sha256": tree_sha(dest),
                       "note": "git archive 固定提交，不包含原目录未提交补丁"}
    stamp.write_text(json.dumps(info, ensure_ascii=False, indent=2) + "\n")
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path,
                        default=Path("/home/yang/study_dir/riscv_embedded"))
    parser.add_argument("--out", type=Path, default=ROOT / "build/linux")
    parser.add_argument("--cross", default="/usr/bin/riscv64-linux-gnu-")
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    args.sources, out = args.sources.resolve(), args.out.resolve()
    if out == args.sources or args.sources in out.parents:
        raise RuntimeError("输出目录必须与原源码目录分离")
    for d in ["logs", "source", "linux", "busybox", "opensbi", "rootfs", "artifacts"]:
        (out / d).mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(CCACHE_DISABLE="1", KBUILD_BUILD_TIMESTAMP="2026-10-09 00:00:00 +0000",
               KBUILD_BUILD_USER="rviss", KBUILD_BUILD_HOST="baseline",
               KBUILD_BUILD_VERSION="1", SOURCE_DATE_EPOCH="1791504000")
    for tool in [args.cross + "gcc", args.cross + "readelf", "rsync", "dtc", "flex", "bison", "bc"]:
        if not shutil.which(tool):
            raise RuntimeError(f"缺少工具：{tool}")
    sources = snapshot(args, out, env)
    art, rootfs = out / "artifacts", out / "rootfs"
    bb = ["make", "-C", out / "source/busybox", f"O={out / 'busybox'}",
          "HOSTCC=/usr/bin/gcc", f"CROSS_COMPILE={args.cross}",
          "EXTRA_CFLAGS=-march=rv64gc -mabi=lp64d"]
    run(bb + ["allnoconfig"], out / "logs/busybox.log", env)
    # BusyBox 此版本的 allnoconfig 会覆盖 KCONFIG_ALLCONFIG 的布尔值。
    bb_config = out / "busybox/.config"
    requested = config_values(CONFIG / "busybox_min.config")
    lines = []
    for line in bb_config.read_text().splitlines():
        key = line.split("=", 1)[0] if line.startswith("CONFIG_") else ""
        if line.startswith("# CONFIG_") and line.endswith(" is not set"):
            key = line[2:-11]
        if key in requested:
            line = key + "=" + requested.pop(key)
        lines.append(line)
    lines += [k + "=" + v for k, v in requested.items()]
    bb_config.write_text("\n".join(lines) + "\n")
    run(bb + ["oldconfig"], out / "logs/busybox.log", env)
    verify_config(CONFIG / "busybox_min.config", out / "busybox/.config")
    run(bb + [f"-j{args.jobs}"], out / "logs/busybox.log", env)
    run(bb + [f"CONFIG_PREFIX={rootfs}", "install"], out / "logs/busybox.log", env)
    for d in ["dev", "proc", "sys", "tmp", "root", "etc"]:
        (rootfs / d).mkdir(exist_ok=True)
    for src, dest in [("init", "init"), ("workload", "bin/rviss_workload")]:
        shutil.copy2(ROOT / "tools/linux" / src, rootfs / dest)
        (rootfs / dest).chmod(0o755)
    run([args.cross + "gcc", "-static", "-O2", "-march=rv64gc", "-mabi=lp64d",
         "-Wall", "-Wextra", ROOT / "tools/linux/rviss_probe.c", "-o",
         rootfs / "bin/rviss_probe"], out / "logs/probe.log", env)
    devices = out / "devices.list"
    devices.write_text("nod /dev/console 0600 0 0 c 5 1\n"
                       "nod /dev/null 0666 0 0 c 1 3\n"
                       "nod /dev/ttyS0 0600 0 0 c 4 64\n")
    fragment = out / "linux_requested.config"
    fragment.write_text((CONFIG / "rv64gc_min.config").read_text() +
                        f'CONFIG_INITRAMFS_SOURCE="{rootfs} {devices}"\n' +
                        f"CONFIG_INITRAMFS_ROOT_UID={os.getuid()}\n" +
                        f"CONFIG_INITRAMFS_ROOT_GID={os.getgid()}\n")
    kernel = ["make", "-C", out / "source/linux", f"O={out / 'linux'}",
              "ARCH=riscv", f"CROSS_COMPILE={args.cross}", "HOSTCC=/usr/bin/gcc"]
    run(kernel + ["allnoconfig"], out / "logs/linux.log",
        dict(env, KCONFIG_ALLCONFIG=str(fragment)))
    verify_config(fragment, out / "linux/.config")
    run(kernel + [f"-j{args.jobs}", "Image"], out / "logs/linux.log", env)
    shutil.copy2(out / "linux/arch/riscv/boot/Image", art / "Image")
    shutil.copy2(out / "linux/vmlinux", art / "vmlinux")
    shutil.copy2(out / "linux/.config", art / "linux.config")
    shutil.copy2(out / "busybox/.config", art / "busybox.config")
    shutil.copy2(rootfs / "bin/busybox", art / "busybox")
    # 导出与内核内置内容一致的归档，供检查和后续工具使用。
    cpio = out / "linux/usr/initramfs_inc_data"
    shutil.copy2(cpio, art / "initramfs.cpio.gz")
    run(["dtc", "-I", "dts", "-O", "dtb", "-o", art / "rviss_virt.dtb",
         CONFIG / "rviss_virt.dts"], out / "logs/dtb.log", env)
    sbi_src = out / "source/opensbi"
    shutil.copy2(CONFIG / "opensbi_min.config",
                 sbi_src / "platform/generic/configs/rviss_defconfig")
    sbi = ["make", "-C", sbi_src, f"O={out / 'opensbi'}", "PLATFORM=generic",
           "PLATFORM_DEFCONFIG=rviss_defconfig", "PLATFORM_RISCV_XLEN=64",
           "PLATFORM_RISCV_ISA=rv64imac_zicsr_zifencei", "PLATFORM_RISCV_ABI=lp64",
           f"CROSS_COMPILE={args.cross}", "HOSTCC=/usr/bin/gcc",
           "FW_TEXT_START=0x80000000", "FW_PAYLOAD=y", "FW_JUMP=n", "FW_DYNAMIC=n",
           "FW_PAYLOAD_OFFSET=0x200000", f"FW_PAYLOAD_PATH={art / 'Image'}",
           f"FW_FDT_PATH={art / 'rviss_virt.dtb'}", "FW_PAYLOAD_FDT_ADDR=0x8f000000"]
    run(sbi + [f"-j{args.jobs}"], out / "logs/opensbi.log", env)
    verify_config(CONFIG / "opensbi_min.config", out / "opensbi/platform/generic/kconfig/.config")
    for name in ["fw_payload.bin", "fw_payload.elf"]:
        shutil.copy2(out / "opensbi/platform/generic/firmware" / name, art / name)
    shutil.copy2(out / "opensbi/platform/generic/kconfig/.config", art / "opensbi.config")
    header = (art / "Image").read_bytes()[:64]
    image_size = struct.unpack_from("<Q", header, 16)[0]
    if header[48:56] != b"RISCV\0\0\0" or not image_size:
        raise RuntimeError("Linux Image header 不符合预期")
    if 0x80200000 + image_size >= 0x8f000000:
        raise RuntimeError("Linux 有效映像区间与 DTB 冲突")
    # ELF 属性与静态链接证据保存，不能只凭 -march 声称未使用额外扩展。
    for name in ["busybox", "fw_payload.elf", "vmlinux"]:
        run([args.cross + "readelf", "-h", "-A", "-l", art / name],
            out / "logs" / (name + ".readelf.log"), env)
    manifest = {"profile": "rv64gc-single-hart-linux", "sources": sources,
                "toolchain": subprocess.check_output([args.cross + "gcc", "--version"], text=True).splitlines()[0],
                "isa": "RV64IMAFDC_Zicsr_Zifencei + Zicntr, M/S/U, Sv39",
                "busybox_abi": "lp64d, static", "opensbi_abi": "lp64",
                "layout": {"ram_base": "0x80000000", "ram_bytes": 268435456,
                           "firmware_entry": "0x80000000", "kernel_entry": "0x80200000",
                           "kernel_image_size": image_size, "dtb_address": "0x8f000000"},
                "qemu_cpu": CPU,
                "bootargs": "console=ttyS0,115200 earlycon=uart8250,mmio,0x10000000 rdinit=/init loglevel=6",
                "artifacts": {p.name: {"sha256": sha(p), "bytes": p.stat().st_size}
                              for p in sorted(art.iterdir()) if p.is_file() and p.name != "manifest.json"},
                "inputs": {str(p.relative_to(ROOT)): sha(p) for p in
                           sorted(CONFIG.iterdir()) + [ROOT / "tools/linux/init", ROOT / "tools/linux/workload",
                                                       ROOT / "tools/linux/rviss_probe.c", Path(__file__)]}}
    (art / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    print(f"构建完成：{art / 'fw_payload.bin'}\n清单：{art / 'manifest.json'}")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
