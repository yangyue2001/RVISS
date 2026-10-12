#!/usr/bin/env python3
"""固定 Spike 源码与独立桥。外部网络失败不能降级到来源未知的 .so。"""
from pathlib import Path
import hashlib
import json
import subprocess
import tarfile
import argparse

ROOT = Path(__file__).resolve().parents[2]
COMMIT = 'da2f415bb62bec3e93b33b379d787b4262aade9f'
ARCHIVE_SHA256 = '1d82e498671cf82279c28d8e46c6fa85e3cd14b7c5c18e24707ed628c8b8c6b5'
OUT = ROOT / 'build/spike'
SOURCE = OUT / ('riscv-isa-sim-' + COMMIT)
OBJ = OUT / 'obj'


def run(command, name, cwd=ROOT):
    print(' '.join(map(str, command)), flush=True)
    with (OUT / (name + '.log')).open('w') as log:
        subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    archive = OUT / 'source.tar.gz'
    if not archive.exists():
        run(['curl', '--silent', '--show-error', '--location',
             f'https://codeload.github.com/riscv-software-src/riscv-isa-sim/tar.gz/{COMMIT}',
             '-o', str(archive)], 'download')
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == ARCHIVE_SHA256
    if not SOURCE.exists():
        root = OUT.resolve()
        with tarfile.open(archive) as tar:
            for member in tar.getmembers():
                target = OUT / member.name
                if not target.resolve().is_relative_to(root):
                    raise RuntimeError('unsafe source archive member')
                if member.issym() or member.islnk():
                    # 固定归档含库内相对链接（spike_dasm → fesvr）；
                    # 只拒绝解析后逃出输出目录的链接。
                    base = target.parent if member.issym() else OUT
                    if not (base / member.linkname).resolve().is_relative_to(root):
                        raise RuntimeError('unsafe source archive link')
            tar.extractall(OUT)
    OBJ.mkdir(exist_ok=True)
    # 不生成大体积 debug 信息；首次人工构建的 -g 对象已 strip-debug 后归档。
    if not (OBJ / 'Makefile').exists():
        run([str(SOURCE / 'configure'), '--prefix=' + str(OUT / 'install'),
             '--without-boost', '--without-boost-asio', '--without-boost-regex',
             'CXX=/usr/bin/g++', 'CC=/usr/bin/gcc', 'CXXFLAGS=-O2'], 'configure', OBJ)
    libraries = ['libriscv.a', 'libsoftfloat.a', 'libdisasm.a', 'libfesvr.a', 'libfdt.a']
    run(['make', '-j' + str(args.jobs), *libraries], 'build', OBJ)
    includes = [OBJ] + [SOURCE / name for name in ['riscv', 'fesvr', 'softfloat']]
    command = ['/usr/bin/g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror']
    command += ['-I' + str(path) for path in includes]
    command += [str(ROOT / 'tools/spike/bridge.cc'), '-Wl,--start-group']
    command += [str(OBJ / name) for name in libraries]
    command += ['-Wl,--end-group', '-lpthread', '-ldl', '-o', str(OUT / 'bridge')]
    run(command, 'bridge-link')
    manifest = dict(commit=COMMIT, archive_sha256=ARCHIVE_SHA256,
                    upstream='https://github.com/riscv-software-src/riscv-isa-sim',
                    source_modified=False, boost=False, compiler='/usr/bin/g++',
                    pmpregions='case input: 0 or 16', pmp_granularity=4, misaligned=False, isa='case input; initially rv64i_zicsr_zifencei',
                    fetch_observer='actual mmio_fetch bytes plus FETCH memtracer; no speculative prefetch',
                    priv='MSU', mmu='case input: clear default with SBARE, then enable only Sv39 when selected', asid_bits=0,
                    ad_policy='Sv39 reference uses Svadu plus initial menvcfg.ADUE=1 to select Priv1.12 hardware update; RVISS does not advertise Svadu',
                    store_observer='actual last successful physical callback plus explicit commit log; implicit A/D writes included in RAM hash',
                    fp_initialization='shared FPR bits; FLEN32 boxed to private Spike 128-bit representation; initial FP CSR writes bracketed by temporary FS enable/restore, before execution', counters='mcycle event policy differs; retirement observed from actual commit log, independent guest minstret; mcountinhibit read-only zero',
                    bridge_sha256=hashlib.sha256((OUT / 'bridge').read_bytes()).hexdigest(),
                    configure_args=(OBJ / 'config.status').read_text().split('ac_cs_config=')[1].splitlines()[0])
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Spike source and bridge verified; manifest:', OUT / 'manifest.json')


if __name__ == '__main__':
    main()
