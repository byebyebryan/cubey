# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.4"]
# ///
"""Inspect bounded native optical residuals and deliberately exhausted budgets."""
import argparse
import json
from pathlib import Path
import numpy as np
from qualify_optics import native
from profile_first_pass import sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('build/dev/projects/raytracing/raytracing'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--quality-packet', type=Path)
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    result = dict(binary_sha256=sha(args.binary), residuals=[], budgets=[], full_resolution_flags=[])
    for view in ['reflection', 'curved-mirror', 'refraction']:
        actual, scene, command = native(args.binary, args.output, view,
            ['--config', f'projects/raytracing/presets/optics/{view}.json', '--width', '480', '--height', '270'], 480, 270)
        assert np.isfinite(actual).all()
        status = actual[:, 11, 3].astype(np.uint32)
        residual = actual[:, 11, :3]
        assert (residual >= 0).all()
        if view == 'refraction':
            assert ((status & 8) != 0).any(), 'glass cutoff was not exercised'
        assert actual[:, 15, 0].max() <= 32 and actual[:, 10, 3].max() <= 128
        assert actual[:, 15, 2].max() <= 16
        result['residuals'].append(dict(view=view, width=480, height=270,
            scope='separate bounded diagnostic at lower resolution; throughput is not radiance',
            status_counts={str(int(s)): int(np.sum(status == s)) for s in np.unique(status)},
            discarded_throughput_mean_rgb=residual.mean(axis=0).tolist(),
            discarded_throughput_max_rgb=residual.max(axis=0).tolist(),
            discarded_pixels=int(np.sum(np.any(residual > 0, axis=1))),
            max_total_calls=int(actual[:, 10, 3].max()), max_stack=int(actual[:, 15, 2].max()), command=command))
    for name, option, value, bit in [('total', '--max-total-rays', '1', 1),
                                    ('events', '--max-events', '1', 2),
                                    ('radiance', '--max-path-rays', '1', 4)]:
        actual, _, command = native(args.binary, args.output, 'budget-' + name,
            ['--scene', 'reflection-fixture', '--target-y', '.55', '--distance', '3', option, value])
        status = actual[:, 11, 3].astype(np.uint32)
        eligible = (actual[:, 1, 3] == 1) & ((status & bit) != 0)
        assert eligible.any(), name
        assert (actual[eligible, 10, :3] == 0).all(), 'terminated mirror path acquired fallback radiance'
        assert (actual[eligible, 11, :3].max(axis=1) > 0).all(), 'missing discarded throughput'
        result['budgets'].append(dict(name=name, passed=True, terminated_mirror_pixels=int(eligible.sum()), command=command))
    if args.quality_packet:
        for view in ['reflection', 'curved-mirror', 'refraction']:
            image = np.fromfile(args.quality_packet/(view + '-default.bin'), dtype='<f4').reshape(1080,1920,4)
            control = np.fromfile(args.quality_packet/(view + '-control.bin'), dtype='<f4').reshape(1080,1920,4)
            rejected = np.argwhere((image[:, :, 3].astype(np.uint32) & 16) != 0)
            result['full_resolution_flags'].append(dict(view=view, medium_state_rejection_pixels_yx=rejected.tolist(),
                scope='observed status locations; cause of individual rejected paths is unclassified',
                max_absolute_rgb_error_at_rejections=float(np.max(np.abs(image[rejected[:,0],rejected[:,1],:3]-control[rejected[:,0],rejected[:,1],:3]))) if len(rejected) else 0))
    result['passed'] = True
    (args.output/'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ['residuals','budgets']}, indent=2))


if __name__ == '__main__':
    main()
