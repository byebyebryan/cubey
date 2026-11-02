#!/usr/bin/env python3
"""Bounded, serial edit-service controls. Never overwrites a prior cohort."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
OLD = ROOT / 'outputs/sdf/interactive-edit-loop-20261031/qualified-v1/service/service.json'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def identity(binary):
    baseline = json.loads((ROOT / 'outputs/sdf/interactive-edit-loop-20261031/baseline.json').read_text())
    shader_dir = ROOT / 'build/release/projects/sdf/shaders'
    public = shader_dir.parent / 'sdf'
    assert digest(public) == baseline['binaries']['sdf'], 'public executable changed'
    assert all(digest(shader_dir / n) == h for n, h in baseline['original_shaders'].items()), 'protected SPV changed'
    paths = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard',
                                    'projects/sdf'], cwd=ROOT, text=True).splitlines()
    return dict(source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                source_status=subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True),
                source_files={p: digest(ROOT / p) for p in paths if (ROOT / p).is_file()},
                binary=str(binary), binary_sha256=digest(binary), harness_sha256=digest(__file__),
                shaders={p.name: digest(p) for p in sorted(shader_dir.glob('*.spv'))},
                public_executable_unchanged=True, protected_shaders_unchanged=len(baseline['original_shaders']))


def run(command, output):
    start = time.monotonic()
    with output.with_suffix('.log').open('w') as log:
        process = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=180)
    log = output.with_suffix('.log').read_text()
    if process.returncode or 'VUID-' in log or 'Validation Error' in log:
        raise RuntimeError(log[-4000:])
    native = json.loads(output.read_text())
    assert native['complete']
    if native['drained']:
        assert native['canonical_cache_equal']
    frames = [f['scheduled_to_completion_ms'] for f in native['frames']]
    return dict(command=command, exit_code=process.returncode,
                process_wall_seconds=time.monotonic() - start,
                log_sha256=digest(output.with_suffix('.log')), native=native,
                stats=dict(median_frame_ms=statistics.median(frames), max_frame_ms=max(frames),
                           over_16_67=sum(f > 16.67 for f in frames)))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--groups', type=int, default=512)
    p.add_argument('--budget', type=float, default=4)
    p.add_argument('--repeats', type=int, default=3)
    p.add_argument('--validation', choices=['paired', 'on', 'off'], default='paired')
    p.add_argument('--preparation', choices=['legacy', 'palette', 'sealed'], default='legacy')
    p.add_argument('--pooled', action='store_true')
    p.add_argument('--view', choices=['preset', 'close'], default='preset')
    p.add_argument('--subjects', nargs='+', choices=['guardian', 'robot'], default=['guardian', 'robot'])
    args = p.parse_args()
    args.binary = args.binary.resolve(); args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    original = json.loads(OLD.read_text())
    templates = {(r['subject'], r['operation']): r for r in original['runs'] if r['budget_ms'] == 4}
    receipt = dict(format='cubey.sdf.edit-service-cohort', version=1, identity=identity(args.binary),
                   controls=dict(groups=args.groups, budget_ms=args.budget, preparation=args.preparation,
                                 pooled=args.pooled, validation=args.validation, view=args.view),
                   runs=[], complete=False)
    save(args.output / 'cohort.json', receipt)
    for repeat in range(args.repeats):
        for subject in args.subjects:
            for operation in ('add', 'cut'):
                source = templates[subject, operation]
                old_command = source['command']
                source_input = Path(old_command[old_command.index('--edit-input') + 1])
                data = json.loads(source_input.read_text())
                data['service'].update(groups=args.groups, budget_ms=args.budget,
                                       preparation=args.preparation, pooled=args.pooled)
                modes = ([True, False] if repeat % 2 == 0 else [False, True]) if args.validation == 'paired' else [args.validation == 'on']
                for validation in modes:
                    name = f'{subject}-{operation}-{repeat}-validation-{int(validation)}'
                    inp, out = args.output / (name + '-input.json'), args.output / (name + '.json')
                    save(inp, data)
                    command = old_command.copy(); command[0] = str(args.binary)
                    command[command.index('--edit-input') + 1] = str(inp)
                    command[command.index('--edit-output') + 1] = str(out)
                    command[command.index('--require-validation')] = '--require-validation' if validation else '--no-validation'
                    if args.view == 'close': command += ['--distance', '.8']
                    result = run(command, out)
                    receipt['runs'].append(dict(subject=subject, operation=operation, repeat=repeat,
                                                validation=validation, input_sha256=digest(inp), **result))
                    save(args.output / 'cohort.json', receipt)
                    n = result['native']
                    print(name, 'replacement', n['replacement_completed_ms'],
                          'frame max', result['stats']['max_frame_ms'], 'pass', n['service_passed'], flush=True)
    receipt['complete'] = True
    save(args.output / 'cohort.json', receipt)


if __name__ == '__main__': main()
