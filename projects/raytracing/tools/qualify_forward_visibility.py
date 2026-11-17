# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.5", "pillow==13.0.0"]
# ///
"""Check that forward glass actually transmits the existing marble scene object.

Toggle only the slab pattern, excluding dragon silhouette pixels. This measures
scene visibility, not optical accuracy or parity with ray tracing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path,
                        default=Path("build/dev/projects/raytracing/raytracing"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    binary_hash = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    width, height = 640, 360
    calls = []

    def capture(name, extra=()):
        prefix = args.output / name
        command = [str(args.binary), "--config",
                   "projects/raytracing/presets/optics/dragon-forward.json",
                   "--headless", "--require-validation", "--width", str(width),
                   "--height", str(height), "--target-y", ".25", "--distance", "1.25",
                   "--target-x", ".1", "--output", str(prefix.with_suffix(".png")),
                   "--metrics", str(prefix.with_suffix(".json")), *map(str, extra)]
        result = subprocess.run(command, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        prefix.with_suffix(".log").write_text(result.stdout)
        assert result.returncode == 0, result.stdout
        assert not any(s in result.stdout.lower() for s in
                       ("validation error", "vuid-", "hazard detected")), result.stdout
        metrics = json.loads(prefix.with_suffix(".json").read_text())
        assert not metrics["ray_tracing_enabled"] and metrics["as_bytes"] == 0
        calls.append({"name": name, "command": command, "validation_clean": True})
        print(name, flush=True)
        return np.asarray(Image.open(prefix.with_suffix(".png")).convert("RGB")).astype(int)

    mask_path = args.output / "mask.bin"
    capture("mask", ["--view", "transmission-ray", "--hdr-output", mask_path])
    raw = np.fromfile(mask_path, dtype="<f4").reshape(height, width, 4)
    assert np.isfinite(raw).all()
    mask = (raw[:, :, 3] > 0) & (raw[:, :, 3] < .201)
    # Erode two pixels to keep raster coverage and opaque slab edges out.
    padded = np.pad(mask, 2, constant_values=False)
    interior = np.logical_and.reduce([padded[y:y+height, x:x+width]
                                     for y in range(5) for x in range(5)])
    assert interior.sum() > 30000
    results = {}
    source_images = {}
    for source in ("environment", "screen", "hybrid"):
        extra = ["--forward-transmission-source", source]
        patterned = capture(source + "-pattern", extra)
        source_images[source] = patterned
        plain = capture(source + "-plain", [*extra, "--no-pattern"])
        delta = np.abs(patterned - plain)
        changed = (delta.max(axis=-1) > 0) & interior
        count = int(changed.sum())
        results[source] = {"changed_glass_interior_pixels": count,
                           "changed_fraction": float(count / interior.sum()),
                           "mean_rgb_code_change": float(delta[interior].mean()),
                           "max_rgb_code_change": int(delta[interior].max())}
        if source == "environment":
            assert count == 0, "environment-only should not contain local marble"
        else:
            assert count > 5000, (source, results[source])

    # At this fixed detail camera, the upper glass is backed only by sky;
    # the marble and its projected refraction source are below this crop.
    # Check both halves of hybrid transport so accidentally routing it to
    # screen-only or environment-only cannot pass the visibility controls.
    sky_interior = interior.copy()
    sky_interior[140:] = False
    assert sky_interior.sum() > 10000
    hybrid_sky = source_images["hybrid"][sky_interior]
    environment_error = np.abs(hybrid_sky - source_images["environment"][sky_interior])
    screen_difference = np.abs(hybrid_sky - source_images["screen"][sky_interior])
    sky_refraction = {"glass_pixels": int(sky_interior.sum()),
                      "environment_mean_rgb_code_error": float(environment_error.mean()),
                      "environment_max_rgb_code_error": int(environment_error.max()),
                      "screen_mean_rgb_code_difference": float(screen_difference.mean())}
    assert environment_error.max() <= 1, ("hybrid lost outgoing sky refraction", sky_refraction)
    assert screen_difference.mean() > 5, ("hybrid became screen-only", sky_refraction)
    assert hashlib.sha256(args.binary.read_bytes()).hexdigest() == binary_hash
    receipt = {"passed": True, "binary_sha256": binary_hash,
               "body_interior_pixels": int(interior.sum()), "sources": results,
               "sky_refraction": sky_refraction,
               "calls": calls,
               "scope": "Marble visibility and outgoing sky retained; RGB only; no optical accuracy claim"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(results, indent=2), flush=True)


if __name__ == "__main__":
    main()
