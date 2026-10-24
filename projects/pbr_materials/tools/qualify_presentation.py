#!/usr/bin/env python3
"""Capture the two material stories, sequentially, with per-run provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from qualify_workshops import write_timing_summary

ROOT = Path(__file__).resolve().parents[3]
PRESETS = {"pbr": "projects/pbr_materials/presets/presentation/contrast.json",
           "sdf": "projects/sdf/presets/presentation/wood-cut.json"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "outputs/material-presentation/qualification")
    parser.add_argument("--stage", choices=("captures", "films", "profiles", "all"), default="all")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binaries = {"pbr": ROOT / "build/release/projects/pbr_materials/pbr_materials",
                "sdf": ROOT / "build/release/projects/sdf/sdf"}
    inputs = list((ROOT / "assets/materials").glob("*.json")) + [ROOT / p for p in PRESETS.values()]
    inputs.append(ROOT / "projects/sdf/scenes/material-presentation/wood-cut.json")
    identity = dict(source_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        source_dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
        binaries={r: sha(b) for r, b in binaries.items()},
        shaders={r: {p.name: sha(p) for p in sorted((b.parent / "shaders").glob("*.spv"))} for r, b in binaries.items()},
        inputs={str(p.relative_to(ROOT)): sha(p) for p in inputs})
    for p in inputs:
        target = output / "inputs" / p.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, target)
    receipt = output / "manifest.json"
    manifest = json.loads(receipt.read_text()) if receipt.exists() else {"runs": {}, "derived": {}}

    def run(name, renderer, extra=(), suffix="png"):
        path = output / f"{name}.{suffix}"
        command = [str(binaries[renderer]), "--config", PRESETS[renderer], "--headless", "--require-validation",
                   "--width", "1920", "--height", "1080", "--output", str(path), *extra]
        result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (output / f"{name}.log").write_text(result.stdout)
        record = dict(command=command, returncode=result.returncode,
            validation_errors="vulkan validation error" in result.stdout.lower(), identity=identity,
            artifact=path.name if path.exists() else None, sha256=sha(path) if path.exists() else None,
            log=f"{name}.log")
        manifest["runs"][name] = record
        receipt.write_text(json.dumps(manifest, indent=2) + "\n")
        if result.returncode or record["validation_errors"]:
            raise RuntimeError(f"{name}: {result.stdout[-3000:]}")
        print(name, flush=True)
        return path

    def derive(name, source, arguments):
        path = output / name
        command = ["ffmpeg", "-v", "error", "-y", "-i", str(source), *arguments, str(path)]
        subprocess.run(command, check=True)
        manifest["derived"][name] = dict(command=command, source=source.name, source_sha256=sha(source), sha256=sha(path))
        receipt.write_text(json.dumps(manifest, indent=2) + "\n")
        return path

    if args.stage in ("captures", "all"):
        for renderer, samples in (("pbr", (0, 4, 8)), ("sdf", (0, 2.1, 3, 8))):
            for t in samples:
                extra = ["--presentation-time" if renderer == "pbr" else "--time", str(t)]
                if renderer == "sdf" and t in (0, 8):
                    extra += ["--validate-field", "--validate-history"]
                run(f"{renderer}-{t}", renderer, extra)
    if args.stage in ("films", "all"):
        for renderer in ("pbr", "sdf"):
            master = run(f"{renderer}-master-1080p", renderer,
                         ("--capture", "video", "--frames", "480", "--fps", "60"), "mp4")
            gallery = derive(f"{renderer}.mp4", master, ["-an", "-vf", "scale=1280:720:flags=lanczos",
                "-c:v", "libx264", "-preset", "medium", "-crf", "24", "-pix_fmt", "yuv420p", "-r", "60",
                "-movflags", "+faststart"])
            derive(f"{renderer}-poster.png", gallery,
                   ["-ss", "6.5", "-frames:v", "1", "-vf", "format=rgb24"])
    if args.stage in ("profiles", "all"):
        for renderer in ("pbr", "sdf"):
            run(f"profile-{renderer}", renderer,
                ("--presentation-time" if renderer == "pbr" else "--time", "8",
                 "--headless-render-only", "--frames", "112", "--profile-warmup-frames", "16",
                 "--profile-output", str(output / f"profile-{renderer}")))
        write_timing_summary(output)


if __name__ == "__main__":
    main()
