#!/usr/bin/env python3
"""Bounded interactive-edit followup; native GPU jobs are always serial."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import statistics

import cached_edit_loop as edit
import local_edit_loop as local
from query_study import ROOT, digest

RAW = ROOT / 'outputs/sdf/interactive-edit-loop-20261031'
OLD = ROOT / 'outputs/sdf/local-edit-loop-20261030/stroke-inputs-v3'


def save(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def identity(args):
    baseline = json.loads((RAW / 'baseline.json').read_text())
    shader_dir = args.binary.parent / 'shaders'
    assert digest(args.binary.parent / 'sdf') == baseline['binaries']['sdf']
    assert all(digest(shader_dir / n) == h for n, h in baseline['original_shaders'].items())
    paths = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', 'projects/sdf'], cwd=ROOT, text=True).splitlines()
    return dict(source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                source_status=subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True),
                source_files={p: digest(ROOT / p) for p in paths if (ROOT / p).is_file()},
                binary_sha256=digest(args.binary), harness_sha256=digest(Path(__file__)),
                shaders={p.name: digest(p) for p in sorted(shader_dir.glob('*stroke*.spv'))},
                original_public_executable_unchanged=True, original_shaders_unchanged=len(baseline['original_shaders']))


def service(args):
    receipt = dict(identity(args), runs=[], complete=False)
    cases = [('guardian', 'cut')] if args.pilot else [(s, op) for s in ('guardian', 'robot') for op in ('add', 'cut')]
    for repeat in range(1 if args.pilot else 3):
        for subject, operation in cases:
            for budget in ([4] if args.pilot else ([2, 4] if repeat % 2 == 0 else [4, 2])):
                name = f'{subject}-{operation}-{budget}-{repeat}'
                data = json.loads((OLD / f'{subject}-{operation}-4.json').read_text())
                data['service'] = dict(budget_ms=budget, groups=512, frames=60)
                inp, out = args.output / (name + '-input.json'), args.output / (name + '.json')
                save(inp, data)
                result = local.run(edit.native(args, inp, out, 'service',
                    ['--edit-commit-builder', 'local-dom'], subject, size=(1920, 1080)), out)
                native = result['native']
                assert native['complete']
                if native['drained']:
                    assert native['canonical_cache_equal']
                values = [f['scheduled_to_completion_ms'] for f in native['frames']]
                receipt['runs'].append(dict(subject=subject, operation=operation, budget_ms=budget,
                    repeat=repeat, stats=dict(median_ms=statistics.median(values), max_ms=max(values),
                    over_16_67=sum(v > 16.67 for v in values), over_11_11=sum(v > 11.11 for v in values)), **result))
                save(args.output / 'service.json', receipt)
                print(name, 'published', native['published_frame'], 'pass', native['service_passed'], flush=True)
    receipt['complete'] = True
    save(args.output / 'service.json', receipt)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase', choices=['service'])
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--binary', type=Path, default=ROOT / 'build/release/projects/sdf/cubey_project_sdf_cached_stroke_study')
    p.add_argument('--pilot', action='store_true')
    args = p.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    globals()[args.phase](args)


if __name__ == '__main__':
    main()
