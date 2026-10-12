#!/usr/bin/env python3
"""Verify the fixed FW_PAYLOAD on RVISS with real UART input and guest shutdown."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

from run_qemu import sha, verify_boot

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/linux')
    parser.add_argument('--rviss', type=Path, default=ROOT / 'build/t014-device/rviss')
    parser.add_argument('--boots', type=int, default=3)
    parser.add_argument('--rounds', type=int, default=100)
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--max-instr', type=int, default=2000000000)
    parser.add_argument('--replay', type=Path)
    args = parser.parse_args()
    if args.boots < 1 or not 1 <= args.rounds <= 100000 or args.timeout < 1 or args.max_instr < 1:
        parser.error('invalid budget')
    out, binary = args.out.resolve(), args.rviss.resolve()
    manifest_file = out / 'artifacts/manifest.json'
    manifest = json.loads(manifest_file.read_text())
    for name, item in manifest['artifacts'].items():
        if sha(out / 'artifacts' / name) != item['sha256']:
            raise RuntimeError(f'artifact identity mismatch: {name}')
    identity = sha(binary)
    cmd = [str(binary), '-b', '--memory', '256M',
           '--max-instr', str(args.max_instr), '--max-events', str(2 * args.max_instr + 1),
           str(out / 'artifacts/fw_payload.bin')]
    results = []
    prefix = 'rviss_replay' if args.replay else 'rviss'
    for i in range(args.boots):
        log = out / 'logs' / f'{prefix}_{args.rounds}_{i + 1}.log'
        input_log = out / 'logs' / f'rviss_{args.rounds}_{i + 1}.input'
        run_cmd = cmd + (['--uart-replay', str(args.replay.resolve())] if args.replay else
                         ['--uart-stdin', '--uart-record', str(input_log)])
        result = verify_boot(run_cmd, log, args.rounds, args.timeout)
        result['command'] = run_cmd
        result['input_sha256'] = sha(args.replay or input_log)
        text = log.read_text(errors='replace').replace('\r', '')
        match = re.search(r'retired=(\d+) attempts=(\d+) events=(\d+) logical_ticks=(\d+)', text)
        if match:
            result['counters'] = dict(zip(['retired', 'attempts', 'events', 'logical_ticks'], map(int, match.groups())))
        result['guest_shutdown'] = 'stop reason: HALTED' in text and 'halt_code = 0' in text
        result['passed'] &= result['guest_shutdown']
        if not result['guest_shutdown']:
            result['error'] = result.get('error', '') + '; missing guest finisher shutdown'
        results.append(result)
        print(json.dumps(result, ensure_ascii=False), flush=True)
        if not result['passed']:
            break
    if identity != sha(binary):
        raise RuntimeError('RVISS binary changed during acceptance')
    report = {'platform': 'RVISS', 'rviss_sha256': identity,
              'manifest_sha256': sha(manifest_file), 'command': cmd,
              'requested_boots': args.boots, 'runs': results,
              'passed': len(results) == args.boots and all(r['passed'] for r in results)}
    path = out / f'acceptance_{prefix}_{args.rounds}.json'
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(f'Report: {path}', flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        sys.exit(1)
