#!/usr/bin/env python3
"""Compare existing shared cloud horizon and sampling choices on fixed SDF inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


VARIANTS = {
    'local': {'cloud-distance-mode': 'local'},
    'auto': {},
    'samples2': {'cloud-view-samples': 2},
    'full': {'cloud-quality': 'full'},
    'bilateral': {'cloud-resolve-mode': 'metadata-bilateral'},
    'reference': {'cloud-quality': 'full', 'cloud-view-samples': 2},
    'backdrop': {'cloud-sky-only': True, 'cloud-view-samples': 2},
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    binary = root / 'build/release/projects/sdf/sdf'
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    receipt = dict(binary_sha256=sha(binary),
                   shader_sha256={p.name: sha(p) for p in sorted((binary.parent / 'shaders').glob('*.spv'))},
                   source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                   variants=VARIANTS, runs={})

    def run(name, config, extra=(), size=960):
        preset = out / (name + '.json')
        preset.write_text(json.dumps(config, indent=2) + '\n')
        image = out / (name + '.png')
        command = [str(binary), '--headless', '--require-validation', '--config', str(preset),
                   '--width', str(size), '--height', str(size), '--output', str(image), *map(str, extra)]
        result = subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / (name + '.log')).write_text(result.stdout)
        assert result.returncode == 0 and 'validation error' not in result.stdout.lower(), result.stdout
        assert result.stdout.count('cloud probe capture generation=') == 1
        assert 'environment finish sky_generation=1 cloud_generation=1' in result.stdout
        receipt['runs'][name] = dict(command=command, capture=image.name, sha256=sha(image),
                                     preset=preset.name, scene_sha256=sha(root / config['authored-scene']))
        (out / 'manifest.json').write_text(json.dumps(receipt, indent=2) + '\n')

    for variant, options in VARIANTS.items():
        for case in ['background', 'sphere-bronze', 'guardian-bronze', 'guardian-glass']:
            source = 'sphere-plain' if case == 'background' else case
            config = json.loads((root / f'projects/sdf/presets/environment-study/{source}-clouds.json').read_text())
            config.update({'cloud-distance-mode': 'auto', 'cloud-quality': 'half',
                           'cloud-resolve-mode': 'terrain-post', 'cloud-view-samples': 1,
                           'cloud-debug-view': 'final', 'cloud-sky-only': False, **options})
            extra = ['--motion', 'construction', '--time', 0] if case == 'background' else []
            run(variant + '-' + case, config, extra)
        print(variant, 'passed', flush=True)
    for variant in ['local', 'auto']:
        for debug in ['horizon-handoff', 'integrated-horizon-alpha', 'cloud-alpha']:
            config = json.loads((out / f'{variant}-background.json').read_text())
            config.update({'cloud-debug-view': debug, 'supersample': 1})
            run(variant + '-' + debug, config, ['--motion', 'construction', '--time', 0], 480)
    baseline = json.loads((root / 'outputs/sdf/environment-baseline/manifest.json').read_text())
    assert all(sha(root / path) == digest for path, digest in baseline['catalog_sha256'].items())
    receipt['accepted_catalog_28_files_exact'] = True
    receipt['passed'] = True
    (out / 'manifest.json').write_text(json.dumps(receipt, indent=2) + '\n')


if __name__ == '__main__':
    main()
