#!/usr/bin/env python3
"""Package native environment comparisons and scoped qualification evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    working = root / 'outputs/sdf'
    read = lambda path: json.loads(path.read_text())
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    names = dict(comparison='environment-comparison', foundation='environment-foundation',
                 controls='environment-native-controls', profile='environment-profile',
                 selected_glass='environment-final-glass', glass='environment-glass-budget')
    paths = {name: working / path for name, path in names.items()}
    receipts = {name: read(path / ('windowed-interaction.json' if name == 'controls' else 'manifest.json'))
                for name, path in paths.items()}
    captures = receipts['comparison']
    for name in ('comparison', 'foundation', 'controls', 'selected_glass'):
        assert receipts[name]['passed'], name
    assert receipts['profile']['owned_gpu_jobs_excluded']
    assert receipts['controls']['environment_controls']
    for name in ('foundation', 'controls', 'profile', 'selected_glass'):
        for key in ('binary_sha256', 'shader_sha256'):
            assert receipts[name][key] == captures[key], (name, key)
    assert receipts['selected_glass']['budgets']['8']['worst_cap_fraction'] <= .01
    assert len(captures['cases']) == 24 and len(receipts['profile']['runs']) == 24
    baseline = read(working / 'environment-baseline/manifest.json')
    for path, digest in baseline['catalog_sha256'].items():
        assert sha(root / path) == digest, path
    for key, case in captures['cases'].items():
        assert sha(paths['comparison'] / case['scene']) == case['scene_sha256']
        assert receipts['profile']['runs'][key]['scene_sha256'] == case['scene_sha256']
        assert receipts['profile']['runs'][key]['gpu_frame']['samples'] == 148
        for view in case['views'].values():
            assert sha(paths['comparison'] / view['capture']) == view['sha256']
        if 'orbit' in case:
            assert sha(paths['comparison'] / case['orbit']['capture']) == case['orbit']['sha256']
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    for name in ('comparison', 'foundation', 'controls', 'profile'):
        shutil.copytree(paths[name], out / name)
    # The full numeric sweeps are retained; only each budget's worst mask is
    # needed in the browser bundle. Other sweep images remain in working data.
    masks = {}
    for name in ('glass', 'selected_glass'):
        destination = out / name
        destination.mkdir()
        shutil.copy2(paths[name] / 'manifest.json', destination / 'manifest.json')
        for budget in receipts[name]['budgets']:
            key, row = max(((key, row) for key, row in receipts[name]['runs'].items()
                            if key.startswith(f'guardian-{budget}-angle-')),
                           key=lambda pair: pair[1]['cap_fraction'])
            for suffix in ('.png', '.json', '.log'):
                shutil.copy2(paths[name] / (key + suffix), destination / (key + suffix))
            masks[f'{name}-{budget}'] = f'{name}/{key}.png'
    shutil.copytree(paths['glass'] / 'profiles', out / 'glass/profiles')
    shutil.copytree(working / 'environment-final-extent', out / 'extent-comparison')
    shutil.copy2(working / 'environment-baseline/manifest.json', out / 'baseline.json')
    source_paths = [*sorted((root / 'projects/sdf').glob('*.cpp')),
                    *sorted((root / 'projects/sdf').glob('*.h')),
                    *sorted((root / 'projects/sdf/shaders').glob('*')),
                    *sorted((root / 'projects/sdf/tools').glob('*.py')),
                    *sorted((root / 'projects/sdf/tools').glob('*report_template.html')),
                    root / 'projects/sdf/CMakeLists.txt',
                    root / 'include/cubey/render/atmosphere_reflection_probe.h',
                    root / 'include/cubey/engine/atmosphere_environment_runtime.h',
                    root / 'src/cubey/render/atmosphere_reflection_probe.cpp',
                    root / 'src/cubey/engine/atmosphere_environment_runtime.cpp',
                    root / 'docs/notes/sdf-environment-presentation-plan.md',
                    root / 'docs/notes/sdf-environment-presentation-evidence.md']
    source_hashes = {}
    for path in source_paths:
        if not path.is_file():
            continue
        relative = path.relative_to(root)
        destination = out / 'source' / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
        source_hashes[str(relative)] = sha(path)
    data = dict(comparison=captures, foundation=receipts['foundation'],
                controls=receipts['controls'], profile=receipts['profile'],
                glass=receipts['glass'], selected_glass=receipts['selected_glass'], masks=masks,
                old_glass_profile=read(paths['glass'] / 'profiles/manifest.json'),
                source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                source_sha256=source_hashes, browser_review='unavailable; no connected browser')
    (out / 'review.json').write_text(json.dumps(data, indent=2) + '\n')
    template = Path(__file__).with_name('environment_report_template.html').read_text()
    (out / 'index.html').write_text(template.replace('__DATA__', json.dumps(data).replace('</', '<\\/')))
    files = {str(path.relative_to(out)): dict(bytes=path.stat().st_size, sha256=sha(path))
             for path in sorted(out.rglob('*')) if path.is_file()}
    (out / 'assets.json').write_text(json.dumps(files, indent=2) + '\n')
    size = sum(path.stat().st_size for path in out.rglob('*') if path.is_file())
    assert size < 100 * 1024 * 1024, size
    print(json.dumps(dict(output=str(out), bytes=size, files=len(files) + 1)))


if __name__ == '__main__':
    main()
