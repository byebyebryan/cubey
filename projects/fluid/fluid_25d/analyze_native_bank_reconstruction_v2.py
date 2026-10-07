#!/usr/bin/env python3
"""Read-only measurements of the V2 display experiment, not hydrology."""
from __future__ import annotations

import argparse
from array import array
import json
import math
from pathlib import Path
import subprocess
import sys

import run_native_bank_reconstruction_v2 as study
import run_native_presentation_v1 as reference


def floats(path: Path) -> array:
    values = array("f")
    values.frombytes(path.read_bytes())
    if sys.byteorder != "little":
        values.byteswap()
    return values


def bed_approximation() -> dict:
    path = reference.ROOT / "outputs/fluid/native-rain-recession-v1-20261003-1ZMqTJ/recordings/rain-off/bed.f32"
    bed = floats(path)
    n = 512
    if len(bed) != n*n:
        raise ValueError("unexpected native bed size")
    # At a native centre, the cubic basis reduces to 1/6,2/3,1/6.
    weights = (1/6,2/3,1/6)
    departures = []
    for y in range(1,n-1):
        for x in range(1,n-1):
            shown = sum(bed[(y+j-1)*n+x+i-1]*weights[i]*weights[j]
                        for j in range(3) for i in range(3))
            departures.append(abs(shown-bed[y*n+x]))
    ordered = sorted(departures)
    return {"scope": "physical metres of render-only bed approximation at interior native centres; not input mutation",
            "bed_sha256": reference.sha256_file(path), "sample_count": len(ordered),
            "p50_m": ordered[len(ordered)//2], "p95_m": ordered[int(len(ordered)*.95)],
            "max_m": ordered[-1], "rms_m": math.sqrt(sum(v*v for v in ordered)/len(ordered))}


def rgb(path: Path) -> bytes:
    result = subprocess.run(["rtk","proxy","ffmpeg","-v","error","-i",str(path),
                             "-f","rawvideo","-pix_fmt","rgb24","pipe:1"],
                            capture_output=True, check=True, timeout=60)
    if len(result.stdout) != 1920*1080*3:
        raise ValueError("unexpected diagnostic image dimensions")
    return result.stdout


def high_depth_band(r: int, g: int, b: int) -> bool:
    # Linear diagnostic colors are sRGB-encoded at capture: deep blue becomes
    # (89,124,255), cyan (0,231,231). Test against the captured palette, not the
    # shader's unencoded red=0.1. Gray/magenta/yellow/background stay excluded.
    return r < 100 and b > 200


def image_metrics(root: Path) -> list[dict]:
    rows = []
    for sampling, subdivision in study.MODES:
        name = study.mode_name(sampling,subdivision)
        media = root / name / "media"
        solid = rgb(media/"rain-off-collection-14400s-solid.png")
        no_occlusion = rgb(media/"rain-off-collection-14400s-no-occlusion.png")
        contour_path = media/"rain-off-collection-14400s-contours.png"
        contour = rgb(contour_path)
        changed = over8 = maximum = 0
        for i in range(0,len(solid),3):
            difference = max(abs(solid[i+c]-no_occlusion[i+c]) for c in range(3))
            changed += difference > 0
            over8 += difference > 8
            maximum = max(maximum,difference)
        # The diagnostic's opaque cyan and blue bands are >=26 mm. Red,
        # magenta/yellow, gray rain film and brown terrain are excluded.
        band_pixels = sum(high_depth_band(r,g,b) for r,g,b in zip(contour[0::3],contour[1::3],contour[2::3]))
        rows.append({"sampling": sampling, "subdivision": subdivision, "pixel_count":1920*1080,
                     "solid_vs_no_occlusion_different_pixels":changed,
                     "solid_vs_no_occlusion_pixels_difference_over_8":over8,
                     "solid_vs_no_occlusion_max_channel_difference":maximum,
                     "depth_band_at_least_26mm_screen_pixels":band_pixels,
                     "contour_sha256":reference.sha256_file(contour_path)})
    baseline = rows[0]["depth_band_at_least_26mm_screen_pixels"]
    for row in rows:
        row["depth_band_screen_pixel_delta_vs_triangular"] = row["depth_band_at_least_26mm_screen_pixels"]-baseline
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,required=True)
    args = parser.parse_args()
    root = args.out.resolve()
    if args.out.is_symlink() or root.parent != reference.OUTPUT_ROOT.resolve() or not root.name.startswith("bank-reconstruction-v2-"):
        raise ValueError("output must be a bank-reconstruction-v2-* evidence leaf")
    before = study.bank.identity()
    measurement = {"schema":"cubey.fluid25d.bank_reconstruction_measurements.v2",
                   "runtime_identity":before, "bed_approximation":bed_approximation(),
                   "image_metrics":image_metrics(root),
                   "image_metric_scope":"screen pixels after projection, rasterization and depth test; not physical area/volume or a topology oracle"}
    reference.assert_same_runtime(before,study.bank.identity(),"display measurement")
    reference.write_json_exclusive(root/"measurements.json",measurement)
    print(json.dumps({k:v for k,v in measurement.items() if k!="runtime_identity"},indent=2))


if __name__ == "__main__":
    main()
