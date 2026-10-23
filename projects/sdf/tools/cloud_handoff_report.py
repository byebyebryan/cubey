#!/usr/bin/env python3
"""Package the bounded SDF cloud integration correction and native A/B evidence."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from qualify import distribution


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    working = root / 'outputs/sdf'
    read = lambda p: json.loads(p.read_text())
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    data = {}
    for name in ['comparison', 'foundation', 'controls', 'films', 'profile', 'admission']:
        path = working / ('cloud-handoff-' + name)
        filename = 'windowed-interaction.json' if name == 'controls' else 'manifest.json'
        data[name] = read(path / filename)
        assert data[name].get('passed', name == 'profile'), name
        if name not in ['admission']:
            for key in ['binary_sha256', 'shader_sha256']:
                assert data[name][key] == data['comparison'][key], (name, key)
        shutil.copytree(path, out / name)
    assert data['profile']['owned_gpu_jobs_excluded']
    assert data['controls']['environment_controls']
    compatibility = read(working / 'cloud-handoff-comparison/compatibility.json')
    assert compatibility['passed']
    data['compatibility'] = compatibility
    for row in data['comparison']['runs'].values():
        assert sha(out / 'comparison' / row['capture']) == row['sha256']
    for row in data['films']['runs'].values():
        assert sha(out / 'films' / row['capture']) == row['sha256']
        assert row['frames'] == 240 and row['field_builds'] == row['probe_captures'] == 1
    for row in data['profile']['runs'].values():
        assert row['gpu_frame']['samples'] == 148 and row['gpu_scope'] == 'whole scene'
    for name, row in data['profile']['runs'].items():
        values = {}
        with (out / 'profile' / (name + '.passes.csv')).open() as stream:
            for sample in csv.DictReader(stream):
                if sample['kind'] == 'gpu':
                    values.setdefault(sample['label'], []).append(float(sample['duration_ms']))
        row['passes'] = {label: distribution(samples) for label, samples in values.items()}
    baseline = read(working / 'environment-baseline/manifest.json')
    assert all(sha(root / path) == digest for path, digest in baseline['catalog_sha256'].items())
    data['source_commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    paths = ['projects/sdf/sdf_environment.cpp', 'projects/sdf/sdf_config.h',
             'projects/sdf/tools/capture_cloud_handoff.py', 'projects/sdf/tools/cloud_handoff_report.py',
             'projects/sdf/tools/cloud_handoff_report_template.html',
             'include/cubey/engine/cloud_environment_runtime.h',
             'include/cubey/render/cloud_layer_config.h', 'src/cubey/engine/cloud_environment_runtime.cpp',
             'src/cubey/render/cloud_layer.cpp', 'src/cubey/render/cloud_environment_probe.cpp',
             'shaders/cubey/atmosphere/sky_background_direction.glsl',
             'shaders/cubey/cloud/cloud_common.glsl', 'shaders/cubey/cloud/cloud_composite_background.frag',
             'shaders/cubey/cloud/cloud_composite_background_depth.frag',
             'tests/cubey/render_cloud_layer_tests.cpp', 'docs/notes/sdf-environment-presentation-evidence.md']
    paths += [f'projects/sdf/scenes/material-study/{name}.json' for name in
              ['sphere-plain', 'sphere-bronze', 'guardian-bronze', 'guardian-glass']]
    data['source_sha256'] = {}
    for name in paths:
        destination = out / 'source' / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / name, destination)
        data['source_sha256'][name] = sha(root / name)
    template = Path(__file__).with_name('cloud_handoff_report_template.html').read_text()
    (out / 'review.json').write_text(json.dumps(data, indent=2) + '\n')
    (out / 'index.html').write_text(template.replace('__DATA__', json.dumps(data).replace('</', '<\\/')))
    assets = {str(p.relative_to(out)): dict(bytes=p.stat().st_size, sha256=sha(p))
              for p in sorted(out.rglob('*')) if p.is_file()}
    (out / 'assets.json').write_text(json.dumps(assets, indent=2) + '\n')
    size = sum(p.stat().st_size for p in out.rglob('*') if p.is_file())
    assert size < 100 * 1024 * 1024, size
    print(json.dumps(dict(output=str(out), files=len(assets) + 1, bytes=size)))


if __name__ == '__main__':
    main()
