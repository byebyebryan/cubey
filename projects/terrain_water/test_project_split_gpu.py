#!/usr/bin/env python3
"""Headless project-boundary parity, with no external solver or optional IO stack."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "projects/fluid/fluid_25d"))
import run_native_shoreline_raster_v1 as fixtures


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--legacy", type=Path, required=True)
    p.add_argument("--terrain", type=Path, required=True)
    p.add_argument("--out", type=Path)
    p.add_argument("--study", type=Path, help="optional prepared Joining valleys real-scene parity")
    args = p.parse_args()
    if args.out:
        out = args.out.resolve()
        out.mkdir()  # Never overwrite a prior review.
    else:
        output_root = ROOT / "outputs/fluid"
        output_root.mkdir(parents=True, exist_ok=True)
        out = Path(tempfile.mkdtemp(prefix="terrain-water-split-", dir=output_root))
    old, new = args.legacy.resolve(strict=True), args.terrain.resolve(strict=True)
    shaders = list((new.parent / "shaders").glob("*.spv"))
    if not shaders or any(sha(path) != sha(old.parent / "shaders" / path.name) for path in shaders):
        raise ValueError("shared shader artifacts differ across project frontends")
    manifest = fixtures.fixture(out / "fixture", "fully-wet-lake")
    before = {str(p): sha(p) for p in manifest.parent.rglob("*") if p.is_file()}
    cases = {
        "builtin-river": ["--fluid25d-backend", "builtin"],
        "recorded-water": ["--fluid25d-backend", "recording", "--fluid25d-recording", str(manifest),
                           "--fluid25d-recording-gpu-validation", "--fluid25d-native-presentation", "scenic",
                           "--fluid25d-scenic-material", "macro", "--fluid25d-native-bank-view", "bspline-2x"],
    }
    if args.study:
        study = args.study.resolve(strict=True)
        case = study / "cases/joining-valleys-patch-1024-16m"
        recording = study / "runs/joining-valleys-patch-1024-16m/recording/recording.json"
        cases["joining-valleys"] = [
            "--fluid25d-backend", "recording", "--fluid25d-recording", str(recording),
            "--fluid25d-recording-time-seconds", "21635.626953125",
            "--fluid25d-recording-gpu-validation", "--fluid25d-native-presentation", "scenic",
            "--fluid25d-scenic-material", "macro", "--fluid25d-native-bank-view", "bspline-2x",
            "--fluid25d-scenic-surface-grid", str(case / "surface-grid.json"),
            "--fluid25d-scenic-surface-mode", "correlated", "--fluid25d-native-shadow-projection", "scene",
            "--fluid25d-native-surface-highlights", "off", "--fluid25d-rain-visuals"]
    rows = []
    for name, options in cases.items():
        hashes = []
        for label, app in (("fluid25d", old), ("terrain-water", new)):
            png = out / f"{name}-{label}.png"
            width, height = (1280, 720) if name == "joining-valleys" else (384, 288)
            command = [str(app), "--headless", "--frames", "4", "--width", str(width), "--height", str(height),
                       "--output", str(png), *options]
            run = subprocess.run(command, text=True, capture_output=True, timeout=90)
            log = run.stdout + run.stderr
            (out / f"{name}-{label}.log").write_text(log)
            if run.returncode or "vulkan validation error" in log.lower() or not png.is_file():
                raise ValueError(f"{name}/{label} failed: {log[-4000:]}")
            if name == "recorded-water" and "PASS" not in log:
                raise ValueError("recorded field import was not audited")
            hashes.append(sha(png))
            rows.append({"case": name, "project": label, "command": command, "png": png.name,
                         "sha256": hashes[-1], "app_sha256": sha(app)})
        if len(set(hashes)) != 1:
            raise ValueError(f"project separation changed {name} pixels")
    after = {str(p): sha(p) for p in manifest.parent.rglob("*") if p.is_file()}
    if before != after:
        raise ValueError("recorded native fields were mutated")
    result = {"schema": "cubey.terrain-water.project-split.v1", "pass": True,
              "shader_artifacts_exact": len(shaders), "pixel_exact_pairs": len(cases),
              "native_fixture_immutable": True, "external_solver_run": False, "rows": rows}
    (out / "validation.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"pass": True, "shader_artifacts_exact": len(shaders),
                      "pixel_exact_pairs": len(cases), "out": str(out)}, indent=2))


if __name__ == "__main__":
    main()
