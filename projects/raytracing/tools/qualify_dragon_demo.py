# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.5", "pillow==13.0.0"]
# ///
"""Real-GPU checks for fixed-frame transport and the matched forward adapter.

Requires the prepared optical dragon. Run separately from timing/video work.
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
    parser.add_argument("--binary", type=Path, default=Path("build/dev/projects/raytracing/raytracing"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--raw-reference", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    binary_hash = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    checks = {}

    def capture(name, extra=(), *, optics=False, config="dragon-demo"):
        prefix = args.output / name
        command = [str(args.binary), "--config",
                   f"projects/raytracing/presets/optics/{config}.json",
                   "--headless", "--width", "320", "--height", "180", "--require-validation",
                   "--output", str(prefix.with_suffix(".png")),
                   "--metrics", str(prefix.with_suffix(".json")),
                   "--optics-output" if optics else "--hdr-output", str(prefix.with_suffix(".bin")),
                   *extra]
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        prefix.with_suffix(".log").write_text(result.stdout)
        assert result.returncode == 0, result.stdout
        assert not any(x in result.stdout.lower() for x in ("vuid-", "validation error", "hazard detected")), result.stdout
        data = np.fromfile(prefix.with_suffix(".bin"), dtype=np.float32)
        assert np.isfinite(data).all()
        records = data.reshape(180, 320, 16 if optics else 1, 4)
        hdr = records[:, :, 10, :3] if optics else records[:, :, 0, :3]
        assert (hdr >= 0).all()
        status = records[:, :, 11, 3].astype(np.uint32) if optics else records[:, :, 0, 3].astype(np.uint32)
        counts = {str(bit): int(np.count_nonzero(status & bit)) for bit in (1,2,4,8,16,32,64,128)}
        # Policy pruning is deliberately separate. Legacy numeric-tail status 8
        # remains reported; invalid medium, work exhaustion and stack overflow fail.
        assert not np.any(status & (1|2|4|16|32|64)), counts
        checks[name] = {"command": command, "finite_nonnegative": True,
                       "validation_clean": True, "status_pixels": counts}
        print(name, counts, flush=True)
        return prefix, json.loads(prefix.with_suffix(".json").read_text())

    a, _ = capture("demo-four", ("--samples", "1", "--seed", "7"))
    b, _ = capture("demo-other-seed", ("--samples", "8192", "--seed", "99"))
    assert a.with_suffix(".bin").read_bytes() == b.with_suffix(".bin").read_bytes()
    assert np.array_equal(np.array(Image.open(a.with_suffix(".png"))), np.array(Image.open(b.with_suffix(".png"))))
    checks["seed_and_offline_target_independent"] = True
    capture("demo-optics", optics=True)
    capture("demo-two", ("--demo-samples", "2"), optics=True)

    command = [str(args.binary), "--config", "projects/raytracing/presets/optics/dragon-demo.json",
               "--headless", "--headless-render-only", "--frames", "3", "--width", "320", "--height", "180",
               "--require-validation", "--metrics", str(args.output / "frames.json")]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (args.output / "frames.log").write_text(result.stdout)
    assert result.returncode == 0 and "validation error" not in result.stdout.lower(), result.stdout
    frames = json.loads((args.output / "frames.json").read_text())["sampling_frames"]
    assert len(frames) == 3 and all(f["samples_before"] == 0 and f["new_samples"] == 4 and f["recorded_dispatches"] == 4 for f in frames)
    checks["native_style_frames_complete_without_history"] = frames

    command = [str(args.binary), "--config", "projects/raytracing/presets/optics/dragon-forward.json",
               "--headless", "--require-validation", "--width", "320", "--height", "180",
               "--output", str(args.output / "forward.png"), "--metrics", str(args.output / "forward.json")]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (args.output / "forward.log").write_text(result.stdout)
    assert result.returncode == 0 and "validation error" not in result.stdout.lower(), result.stdout
    forward = json.loads((args.output / "forward.json").read_text())
    assert forward["renderer"] == "forward" and not forward["ray_tracing_enabled"] and forward["as_bytes"] == 0
    checks["genuine_non_rt_forward"] = True
    if args.raw_reference:
        raw, _ = capture("sampled-raw", ("--firefly-clamp", "0", "--samples", "32", "--seed", "19"), config="dragon-glass")
        previous = np.fromfile(args.raw_reference, dtype=np.float32)
        current = np.fromfile(raw.with_suffix(".bin"), dtype=np.float32)
        checks["sampled_raw_reference"] = {"path": str(args.raw_reference),
            "bitwise_equal": np.array_equal(current, previous),
            "max_abs_difference": float(np.max(abs(current-previous)))}
        # A newly compiled optional shader branch can change float rounding.
        # Require tight HDR agreement and unchanged transport status separately.
        assert np.allclose(current.reshape(-1, 4)[:, :3], previous.reshape(-1, 4)[:, :3], rtol=1e-5, atol=2e-6)
        assert np.array_equal(current.reshape(-1, 4)[:, 3], previous.reshape(-1, 4)[:, 3])
        previous_png = args.raw_reference.with_suffix(".png")
        if previous_png.exists():
            pixel_equal = np.array_equal(np.array(Image.open(raw.with_suffix(".png"))), np.array(Image.open(previous_png)))
            checks["sampled_raw_reference"]["png_pixel_equal"] = pixel_equal
            assert pixel_equal
    assert hashlib.sha256(args.binary.read_bytes()).hexdigest() == binary_hash
    (args.output / "validation.json").write_text(json.dumps({"passed": True,
        "binary_sha256": binary_hash, "checks": checks}, indent=2)+"\n")
    print("Dragon demo checks passed", flush=True)


if __name__ == "__main__":
    main()
