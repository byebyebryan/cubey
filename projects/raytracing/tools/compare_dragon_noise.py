# /// script
# dependencies = ["numpy==2.4.6", "pillow==12.0.0"]
# ///
"""Matched raw/clamp/OIDN/combined still-image comparison, with pinned inputs.

OIDN is an explicit external tool, not a renderer dependency. Stages are run
sequentially so GPU denoising does not overlap the rendering measurements.
"""

import argparse
import json
import subprocess
import time
from pathlib import Path

import numpy as np
from PIL import Image

from measure_dragon_glass import PRESET, capture, digest


def write_pfm(path, rgb):
    height, width, _ = rgb.shape
    with path.open("wb") as stream:
        stream.write(f"PF\n{width} {height}\n-1.0\n".encode())
        stream.write(np.ascontiguousarray(rgb[::-1], dtype="<f4").tobytes())


def read_pfm(path):
    with path.open("rb") as stream:
        assert stream.readline().strip() == b"PF"
        width, height = map(int, stream.readline().split())
        scale = float(stream.readline())
        assert abs(scale) == 1
        raw = np.frombuffer(stream.read(), dtype="<f4" if scale < 0 else ">f4")
    return raw.reshape(height, width, 3)[::-1].astype(np.float32)


def display_rgb8(rgb):
    """Mirror rgba16f storage, ACES and explicit sRGB encoding at exposure zero."""
    color = np.maximum(rgb.astype(np.float16).astype(np.float32), 0)
    color = np.clip(
        color * (np.float32(2.51) * color + np.float32(0.03))
        / (color * (np.float32(2.43) * color + np.float32(0.59)) + np.float32(0.14)),
        0,
        1,
    )
    color = np.where(
        color <= np.float32(0.0031308),
        color * np.float32(12.92),
        np.float32(1.055) * np.power(color, np.float32(1 / 2.4)) - np.float32(0.055),
    )
    return np.rint(np.clip(color, 0, 1) * 255).astype(np.uint8)


def identity(binary):
    return {
        "binary": digest(binary),
        "shaders": {
            p.name: digest(p) for p in sorted((binary.parent / "shaders").glob("*.spv"))
        },
        "preset": digest(PRESET),
        "model": digest(json.loads(PRESET.read_text())["model"]),
        "recipes": {
            p.name: digest(p) for p in sorted(Path("assets/materials").glob("*.json"))
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", choices=["capture", "denoise"], required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--binary", type=Path, default=Path("build/dev/projects/raytracing/raytracing")
    )
    parser.add_argument("--oidn", type=Path)
    parser.add_argument("--reference-manifest", type=Path)
    args = parser.parse_args()
    root, binary = args.output.resolve(), args.binary.resolve()
    root.mkdir(parents=True, exist_ok=True)
    pinned = identity(binary)
    pin = root / "identity.json"
    if pin.exists():
        assert json.loads(pin.read_text()) == pinned, "comparison inputs changed"
    else:
        pin.write_text(json.dumps(pinned, indent=2) + "\n")
    manifest_path = root / "captures.json"
    if args.stage == "capture":
        records = []
        for samples in [128, 512]:
            for limit in [0, 100, 1000, 10000]:
                for seed in [19, 43]:
                    assert identity(binary) == pinned
                    name = f"front-{samples}-s{seed}-c{limit}"
                    receipt = capture(
                        binary, root / "captures", name, 1920, 1080, samples, seed,
                        ["--firefly-clamp", str(limit)], diagnostic_continue=True,
                    )
                    metrics = json.loads((root / "captures" / (name + ".json")).read_text())
                    assert metrics["config"]["firefly-clamp"] == limit
                    assert metrics["config"]["smooth-normals"]
                    records.append({"name": name, "samples": samples, "seed": seed,
                                    "clamp": limit, "prefix": str(root / "captures" / name),
                                    "receipt": receipt})
                    manifest_path.write_text(json.dumps({"identity": pinned, "captures": records}, indent=2) + "\n")
        if args.reference_manifest:
            old = json.loads(args.reference_manifest.read_text())
            old_cases = {(c["samples"], c["seed"]): c for c in old["captures"] if c["lighting"] == "combined"}
            fidelity = []
            for c in records:
                if c["clamp"] != 0:
                    continue
                reference = old_cases[c["samples"], c["seed"]]["prefix"]
                a, b = [np.array(Image.open(p + ".png"))[:, :, :3].astype(float) for p in (c["prefix"], reference)]
                delta = np.abs(a - b)
                raw_a, raw_b = [np.fromfile(p + ".bin", dtype="<f4") for p in (c["prefix"], reference)]
                row = dict(name=c["name"], png_mean_absolute_rgb8=float(delta.mean()),
                           png_p99_absolute_rgb8=float(np.quantile(delta, .99)),
                           png_max_absolute_rgb8=float(delta.max()),
                           hdr_bitwise_identical=bool(np.array_equal(raw_a.view(np.uint32), raw_b.view(np.uint32))))
                assert delta.mean() < .05 and np.quantile(delta, .99) <= 1, row
                fidelity.append(row)
            (root / "raw-fidelity.json").write_text(json.dumps(fidelity, indent=2) + "\n")
        assert identity(binary) == pinned
        return

    assert args.oidn and args.oidn.is_file(), "pass the external oidnDenoise executable"
    tool = args.oidn.resolve()
    manifest = json.loads(manifest_path.read_text())
    results = []
    for c in manifest["captures"]:
        name, prefix = c["name"], c["prefix"]
        target = root / "denoised"
        target.mkdir(exist_ok=True)
        receipt_path = target / (name + ".receipt.json")
        if receipt_path.exists():
            previous = json.loads(receipt_path.read_text())
            assert previous["oidn_sha256"] == digest(tool)
            assert previous["input_hdr_sha256"] == digest(prefix + ".bin")
            for suffix, sha in previous["artifacts"].items():
                assert digest(target / (name + suffix)) == sha
            results.append(previous)
            continue
        hdr = np.fromfile(prefix + ".bin", dtype="<f4").reshape(1080, 1920, 4)[:, :, :3]
        original = np.array(Image.open(prefix + ".png"))[:, :, :3]
        calibration = np.abs(display_rgb8(hdr).astype(float) - original)
        assert calibration.mean() < .05 and calibration.max() <= 1, "post transform mismatch"
        input_path, output_path = target / (name + "-input.pfm"), target / (name + ".pfm")
        write_pfm(input_path, hdr)
        command = [str(tool), "--device", "cuda", "--filter", "RT", "--quality", "high",
                   "--hdr", str(input_path), "--output", str(output_path), "-n", "3"]
        start = time.monotonic()
        process = subprocess.run(command, capture_output=True, text=True, timeout=180)
        wall = time.monotonic() - start
        (target / (name + ".log")).write_text(process.stdout + process.stderr)
        assert process.returncode == 0, process.stdout + process.stderr
        denoised = read_pfm(output_path)
        assert denoised.shape == hdr.shape and np.isfinite(denoised).all() and (denoised >= 0).all()
        rgba = np.concatenate([display_rgb8(denoised), np.full((1080, 1920, 1), 255, dtype=np.uint8)], axis=2)
        Image.fromarray(rgba).save(target / (name + ".png"))
        record = dict(name=name, samples=c["samples"], seed=c["seed"], clamp=c["clamp"],
                      prefix=str(target / name), command=command, oidn_sha256=digest(tool),
                      input_hdr_sha256=digest(prefix + ".bin"), finite_nonnegative=True,
                      scope="offline OIDN RT color-only CUDA high; auto input scale; no temporal claim",
                      process_wall_seconds_including_startup_io_and_three_executions=wall,
                      display_calibration_mean_rgb8=float(calibration.mean()),
                      display_calibration_max_rgb8=float(calibration.max()),
                      artifacts={s: digest(target / (name + s)) for s in [".pfm", ".png", ".log"]})
        receipt_path.write_text(json.dumps(record, indent=2) + "\n")
        results.append(record)
        (root / "denoised.json").write_text(json.dumps(results, indent=2) + "\n")
        print("denoised", name, f"{wall:.3f}s", flush=True)
    (root / "denoised.json").write_text(json.dumps(results, indent=2) + "\n")
    assert identity(binary) == pinned


if __name__ == "__main__":
    main()
