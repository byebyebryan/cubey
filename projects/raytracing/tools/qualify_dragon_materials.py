# /// script
# dependencies = ["numpy==2.5.4"]
# ///
"""Opt-in sampled dragon checks. Requires the locally prepared GLB; never downloads it."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

import numpy as np


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('build/dev/projects/raytracing/raytracing'))
    parser.add_argument('--model', type=Path, default=Path('cache/raytracing/stanford-dragon/dragon.glb'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--geometry-reference', type=Path)
    args = parser.parse_args()
    assert args.model.is_file(), 'Prepare the pinned dragon explicitly before running this check'
    args.output.mkdir(parents=True, exist_ok=False)
    source = {str(p): digest(p) for p in Path('assets/materials').glob('*.json')}
    model_hash = digest(args.model)
    calls = []

    def run(name, extra=(), *, samples=128, width=320, height=240, optics=False,
            config=Path('projects/raytracing/presets/optics/dragon-sampled.json')):
        prefix = args.output / name
        command = [str(args.binary.resolve()), '--config', str(config),
                   '--model', str(args.model), '--headless', '--frames', '1', '--require-validation',
                   '--width', str(width), '--height', str(height), '--samples', str(samples),
                   '--output', str(prefix.with_suffix('.png')), '--metrics', str(prefix.with_suffix('.json')),
                   '--optics-output' if optics else '--hdr-output', str(prefix.with_suffix('.bin')), *extra]
        result = subprocess.run(command, text=True, capture_output=True,
                                env=dict(os.environ, VK_LAYER_VALIDATE_SYNC='1'), timeout=180)
        log = result.stdout + result.stderr
        prefix.with_suffix('.log').write_text(log)
        assert result.returncode == 0, log
        assert not any(token in log.lower() for token in ['vuid-', 'hazard detected', 'vulkan validation error']), log
        metrics = json.loads(prefix.with_suffix('.json').read_text())
        assert metrics['history_samples'] == samples and metrics['history_epochs'] == 1
        assert metrics['as_builds'] == 3 and metrics['target_generations'] == 1
        assert metrics['active_triangles'] == 871458 and metrics['active_route'] == 'dragon'
        assert not any(m['optical_volume_qualified'] for m in metrics['geometry_admission'])
        data = np.fromfile(prefix.with_suffix('.bin'), dtype='<f4').reshape(-1, 16, 4) if optics else np.fromfile(prefix.with_suffix('.bin'), dtype='<f4').reshape(-1, 4)
        assert np.isfinite(data).all(), name
        if not optics:
            assert (data[:, :3] >= 0).all(), name
        calls.append(dict(name=name, command=command, samples=samples, validation_clean=True))
        print(name, 'passed', flush=True)
        return data, metrics

    frames = {}
    for spp in [1, 8, 32, 128, 512, 2048]:
        frames[spp], metrics = run(f'bronze-{spp}', samples=spp)
    reference = frames[2048][:, :3]
    mask_records, _ = run('geometry-mask', samples=1, optics=True)
    foreground = mask_records[:, 0, 0] >= 0
    optics, _ = run('optics-default', samples=512, width=128, height=96, optics=True)
    deep, _ = run('optics-deep', ['--max-events', '12', '--max-path-rays', '64', '--max-total-rays', '256'],
                  samples=512, width=128, height=96, optics=True)
    # Convergence scores include the whole HDR image; masked local evidence follows below.
    errors = {str(spp): float(np.sqrt(np.mean((frame[:, :3] - reference) ** 2))) for spp, frame in frames.items()}
    foreground_errors = {str(spp): float(np.sqrt(np.mean((frame[foreground, :3] - reference[foreground]) ** 2))) for spp, frame in frames.items()}
    assert errors['128'] < errors['8'] and errors['512'] < errors['32'], errors
    assert foreground_errors['128'] < foreground_errors['8'] and foreground_errors['512'] < foreground_errors['32'], foreground_errors
    assert not np.any(optics[:, 11, 3].astype(np.uint32) & (16 | 64)), 'invalid medium or path stack'
    samples_observed = optics[:, 15, 2]
    assert np.all(samples_observed == 512)
    flags = optics[:, 11, 3].astype(np.uint32)
    counts = {str(bit): int(np.count_nonzero(flags & bit)) for bit in [1, 2, 4, 8, 16, 32, 64]}
    deep_flags = deep[:, 11, 3].astype(np.uint32)
    deep_counts = {str(bit): int(np.count_nonzero(deep_flags & bit)) for bit in [1, 2, 4, 8, 16, 32, 64]}
    primary = optics[:, 0, 1].astype(np.int32)
    valid = optics[:, 0, 0] >= 0
    reflected = optics[:, 8, 3].astype(np.int32)
    hits = {str(i): int(np.count_nonzero(valid & (primary == 0) & (reflected == i))) for i in [-3, -1, 0, 1]}
    assert hits['0'] > 0 and hits['1'] > 0, hits
    budget_difference = np.abs(optics[:, 10, :3] - deep[:, 10, :3])

    for material in metrics['workshop_supported_material_ids']:
        run('catalog-' + material, ['--material', material], samples=8, width=128, height=96)

    # Changing a marble subject must leave the *evaluated primary base recipe*
    # alone, even though secondary illumination of that base may change.
    marble, _ = run('marble-subject', ['--material', 'marble'], samples=32, width=128, height=96, optics=True)
    edited, _ = run('marble-subject-edited', ['--material', 'marble', '--material-roughness', '.1'],
                     samples=32, width=128, height=96, optics=True)
    base = (marble[:, 0, 0] >= 0) & (marble[:, 0, 1] == 1) & (edited[:, 0, 1] == 1)
    assert base.sum() > 50 and np.array_equal(marble[base, 13], edited[base, 13]), 'subject edit changed base recipe'
    subject = (marble[:, 0, 0] >= 0) & (marble[:, 0, 1] == 0)
    assert np.mean(np.abs(marble[subject, 13, 3] - edited[subject, 13, 3])) > .2

    saved = args.output / 'saved-view.json'
    original, _ = run('save-effective', ['--material-roughness', '.17', '--save-config', str(saved)], samples=32)
    saved_config = json.loads(saved.read_text())
    assert saved_config['scene'] == 'dragon-materials' and saved_config['material-roughness'] == -1
    assert abs(json.loads(Path(saved_config['recipe-input']).read_text())['surface']['roughness'] - .17) < 1e-6
    reloaded, _ = run('reload-effective', samples=32, config=saved)
    assert np.array_equal(original, reloaded), 'effective view/recipe did not round-trip exactly'

    rejected = []
    for extra in [['--material', 'glass'], ['--material', 'frosted-glass'], ['--material', 'gallery'],
                  ['--dragon-support', 'none'], ['--support-material', 'matte'],
                  ['--environment', 'legacy'], ['--throughput-cutoff', '.0001']]:
        command = [str(args.binary.resolve()), '--scene', 'dragon-materials', '--model', str(args.model), *extra]
        result = subprocess.run(command, text=True, capture_output=True, timeout=20)
        assert result.returncode != 0, extra
        rejected.append(dict(arguments=extra, message=(result.stdout + result.stderr).strip()))

    geometry = None
    if args.geometry_reference:
        path = args.output / 'scene.json'
        run('geometry', ['--scene-output', str(path)], samples=1, width=128, height=96)
        candidate = json.loads(path.read_text())
        old = json.loads(args.geometry_reference.read_text())
        assert candidate['meshes'] == old['meshes'] or all(
            all(new[key] == before[key] for key in ['positions', 'normals', 'indices'])
            for new, before in zip(candidate['meshes'], old['meshes'], strict=True))
        assert len(candidate['meshes']) == len(candidate['instances']) == 2
        assert all(new['world'] == before['world'] for new, before in zip(candidate['instances'], old['instances'], strict=True))
        geometry = dict(reference=str(args.geometry_reference), meshes=2, instances=2,
                        positions_normals_indices_and_worlds_identical=True)

    assert model_hash == digest(args.model)
    assert source == {str(p): digest(p) for p in Path('assets/materials').glob('*.json')}
    receipt = dict(passed=True, binary_sha256=digest(args.binary), asset_sha256=model_hash,
                   device=metrics['device'], hdr_rmse_to_2048_spp=errors,
                   foreground_hdr_rmse_to_2048_spp=foreground_errors, foreground_pixels=int(foreground.sum()),
                   foreground_mask_scope='one jittered primary per pixel; includes edge pixels',
                   transport_flag_pixels=counts, deeper_transport_flag_pixels=deep_counts,
                   first_reflection_last_primary_sample=hits, base_recipe_pixels_checked=int(base.sum()),
                   default_deep_hdr_difference=dict(mean=float(budget_difference.mean()), maximum=float(budget_difference.max())),
                   geometry=geometry, rejected=rejected, calls=calls, source_material_files_unchanged=True)
    (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Sampled opaque dragon qualification passed:', args.output)


if __name__ == '__main__':
    main()
