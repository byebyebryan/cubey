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
    parser.add_argument("--stage", choices=("captures", "films", "profiles", "controls", "metadata", "all"), default="all")
    parser.add_argument("--renderer", choices=("pbr", "sdf"), help="Refresh just the affected renderer")
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
            if args.renderer and renderer != args.renderer:
                continue
            for t in samples:
                extra = ["--presentation-time" if renderer == "pbr" else "--time", str(t)]
                if renderer == "sdf" and t in (0, 8):
                    extra += ["--validate-field", "--validate-history"]
                run(f"{renderer}-{t}", renderer, extra)
    if args.stage in ("films", "all"):
        for renderer in ("pbr", "sdf"):
            if args.renderer and renderer != args.renderer:
                continue
            master = run(f"{renderer}-master-1080p", renderer,
                         ("--capture", "video", "--frames", "480", "--fps", "60"), "mp4")
            gallery = derive(f"{renderer}.mp4", master, ["-an", "-vf", "scale=1280:720:flags=lanczos",
                "-c:v", "libx264", "-preset", "medium", "-crf", "24", "-pix_fmt", "yuv420p", "-r", "60",
                "-movflags", "+faststart"])
            derive(f"{renderer}-poster.png", gallery,
                   ["-ss", "6.5", "-frames:v", "1", "-vf", "format=rgb24"])
    if args.stage in ("profiles", "all"):
        for renderer in ("pbr", "sdf"):
            if args.renderer and renderer != args.renderer:
                continue
            run(f"profile-{renderer}", renderer,
                ("--presentation-time" if renderer == "pbr" else "--time", "8",
                 "--headless-render-only", "--frames", "112", "--profile-warmup-frames", "16",
                 "--profile-output", str(output / f"profile-{renderer}")))
        write_timing_summary(output)
    if args.stage in ("controls", "all"):
        controls = json.loads(Path(__file__).with_name("presentation_controls.json").read_text())
        results = {}
        for name, control in controls.items():
            if args.renderer and control["renderer"] != args.renderer:
                continue
            artifact = output / f"compat-{name}.png"
            command = [str(binaries[control["renderer"]]), *control["arguments"], "--output", str(artifact)]
            result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            (output / f"compat-{name}.log").write_text(result.stdout)
            record = dict(command=command, returncode=result.returncode,
                validation_errors="vulkan validation error" in result.stdout.lower(), artifact=artifact.name,
                sha256=sha(artifact) if artifact.exists() else None, baseline_sha256=control["sha256"], identity=identity)
            record["byte_identical"] = record["sha256"] == record["baseline_sha256"]
            results[name] = record
            (output / "compatibility.json").write_text(json.dumps(results, indent=2) + "\n")
            if result.returncode or record["validation_errors"] or not record["byte_identical"]:
                raise RuntimeError(f"retained control differs: {name}; inspect {artifact}")
    if args.stage in ("films", "metadata", "all"):
        videos = {}
        for name in ("pbr.mp4", "sdf.mp4", "pbr-master-1080p.mp4", "sdf-master-1080p.mp4"):
            command = ["ffprobe", "-v", "error", "-count_frames", "-show_entries",
                "stream=codec_type,codec_name,profile,width,height,pix_fmt,r_frame_rate,nb_read_frames:format=duration",
                "-of", "json", str(output / name)]
            value = json.loads(subprocess.check_output(command, text=True))
            streams = value["streams"]
            if (len(streams) != 1 or streams[0]["codec_type"] != "video" or
                    streams[0]["codec_name"] != "h264" or streams[0]["nb_read_frames"] != "480" or
                    streams[0]["r_frame_rate"] != "60/1" or float(value["format"]["duration"]) != 8):
                raise RuntimeError(f"decoded-video contract failed: {name}")
            videos[name] = value
        (output / "video-qa.json").write_text(json.dumps(videos, indent=2) + "\n")
        gpu = subprocess.check_output(["nvidia-smi", "--query-gpu=name,driver_version", "--format=csv,noheader"], text=True).strip()
        (output / "hardware.json").write_text(json.dumps(dict(gpu=gpu, capture_dimensions=[1920, 1080], supersample=1), indent=2) + "\n")


if __name__ == "__main__":
    main()
