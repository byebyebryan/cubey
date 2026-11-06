# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.4"]
# ///
"""Score fixed-pose, isolated sphere HDR captures without background dilution.

The analytic projected silhouette supplies a declared one-pixel interior guard.
This tool does not infer success from display PNGs or change the fixed limits.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np


def compare(candidate, reference, width, height, radius, distance, fov):
    a = np.fromfile(candidate, dtype='<f4').reshape(height, width, 4).astype(float)
    b = np.fromfile(reference, dtype='<f4').reshape(height, width, 4).astype(float)
    assert np.all(np.isfinite(a)) and np.all(np.isfinite(b)), 'nonfinite HDR observations'
    assert 0 < radius < distance
    projected = radius/np.sqrt(distance*distance-radius*radius)/np.tan(np.deg2rad(fov/2))*height/2
    y, x = np.mgrid[:height, :width]
    distance_pixels = np.sqrt((x+.5-width/2)**2+(y+.5-height/2)**2)
    silhouette = distance_pixels <= projected
    roi = distance_pixels <= projected-1
    assert np.count_nonzero(roi) > 0
    normalization = np.maximum(1, b[..., :3]@np.array([.2126,.7152,.0722]))
    difference = np.abs(a[..., :3]-b[..., :3])/normalization[..., None]
    maximum_channel = np.max(difference, axis=-1)
    mean = float(np.mean(difference[roi]))
    p95 = float(np.percentile(maximum_channel[roi], 95))
    statuses, counts = np.unique(a[..., 3], return_counts=True)
    return dict(passed=mean <= .005 and p95 <= .02,
                mean_limit=.005, p95_limit=.02, roi_mean=mean, roi_p95=p95,
                roi_pixels=int(np.count_nonzero(roi)),
                guarded_pixels=int(np.count_nonzero(silhouette & ~roi)),
                guard_policy='one pixel inside the analytic primary sphere silhouette',
                projected_radius_pixels=float(projected),
                all_pixel_mean=float(np.mean(difference)),
                all_pixel_p95=float(np.percentile(maximum_channel,95)),
                maximum_error=float(np.max(difference)),
                candidate_status_counts={str(int(s)):int(n) for s,n in zip(statuses,counts)},
                candidate_sha256=hashlib.sha256(candidate.read_bytes()).hexdigest(),
                reference_sha256=hashlib.sha256(reference.read_bytes()).hexdigest())


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--candidate', type=Path, required=True)
    p.add_argument('--reference', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--width', type=int, default=640)
    p.add_argument('--height', type=int, default=480)
    p.add_argument('--radius', type=float, default=.25)
    p.add_argument('--distance', type=float, default=1.05)
    p.add_argument('--fov', type=float, default=45)
    args = p.parse_args()
    result = compare(args.candidate,args.reference,args.width,args.height,args.radius,args.distance,args.fov)
    result.update(candidate=str(args.candidate),reference=str(args.reference))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    raise SystemExit(0 if result['passed'] else 1)
