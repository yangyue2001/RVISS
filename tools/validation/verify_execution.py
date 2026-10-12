"""保存可复现命令/退出码；隔离源码构建，不使用工作区旧对象或修改配置。"""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
evidence = repo / 'build/baseline/t003'
evidence.mkdir(parents=True, exist_ok=True)
stage = Path(tempfile.mkdtemp(prefix='rviss-t003-verify-'))
results = []
for name in ['Makefile', 'Kconfig', 'configs', 'scripts', 'src', 'tests', 'include', 'tools']:
    source, destination = repo / name, stage / name
    if source.is_dir():
        shutil.copytree(source, destination,
                        ignore=shutil.ignore_patterns('build', 'config', 'generated', '__pycache__'))
    else:
        shutil.copy2(source, destination)


def run(name, command, cwd=stage, expected=0, source=None):
    completed = subprocess.run(command, cwd=cwd, input=source, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    (evidence / (name + '.log')).write_text(completed.stdout)
    entry = dict(name=name, command=command, cwd=str(cwd), exit_code=completed.returncode,
                 expected_exit_code=expected, log=name + '.log')
    results.append(entry)
    (evidence / 'results.json').write_text(json.dumps(dict(stage=str(stage), results=results), indent=2) + '\n')
    print(name, 'exit', completed.returncode, 'expected', expected, flush=True)
    print(completed.stdout[-1200:], flush=True)
    assert completed.returncode == expected, entry
    return completed.stdout


build = stage / 'out'
make = ['make', '-j4', 'CC=/usr/bin/gcc', 'BUILD_DIR=' + str(build)]
run('clean-minimal-config', make + ['minimal_defconfig'])
run('clean-minimal-check', make + ['check'])
run('clean-ref-config', make + ['ref_defconfig'])
run('clean-ref-check', make + ['check'])
run('clean-debug-config', make + ['debug_defconfig'])
run('clean-debug-check', make + ['check'])

# 同一真实 ELF 输入，开/关运行期 trace 的架构统计和 UART 字节一致。
cross = ['riscv64-linux-gnu-gcc', '-nostdlib', '-nostartfiles', '-no-pie',
         '-march=rv64im', '-mabi=lp64', '-mcmodel=medany', '-Wl,--build-id=none',
         '-Wl,-T' + str(stage / 'tests/smoke/link.ld'), '-e', '_start']
for case, source in [('uart', 'start.S'), ('fault', 'fault.S'), ('irq', 'irq.S')]:
    elf = build / (case + '.elf')
    run('compile-' + case, cross + ['-o', str(elf), str(stage / 'tests/smoke' / source)])
    outputs = []
    for trace in [False, True]:
        outputs.append(run(case + ('-trace' if trace else '-plain'),
                           [str(build / 'rviss'), '-b', '-n', '200000'] +
                           (['-t'] if trace else []) + [str(elf)]))
    summaries = [re.search(r'retired=(\d+) attempts=(\d+) events=(\d+) logical_ticks=(\d+)', x).groups()
                 for x in outputs]
    assert summaries[0] == summaries[1], summaries
    assert all('HIT GOOD TRAP' in x for x in outputs)
    assert '[TRACE instr' not in outputs[0] and '[TRACE instr' in outputs[1]
    if case == 'uart':
        # trace 可在 UART 字节之间输出；逐行删日志后比较客户机字节。
        uart = [re.sub(r'\[(?:ERROR|WARN |INFO |DEBUG|TRACE) [^\n]*\n', '', x) for x in outputs]
        assert uart[0] == uart[1] == 'Hello from RVISS!\n', uart
    if case == 'fault':
        retired, attempts, events, ticks = map(int, summaries[0])
        assert attempts - retired == 12 and events == attempts and ticks == events
    if case == 'irq':
        retired, attempts, events, ticks = map(int, summaries[0])
        assert events - attempts == 1 and retired == attempts and ticks == events

run('workspace-final-check', ['make', 'CC=/usr/bin/gcc', 'check'], cwd=repo)
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
          for p in [repo / '.config', repo / 'include/generated/autoconf.h',
                    build / 'rviss', build / 'librviss_ref.so']}
(evidence / 'hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')
print('T003 verification passed: minimal/ref/debug, faults, budgets, trace and real MMIO output.')
