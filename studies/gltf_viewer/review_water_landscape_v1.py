#!/usr/bin/env python3
"""Gated, saved-water glTF-view audition. Never generates terrain or runs a solver."""
import argparse
import array
import csv
import hashlib
import json
import math
import os
import re
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "studies/terrain/landscapes"))
import review_captured_water_v1 as capture

OUT_IDS = ("mountain-local", "mountain-wide", "mountain-interior-wide", "valley-interior-wide")
CATALOG = ROOT / "projects/terrain/terrain_landscape_presets.json"
GLTF = ROOT / "build/release/projects/gltf_viewer/gltf_viewer"
ASSET = ROOT / "outputs/showcase/audition-2/assets/DamagedHelmet.glb"
HEADINGS = (-1.16, .25, 1.65)
PITCHES = (-.12, -.26)
QUALIFICATION_NAME = "final"


def qualified_dir(out):
    return out / QUALIFICATION_NAME


def gpu_context():
    """Read-only boundary observation, never workload control or isolation proof."""
    raw = subprocess.check_output(["rtk", "proxy", "nvidia-smi", "--query-compute-apps=pid,used_gpu_memory",
                                   "--format=csv,noheader,nounits"], text=True)
    processes = []
    for values in csv.reader(raw.splitlines()):
        if len(values) != 2:
            continue
        pid = int(values[0].strip())
        try:
            name = Path(f"/proc/{pid}/comm").read_text().strip()
        except (FileNotFoundError, PermissionError):
            name = "departed-or-unreadable"
        processes.append({"pid": pid, "name": name, "gpu_memory_mib": values[1].strip()})
    return {"processes": processes, "scope": "boundary snapshot only; shared desktop GPU; no foreign process modified"}


def write(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def presets():
    rows = {}
    cache = ROOT / "cache/terrain"
    for p in json.loads(CATALOG.read_bytes())["presets"]:
        if p["id"] not in OUT_IDS:
            continue
        site_path = cache / p["site"]["path"]
        site = json.loads(site_path.read_bytes())
        binding = cache / p["water"]["path"]
        manifest = binding.parent / "water-capture.json"
        bed = array.array("f")
        bed.frombytes((manifest.parent / "bed.f32").read_bytes())
        if sys.byteorder != "little":
            bed.byteswap()
        rows[p["id"]] = {"site": str(site_path), "binding": str(binding),
            "capture": str(manifest), "focus_xz": site["center_xz_m"],
            "maximum_bed_m": max(bed), "focus_height_m": max(bed) + 200.0}
    if set(rows) != set(OUT_IDS):
        raise ValueError("four prepared wet landscapes are required")
    return rows


def identities():
    paths = {CATALOG, ASSET, ROOT / "docs/media/showcase/manifest.json",
        ROOT / "docs/media/showcase/gltf-terrain.mp4", ROOT / "docs/media/showcase/gltf-terrain.png"}
    for p in presets().values():
        for folder in (Path(p["site"]).parent, Path(p["capture"]).parent):
            paths.update(x for x in folder.rglob("*") if x.is_file())
    return {str(p.relative_to(ROOT)): digest(p) for p in sorted(paths)}


def runtime(app):
    return {"binary": digest(app), "shaders": {p.name: digest(p) for p in
            sorted((app.parent / "shaders").glob("*.spv"))}}


def run(args, log):
    receipt = capture.run(args, log)
    text = log.read_text()
    if "VUID-" in text or "Validation Error" in text or "NVIDIA GeForce RTX 5070 Ti" not in text:
        raise ValueError("hardware Vulkan/validation evidence missing: " + str(log))
    return receipt


def baseline_command():
    return ["rtk", "proxy", str(GLTF), "--headless", "--input", str(ASSET),
        "--pbr-environment-source", "atmosphere", "--time-of-day-mode", "solar",
        "--time-hours", "14", "--time-speed-hours-per-second", ".875", "--clouds",
        "--cloud-weather-preset", "broken-cumulus", "--cloud-quality", "full",
        "--terrain-heightfield", str(ROOT / "cache/terrain/sources/v1/default"),
        "--terrain-surface-detail", "filtered-detail", "--terrain-shadows",
        "--terrain-foreground-height", "200", "--capture-camera-distance-scale", ".70",
        "--exposure", "1", "--ibl-intensity", "1"]


def view_command(id, yaw, pitch, info, water=True):
    # Tiny orbit radius makes the pivot a foreground-proxy camera, not a
    # landscape overview. Terrain's 16m minimum and 8m near clip are disclosed;
    # actual glTF confirmation later uses the unmodified asset camera.
    x, z = info["focus_xz"]
    args = ["rtk", "proxy", str(capture.APP), "--headless", "--terrain-landscape", id,
        "--terrain-water-span", "16", "--terrain-water-focus-x", str(x),
        "--terrain-water-focus-z", str(z), "--terrain-water-focus-height", str(info["focus_height_m"]),
        "--terrain-water-yaw", str(yaw), "--terrain-water-pitch", str(pitch)]
    if not water:
        args.append("--no-terrain-water-visible")
    return args


def freeze(out):
    write(out / "baseline-freeze.json", {"git_head": subprocess.check_output(
        ["rtk", "proxy", "git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "terrain_runtime": runtime(capture.APP), "gltf_runtime": runtime(GLTF),
        "inputs": identities(), "sites": presets(), "camera": {"yaw_rad": list(HEADINGS),
        "pitch_rad": list(PITCHES), "vertical_fov_deg": 60, "proxy_orbit_distance_m": 16,
        "proxy_near_clip_m": 8, "foreground_clearance_over_maximum_bed_m": 200},
        "scope": "Four independent saved storm states. No terrain generation, solver, GUI or media promotion."})
    receipt = run(baseline_command() + ["--frames", "1", "--width", "1280", "--height", "720",
        "--output", str(out / "baseline.png")], out / "baseline.log")
    write(out / "baseline-run.json", receipt)


def audition(out):
    frozen = json.loads((out / "baseline-freeze.json").read_bytes())
    if frozen["inputs"] != identities() or frozen["terrain_runtime"] != runtime(capture.APP):
        raise ValueError("audition inputs/runtime changed")
    rows = []
    for id, info in frozen["sites"].items():
        for yi, yaw in enumerate(HEADINGS):
            for pi, pitch in enumerate(PITCHES):
                name = f"{id}-h{yi}-p{pi}"
                image = out / (name + ".png")
                receipt = run(view_command(id, yaw, pitch, info) + ["--frames", "1",
                    "--width", "1280", "--height", "720", "--terrain-water-audit",
                    "--output", str(image)], out / (name + ".log"))
                if "GPU bed/depth/decoded-velocity exact" not in (out / (name + ".log")).read_text():
                    raise ValueError("immutable field audit missing")
                rows.append({"id": id, "heading": yi, "pitch": pi, "image": image.name,
                    "yaw_rad": yaw, "pitch_rad": pitch, "sha256": digest(image), **receipt})
                print("audition", name, flush=True)
    if frozen["inputs"] != identities() or frozen["terrain_runtime"] != runtime(capture.APP):
        raise ValueError("inputs/runtime changed during audition")
    write(out / "audition.json", {"rows": rows,
        "scope": "Provisional background-only, glTF-like camera screen. Not actual model/depth integration."})


def integrated_command(id, *, hours=14, pitch=-6.8755, water=True, exposure=.4):
    args = ["rtk", "proxy", str(GLTF), "--headless", "--input", str(ASSET),
        "--terrain-landscape", id, "--no-clouds", "--time-of-day-mode", "solar",
        "--time-hours", str(hours), "--time-speed-hours-per-second", "0",
        "--capture-camera-distance-scale", ".70", "--capture-camera-yaw", str(math.degrees(1.65)),
        "--capture-camera-pitch", str(pitch), "--exposure", str(exposure), "--ibl-intensity", "1"]
    if not water:
        args.append("--no-terrain-water-visible")
    return args


def integration(out):
    frozen = json.loads((out / "baseline-freeze.json").read_bytes())
    if frozen["inputs"] != identities():
        raise ValueError("saved fields or published media changed")
    current = runtime(GLTF)
    rows = []
    for id in ("mountain-local", "mountain-interior-wide"):
        for hours in (14, 16):
            for pitch in (-6.8755, -14.8969):
                for water in (True, False):
                    name = f"actual-{id}-{hours}-{'low' if pitch > -10 else 'oblique'}-{'wet' if water else 'hidden'}"
                    image = out / (name + ".png")
                    receipt = run(integrated_command(id, hours=hours, pitch=pitch, water=water) + [
                        "--frames", "1", "--width", "1280", "--height", "720", "--terrain-water-audit",
                        "--output", str(image)], out / (name + ".log"))
                    if "GPU bed/depth/decoded-velocity exact" not in (out / (name + ".log")).read_text():
                        raise ValueError("immutable field audit missing")
                    rows.append({"id": id, "hours": hours, "pitch_deg": pitch, "water": water,
                                 "image": image.name, "sha256": digest(image), **receipt})
                    print("actual", name, flush=True)
    parity = run(baseline_command() + ["--frames", "1", "--width", "1280", "--height", "720",
        "--output", str(out / "baseline-after.png")], out / "baseline-after.log")
    difference = capture.pixel_difference(out / "baseline.png", out / "baseline-after.png")
    if difference["max_abs_8bit"]:
        raise ValueError("existing published-recipe opening frame changed")
    # Rebuilds may add the merge shader, but the no-foreground renderer must
    # remain exactly unchanged. Historical audition framing is reused, not GUI.
    info = frozen["sites"]["mountain-local"]
    terrain_parity = run(view_command("mountain-local", HEADINGS[0], PITCHES[0], info) + [
        "--frames", "1", "--width", "1280", "--height", "720", "--output",
        str(out / "terrain-after.png")], out / "terrain-after.log")
    terrain_difference = capture.pixel_difference(out / "mountain-local-h0-p0.png", out / "terrain-after.png")
    if terrain_difference["max_abs_8bit"]:
        raise ValueError("existing shared terrain/water appearance changed")
    if current != runtime(GLTF) or frozen["inputs"] != identities():
        raise ValueError("runtime/inputs changed during actual confirmation")
    write(out / "integration.json", {"runtime": current, "rows": rows,
        "baseline_parity": {"difference": difference, **parity},
        "terrain_parity": {"difference": terrain_difference, **terrain_parity},
        "scope": "Actual glTF HDR/depth composition; fixed saved hydraulics; clear sky, opaque/masked only."})


def lighting(out):
    rows = []
    current = runtime(GLTF)
    for id in ("mountain-local", "mountain-interior-wide"):
        for hours in (10, 12):
            name = f"light-{id}-{hours}"
            image = out / (name + ".png")
            receipt = run(integrated_command(id, hours=hours, exposure=0) + ["--frames", "1",
                "--width", "1280", "--height", "720", "--terrain-water-audit", "--output", str(image)],
                out / (name + ".log"))
            rows.append({"id": id, "hours": hours, "exposure": 0, "image": image.name, **receipt})
            print(name, flush=True)
    if current != runtime(GLTF):
        raise ValueError("runtime changed during lighting screen")
    write(out / "lighting.json", {"runtime": current, "rows": rows,
        "reason": "14/16h low-angle glint obscures water colour; test existing morning/noon lighting controls only."})


def final_freeze(out):
    old = json.loads((out / "baseline-freeze.json").read_bytes())
    if old["inputs"] != identities():
        raise ValueError("prepared landscape/media identities changed")
    final = qualified_dir(out)
    final.mkdir()
    changed = subprocess.check_output(["rtk", "proxy", "git", "diff", "--name-only", "HEAD"],
                                     cwd=ROOT, text=True).splitlines()
    paths = [ROOT / p for p in changed if Path(p).suffix in (".h", ".cpp", ".frag", ".cmake")
             or Path(p).name == "CMakeLists.txt"]
    paths += [ROOT / "shaders/cubey/terrain/surface_patch/terrain_surface_foreground.frag", Path(__file__)]
    write(final / "protocol.json", {"git_head": subprocess.check_output(
        ["rtk", "proxy", "git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "runtime": runtime(GLTF), "terrain_runtime": runtime(capture.APP), "inputs": old["inputs"],
        "source_files": {str(p.relative_to(ROOT)): digest(p) for p in sorted(set(paths))},
        "candidates": {"mountain-local": {"hours": 10, "exposure": 0},
                       "mountain-interior-wide": {"hours": 12, "exposure": 0}},
        "performance_gate": {"new_scene_gpu_p95_ms": 8, "cpu_service_p95_ms": 4,
                             "warmup_frames": 120, "seconds": 8, "width": 1920, "height": 1080,
                             "pacing_hz": 60, "repetitions": 2,
                             "gpu_isolation": "not enforced; observe boundary processes, never stop foreign workloads"},
        "scope": "Headless only. Saved hydraulics, no solver/generation, no public media/default promotion."})


def verify_final(out):
    frozen = json.loads((qualified_dir(out) / "protocol.json").read_bytes())
    if frozen["runtime"] != runtime(GLTF) or frozen["terrain_runtime"] != runtime(capture.APP):
        raise ValueError("qualified runtime changed")
    if frozen["inputs"] != identities():
        raise ValueError("qualified inputs/media changed")
    if any(digest(ROOT / p) != h for p, h in frozen["source_files"].items()):
        raise ValueError("qualified runtime source/driver changed")
    return frozen


def confirmation(out):
    frozen = verify_final(out)
    final = qualified_dir(out)
    rows = []
    for id in frozen["candidates"]:
        for hours, pitch in ((10, -6.8755), (12, -6.8755), (16, -6.8755), (10, -14.8969)):
            for water in (True, False):
                name = f"{id}-{hours}-{'low' if pitch > -10 else 'oblique'}-{'wet' if water else 'hidden'}"
                image = final / (name + ".png")
                receipt = run(integrated_command(id, hours=hours, pitch=pitch, water=water, exposure=0) + [
                    "--frames", "1", "--width", "1280", "--height", "720", "--terrain-water-audit",
                    "--require-validation", "--output", str(image)], final / (name + ".log"))
                text = (final / (name + ".log")).read_text()
                if "GPU bed/depth/decoded-velocity exact" not in text or "environment=caller-owned" not in text:
                    raise ValueError("caller lighting/immutable field evidence missing")
                rows.append({"id": id, "hours": hours, "pitch_deg": pitch, "water": water,
                             "image": image.name, "sha256": digest(image), **receipt})
                print("confirmed", name, flush=True)
    parity = run(baseline_command() + ["--frames", "1", "--width", "1280", "--height", "720",
        "--require-validation", "--output", str(final / "baseline.png")], final / "baseline.log")
    difference = capture.pixel_difference(out / "baseline.png", final / "baseline.png")
    if difference["max_abs_8bit"]:
        raise ValueError("old glTF appearance changed")
    info = json.loads((out / "baseline-freeze.json").read_bytes())["sites"]["mountain-local"]
    terrain_parity = run(view_command("mountain-local", HEADINGS[0], PITCHES[0], info) + [
        "--frames", "1", "--width", "1280", "--height", "720", "--require-validation",
        "--output", str(final / "terrain-control.png")], final / "terrain-control.log")
    terrain_difference = capture.pixel_difference(out / "mountain-local-h0-p0.png", final / "terrain-control.png")
    if terrain_difference["max_abs_8bit"]:
        raise ValueError("shared Terrain appearance changed")
    write(final / "confirmation.json", {"rows": rows, "baseline_parity": {"difference": difference, **parity},
        "terrain_parity": {"difference": terrain_difference, **terrain_parity}})
    verify_final(out)


def performance(out):
    frozen = verify_final(out)
    final = qualified_dir(out)
    gate = frozen["performance_gate"]
    cases = [(id, water) for id in frozen["candidates"] for water in (False, True)] + [("legacy", True)]
    rows = []
    for repeat in range(2):
        for id, water in (cases if repeat == 0 else reversed(cases)):
            name = f"perf-{id}-{'wet' if water else 'hidden'}-{repeat}"
            prefix = final / name
            if id == "legacy":
                args = baseline_command()
                args[args.index("--time-speed-hours-per-second") + 1] = "0"
            else:
                args = integrated_command(id, water=water, **frozen["candidates"][id])
            before = gpu_context()
            receipt = run(args + ["--headless-render-only", "--frames", "0", "--run-seconds", str(gate["seconds"]),
                "--render-fps", str(gate["pacing_hz"]), "--width", "1920", "--height", "1080",
                "--no-validation", "--profile-warmup-frames", str(gate["warmup_frames"]),
                "--profile-sample-interval", "1", "--profile-output", str(prefix)], final / (name + ".log"))
            text = (final / (name + ".log")).read_text()
            if "readback=0 encode=0" not in text:
                raise ValueError("render-only isolation evidence missing")
            with prefix.with_suffix(".passes.csv").open() as stream:
                passes = list(csv.DictReader(stream))
            gpu = [float(r["duration_ms"]) for r in passes if r["kind"] == "gpu" and r["label"] == "gltf viewer whole scene"]
            cpu = [float(r["duration_ms"]) for r in passes if r["kind"] == "cpu" and r["label"] == "headless.frame_service"]
            if len(gpu) < 100 or len(cpu) < 100:
                raise ValueError("insufficient steady-state complete samples")
            gpu_p95, cpu_p95 = capture.percentile(gpu), capture.percentile(cpu)
            rows.append({"id": id, "water": water, "repeat": repeat, "gpu_p95_ms": gpu_p95,
                "cpu_service_p95_ms": cpu_p95, "gpu_samples": len(gpu), "cpu_samples": len(cpu),
                "gate_pass": None if id == "legacy" else (gpu_p95 <= gate["new_scene_gpu_p95_ms"] and
                                                           cpu_p95 <= gate["cpu_service_p95_ms"]),
                "gpu_context_before": before, "gpu_context_after": gpu_context(), **receipt})
            print("timed", name, gpu_p95, cpu_p95, flush=True)
    write(final / "performance.json", {"rows": rows, "gate": gate,
        "scope": "1080p offscreen Vulkan, two reversed-order repeats, no solver/readback/encode. Legacy control holds its 14h opening lighting; not a matched site/cloud comparison."})
    verify_final(out)


def films(out):
    frozen = verify_final(out)
    final = qualified_dir(out)
    rows = []
    for id, options in frozen["candidates"].items():
        master = final / (id + "-1080p.mp4")
        args = integrated_command(id, **options)
        # Small eased arc, centred on the admitted low-angle heading. Hydraulics
        # remain held; this clip never implies a forming lake or live solver.
        args[args.index("--capture-camera-yaw") + 1] = str(math.degrees(1.65) - 10)
        receipt = run(args + ["--capture", "video", "--frames", "480", "--fps", "60",
            "--width", "1920", "--height", "1080", "--capture-video-orbit-degrees", "20",
            "--require-validation", "--terrain-water-audit", "--output", str(master)],
            final / (id + "-film.log"))
        gallery = final / (id + "-720p.mp4")
        subprocess.run(["rtk", "proxy", "ffmpeg", "-v", "error", "-n", "-i", str(master),
            "-vf", "scale=1280:720", "-an", "-c:v", "libx264", "-preset", "medium", "-crf", "22",
            "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(gallery)], check=True, timeout=120)
        probe = json.loads(subprocess.check_output(["rtk", "proxy", "ffprobe", "-v", "error",
            "-show_streams", "-show_format", "-of", "json", str(master)], text=True))
        video = next(s for s in probe["streams"] if s["codec_type"] == "video")
        if (video["width"], video["height"], video["nb_frames"], video["r_frame_rate"]) != (1920, 1080, "480", "60/1"):
            raise ValueError("film master format/frame count mismatch")
        if abs(float(probe["format"]["duration"]) - 8) > .05:
            raise ValueError("film master is not eight seconds")
        rows.append({"id": id, "master": master.name, "gallery": gallery.name,
            "master_sha256": digest(master), "gallery_sha256": digest(gallery), "probe": probe, **receipt})
        print("filmed", id, flush=True)
    write(final / "films.json", {"rows": rows, "scope": "Unpublished 8s proposals, not replacement media. Saved state; animated appearance and small camera orbit only."})
    verify_final(out)


def main():
    global QUALIFICATION_NAME
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--qualification-name", default="final",
                        help="Fresh named qualification; failed earlier attempts remain retained.")
    parser.add_argument("--phase", required=True, choices=("freeze", "audition", "integration", "lighting",
        "final_freeze", "confirmation", "performance", "films"))
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9-]+", args.qualification_name):
        raise ValueError("qualification name must be a single safe directory component")
    QUALIFICATION_NAME = args.qualification_name
    out = args.out.resolve()
    if not out.is_dir():
        raise ValueError("output directory must already exist")
    globals()[args.phase](out)


if __name__ == "__main__":
    main()
