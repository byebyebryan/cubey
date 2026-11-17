# /// script
# dependencies = ["numpy==2.4.6"]
# ///
"""Opt-in solid-dragon checks; requires the explicitly prepared, certified GLB."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

import numpy as np
from dragon_glass_acceptance import require_requested_memory, require_transport_status


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, default=Path('build/dev/projects/raytracing/raytracing'))
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--sweep-only', action='store_true')
    p.add_argument('--samples', type=int, default=256)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    preset = Path('projects/raytracing/presets/optics/dragon-glass.json')
    model = Path(json.loads(preset.read_text())['model'])
    certificate = json.loads(Path('projects/raytracing/assets/stanford-dragon-optical.json').read_text())
    assert digest(model) == certificate['glb_sha256']
    source = {str(p): digest(p) for p in Path('assets/materials').glob('*.json')}
    calls = []

    def run(name, extra=(), *, mode='hdr', width=160, height=120, samples=None,
            allow_truncation=False):
        samples = a.samples if samples is None else samples
        prefix = a.output / name
        cmd = [str(a.binary.resolve()), '--config', str(preset), '--headless', '--frames', '1',
               '--require-validation', '--width', str(width), '--height', str(height),
               '--samples', str(samples), '--output', str(prefix.with_suffix('.png')),
               '--metrics', str(prefix.with_suffix('.json')), '--'+mode+'-output',
               str(prefix.with_suffix('.bin')), *extra]
        result = subprocess.run(cmd, text=True, capture_output=True,
                                env=dict(os.environ, VK_LAYER_VALIDATE_SYNC='1'), timeout=240)
        log = result.stdout + result.stderr
        prefix.with_suffix('.log').write_text(log)
        assert result.returncode == 0, (name, log)
        assert not any(t in log.lower() for t in ['vuid-', 'hazard detected', 'vulkan validation error']), log
        m = json.loads(prefix.with_suffix('.json').read_text())
        assert m['history_samples'] == samples and m['history_epochs'] == 1
        assert m['as_builds'] == 3 and m['target_generations'] == 1
        assert m['active_triangles'] == 870558 and m['active_route'] == 'dragon'
        assert m['geometry_admission'][0]['optical_volume_qualified']
        assert m['config']['throughput-cutoff'] == 0
        assert m['edge_adjacency_bytes'] == m['active_triangles'] * 16
        require_requested_memory(m)
        data = np.fromfile(prefix.with_suffix('.bin'), dtype='<f4')
        data = data.reshape(-1, 16, 4) if mode == 'optics' else data.reshape(-1, 4)
        assert np.isfinite(data).all(), name
        hdr = data[:, 10, :3] if mode == 'optics' else data[:, :3]
        assert (hdr >= 0).all(), name
        flags = (data[:, 11, 3] if mode == 'optics' else data[:, 3]).astype(np.uint32)
        counts = {str(b): int(np.count_nonzero(flags & b)) for b in [1, 2, 4, 8, 16, 32, 64]}
        require_transport_status(name, counts, allow_truncation=allow_truncation)
        calls.append(dict(name=name, command=cmd, validation_clean=True, finite_hdr=True,
                          status_or_pixels=counts, binary_sha256=digest(a.binary),
                          transport_policy='shallow diagnostic' if allow_truncation else 'accepted view',
                          budget={key: m['config'][key] for key in
                                  ['max-events', 'max-path-rays', 'max-total-rays']},
                          firefly_clamp=m['config']['firefly-clamp'],
                          edge_adjacency_bytes=m['edge_adjacency_bytes'],
                          resident_requested_bytes=m['resident_project_requested_bytes'],
                          peak_requested_bytes=m['peak_simultaneous_project_requested_bytes']))
        print(name, counts, flush=True)
        return data, m

    # Check the actual preset before any reference-budget overrides. A broken
    # preset must fail even when all deeper reference controls would pass.
    default, default_metrics = run('preset-default', mode='optics')
    assert default_metrics['config']['firefly-clamp'] == 100
    depths = [24, 64, 128, 256]
    budget = lambda d: ['--max-events', str(d), '--max-path-rays', str(min(2048, d*8)),
                        '--max-total-rays', str(min(8192, d*32))]
    sweep = {d: run('depth-'+str(d), budget(d), mode='optics',
                    allow_truncation=d < 128)[0] for d in depths}
    mask = (sweep[256][:, 0, 0] >= 0) & (sweep[256][:, 0, 1] == 0)
    ref = sweep[256][:, 10, :3]
    differences = {str(d): dict(
        subject_hdr_rmse=float(np.sqrt(np.mean((sweep[d][mask, 10, :3]-ref[mask])**2))),
        subject_mean_discarded_weight=float(sweep[d][mask, 11, :3].mean()/a.samples),
        mean_queries=float(sweep[d][:, 10, 3].mean()/a.samples),
        maximum_depth=float(sweep[d][:, 15, 1].max())) for d in depths}
    default_error = float(np.sqrt(np.mean((default[mask, 10, :3]-ref[mask])**2)))
    assert default_error <= 1e-5, ('preset differs from deeper reference', default_error)
    receipt = dict(model_sha256=digest(model), samples=a.samples, subject_pixels=int(mask.sum()),
                   budget_comparison=differences,
                   preset_budget={key: default_metrics['config'][key] for key in
                                  ['max-events', 'max-path-rays', 'max-total-rays']},
                   preset_subject_hdr_rmse_to_deep=default_error,
                   status_scope='OR over all completed primary samples per pixel; not last sample', calls=calls)
    (a.output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    if a.sweep_only:
        print(json.dumps(differences, indent=2))
        return

    # These controls keep geometry, camera, atmosphere and marble identical.
    recipe = json.loads(Path('assets/materials/glass.json').read_text())
    recipe['surface']['optics']['attenuation_color'] = [1, 1, 1]
    neutral = a.output/'neutral-glass.json'
    neutral.write_text(json.dumps(recipe, indent=2)+'\n')
    recipe['surface']['optics']['ior'] = 1
    matched = a.output/'index-matched-glass.json'
    matched.write_text(json.dumps(recipe, indent=2)+'\n')
    controls = {}
    for name, extra in [('hidden', ['--hide-dragon']),
                        ('index-matched', ['--recipe-input', str(matched)]),
                        ('neutral', ['--recipe-input', str(neutral)]),
                        ('tinted', []), ('bronze', ['--material', 'bronze'])]:
        controls[name], _ = run(name, extra)
    matched_error = float(np.sqrt(np.mean((controls['hidden'][:, :3]-controls['index-matched'][:, :3])**2)))
    bending = float(np.sqrt(np.mean((controls['neutral'][mask, :3]-controls['index-matched'][mask, :3])**2)))
    tint = float(np.sqrt(np.mean((controls['tinted'][mask, :3]-controls['neutral'][mask, :3])**2)))
    assert matched_error < .02 and bending > matched_error*3 and tint > .005, (matched_error, bending, tint)
    for name, extra in [('grazing', ['--yaw', '1.35', '--pitch', '.1']),
                        ('thin-features', ['--yaw', '-.55', '--pitch', '.5', '--distance', '1.3']),
                        ('marble-view', ['--pitch', '.65'])]:
        run(name, extra)

    marble, _ = run('marble-subject', ['--material', 'marble'], mode='optics', samples=16)
    edited, _ = run('marble-edited', ['--material', 'marble', '--material-roughness', '.1'], mode='optics', samples=16)
    base = (marble[:, 0, 0] >= 0) & (marble[:, 0, 1] == 1) & (edited[:, 0, 1] == 1)
    assert base.sum() > 10 and np.array_equal(marble[base, 13], edited[base, 13])

    saved = a.output/'saved-view.json'
    before, _ = run('save-effective', ['--recipe-input', str(neutral), '--save-config', str(saved)], samples=32)
    # A saved config owns its effective recipe and native scene settings.
    saved_cmd = [str(a.binary.resolve()), '--config', str(saved), '--headless', '--frames', '1',
                 '--hdr-output', str(a.output/'saved-reloaded.bin')]
    result = subprocess.run(saved_cmd, capture_output=True, text=True, timeout=180)
    assert result.returncode == 0, result.stdout+result.stderr
    assert not any(t in (result.stdout+result.stderr).lower() for t in
                   ['vuid-', 'hazard detected', 'vulkan validation error'])
    after = np.fromfile(a.output/'saved-reloaded.bin', dtype='<f4').reshape(-1, 4)
    flags = after[:, 3].astype(np.uint32)
    require_transport_status('saved-reloaded', {str(b): int(np.count_nonzero(flags & b))
                                              for b in [1, 2, 4, 8, 16, 32, 64]})
    assert np.array_equal(before, after), 'effective recipe/view failed exact roundtrip'

    rejected = []
    raw = model.read_bytes()
    tampered_metadata = a.output/'tampered-metadata.glb'
    assert b'main-shell v1' in raw
    tampered_metadata.write_bytes(raw.replace(b'main-shell v1', b'main-shell v2', 1))
    changed = bytearray(raw)
    json_size = struct.unpack_from('<I', raw, 12)[0]
    doc = json.loads(raw[20:20+json_size])
    accessor = doc['accessors'][doc['meshes'][0]['primitives'][0]['attributes']['POSITION']]
    view = doc['bufferViews'][accessor['bufferView']]
    offset = 28+json_size+view.get('byteOffset',0)+accessor.get('byteOffset',0)
    struct.pack_into('<f', changed, offset, struct.unpack_from('<f', raw, offset)[0]+1e-6)
    tampered_position = a.output/'tampered-position.glb'
    tampered_position.write_bytes(changed)
    for extra in [['--model', 'cache/raytracing/stanford-dragon/dragon.glb'],
                  ['--model', str(tampered_metadata)], ['--model', str(tampered_position)],
                  ['--material', 'frosted-glass'], ['--reflection-source', 'environment'],
                  ['--distance', '.1'], ['--dragon-support', 'none']]:
        cmd = [str(a.binary.resolve()), '--config', str(preset), '--headless', *extra]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        assert result.returncode != 0, extra
        rejected.append(dict(arguments=extra, message=(result.stdout+result.stderr).strip()))
    assert source == {str(p): digest(p) for p in Path('assets/materials').glob('*.json')}
    receipt.update(passed=True, index_matched_hdr_rmse=matched_error,
                   bending_subject_hdr_rmse=bending, tint_subject_hdr_rmse=tint,
                   base_recipe_pixels_checked=int(base.sum()), rejected=rejected,
                   saved_view_recipe_exact_roundtrip=True, source_material_files_unchanged=True)
    (a.output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print('Solid dragon qualification passed:', a.output)


if __name__ == '__main__':
    main()
