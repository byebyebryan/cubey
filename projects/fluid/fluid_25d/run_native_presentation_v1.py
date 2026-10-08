#!/usr/bin/env python3
"""Build a bounded remote-review gallery from frozen native recordings.

This runner only asks Cubey to replay already-recorded fields.  It does not
start a hydraulic solver.  Each phase owns an exclusive output directory and
records the exact executable, compiled shader, recording, requested-time, and
media identities used for its artifacts.
"""
from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import html
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[3]
OUTPUT_ROOT = ROOT / "outputs/fluid"
DEFAULT_OUT = OUTPUT_ROOT / "presentation-v1-20261005-2c14bc0d"
INPUT_ROOT = ROOT / "outputs/fluid/native-rain-recession-v1-20261003-1ZMqTJ"
APP = ROOT / "build/dev/projects/fluid/fluid_25d/fluid_25d"
SHADER_ROOT = ROOT / "build/dev/projects/fluid/fluid_25d/shaders"
PROTOCOL = INPUT_ROOT / "protocol.json"
CASES = ("rain-off", "rain-on")
CAMERAS = ("overview", "runoff", "collection")
DIAGNOSTICS = ("depth", "flow", "wet-dry")
WIDTH, HEIGHT = 960, 540
SAVED_INTERVAL_S = 60

# These are the immutable recording leaves inspected for this presentation.
# They are pinned independently of the recording manifests so replacing a
# frame and editing its manifest cannot silently change a later candidate.
PROTOCOL_SHA256 = "6ac7fb333460d90e2dfa4f4eeb852b6ab65af6f466e3668ee23f1a1938a04462"
EXPECTED_INPUT_TREE_SHA256 = {
    "rain-off": "109430e3dcb93b1e29388c8e0cb5ad16cba8864b574e4ec1e4f46fa2c8ee3c69",
    "rain-on": "9f7a4a2a1ef9149173378cc4ab9f8e6ab21916c5e36c71f5422f555bba7452b2",
}
HISTORICAL_BINARY_SHA256 = "2432ac14df634717cbd3aafd9b632fab0e2abe99d57caf3d6959a7fa8c1069a2"
HISTORICAL_HEAD = "5cb8059ce78907f67882ec90f8abf0981cfa0adf"
EXPECTED_CURRENT_BINARY_SHA256 = "8122c97100d25848756b62b5060d2f28cc915c4db6180f0d9aa0bde412ff546e"

VARIANTS = ("original", "motion", "motion-markers", "readable", "readable-markers")
CANDIDATE_VARIANTS = ("motion", "motion-markers", "readable", "readable-markers")
HISTORICAL_TIMES_S = (900, 6000, 7260)
CURRENT_STILL_TIMES_S = (900, 6000, 7260, 14400)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n").encode()


def write_json_exclusive(path: Path, value: object) -> None:
    """Write a new manifest and refuse to replace any existing evidence."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(json_bytes(value))


def write_text_exclusive(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(value)


def reserve_directory(path: Path) -> None:
    """Create an owned phase directory without accepting stale output."""
    if path.is_symlink() or path.exists():
        raise FileExistsError(f"phase output already exists: {path}")
    path.mkdir(parents=True, exist_ok=False)


def checked_output_root(path: Path) -> Path:
    resolved = path.resolve()
    if resolved.parent != OUTPUT_ROOT.resolve() or not resolved.name.startswith("presentation-v1-"):
        raise ValueError("--out must be a fresh presentation-v1-* leaf directly under outputs/fluid")
    if path.is_symlink():
        raise ValueError("--out may not be a symlink")
    return resolved


def recording_tree_fingerprint(directory: Path) -> dict:
    if not directory.is_dir() or directory.is_symlink():
        raise ValueError(f"recording directory is missing or a symlink: {directory}")
    tree = hashlib.sha256()
    files: dict[str, dict] = {}
    total = 0
    for path in sorted(directory.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"recording contains a symlink: {path}")
        if not path.is_file():
            continue
        relative = path.relative_to(directory).as_posix()
        size = path.stat().st_size
        digest = sha256_file(path)
        tree.update(f"{relative}\t{size}\t{digest}\n".encode())
        files[relative] = {"bytes": size, "sha256": digest}
        total += size
    return {
        "tree_sha256": tree.hexdigest(),
        "file_count": len(files),
        "bytes": total,
        "files": files,
    }


def validate_recording_manifest(key: str, directory: Path, fingerprint: dict) -> dict:
    if key not in CASES:
        raise ValueError(f"unknown frozen input case: {key}")
    manifest_path = directory / "recording.json"
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema") != "cubey.fluid25d.recording.v1":
        raise ValueError(f"unexpected recording schema: {manifest_path}")
    grid = manifest.get("grid", {})
    if (grid.get("width"), grid.get("height"), grid.get("cell_size_m")) != (512, 512, 30.0):
        raise ValueError(f"unexpected immutable recording grid: {manifest_path}")
    if manifest.get("fields") != ["h_m", "qx_m2_per_s", "qz_m2_per_s"]:
        raise ValueError(f"unexpected recording field order: {manifest_path}")
    frame_times = [frame.get("time_s") for frame in manifest.get("frames", [])]
    expected_times = [float(t) for t in range(0, 14401, SAVED_INTERVAL_S)]
    if frame_times != expected_times:
        raise ValueError(f"recording does not contain the frozen 60-second cadence: {manifest_path}")
    for frame in manifest["frames"]:
        relative = Path(frame["path"])
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"recording contains an unsafe frame path: {frame['path']}")
        item = fingerprint["files"].get(relative.as_posix())
        if item is None or frame.get("sha256") != item["sha256"]:
            raise ValueError(f"recording frame hash does not match the frozen payload: {frame['path']}")
    protocol = manifest.get("protocol", {})
    if protocol.get("duration_s") != 14400 or protocol.get("output_interval_s") != 60:
        raise ValueError(f"recording has a different duration or save cadence: {manifest_path}")
    return {
        "manifest_sha256": fingerprint["files"]["recording.json"]["sha256"],
        "tree_sha256": fingerprint["tree_sha256"],
        "frame_count": len(frame_times),
        "saved_times_s": frame_times,
        "protocol_name": protocol.get("name"),
        "rainfall_history": protocol.get("rainfall_history"),
    }


def frozen_input_identity() -> dict:
    if sha256_file(PROTOCOL) != PROTOCOL_SHA256:
        raise ValueError("frozen rain-recession protocol hash changed")
    result = {"protocol_sha256": PROTOCOL_SHA256, "cases": {}}
    for key in CASES:
        directory = INPUT_ROOT / "recordings" / key
        fingerprint = recording_tree_fingerprint(directory)
        if fingerprint["tree_sha256"] != EXPECTED_INPUT_TREE_SHA256[key]:
            raise ValueError(f"immutable recording payload changed: {key}")
        record = validate_recording_manifest(key, directory, fingerprint)
        result["cases"][key] = {
            **{k: fingerprint[k] for k in ("tree_sha256", "file_count", "bytes")},
            **record,
        }
    return result


def shader_identity() -> dict:
    if not SHADER_ROOT.is_dir():
        raise ValueError(f"compiled shader directory is missing: {SHADER_ROOT}")
    shaders = {}
    for path in sorted(SHADER_ROOT.glob("*.spv")):
        if path.is_symlink():
            raise ValueError(f"compiled shader output is a symlink: {path}")
        shaders[path.name] = sha256_file(path)
    if not shaders:
        raise ValueError("no compiled Fluid 2.5D SPIR-V files found")
    aggregate = hashlib.sha256()
    for name, digest in shaders.items():
        aggregate.update(f"{name}\t{digest}\n".encode())
    return {"tree_sha256": aggregate.hexdigest(), "files": shaders}


def rtk_output(*args: str, timeout: int = 240) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["rtk", "proxy", *map(str, args)],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )


def git_output(*args: str) -> str:
    result = rtk_output("git", *args, timeout=30)
    if result.returncode != 0:
        raise RuntimeError(f"git command failed: {result.args}: {result.stderr}")
    return result.stdout.strip()


def source_identity() -> dict:
    diff = rtk_output("git", "diff", "--binary", "HEAD", timeout=30)
    if diff.returncode != 0:
        raise RuntimeError("could not fingerprint the tracked worktree")
    return {
        "head": git_output("rev-parse", "HEAD"),
        "status_short": git_output("status", "--short"),
        "tracked_diff_sha256": hashlib.sha256(diff.stdout.encode()).hexdigest(),
    }


def runtime_identity(include_source: bool = True) -> dict:
    if not APP.is_file() or APP.is_symlink():
        raise ValueError(f"Fluid 2.5D executable is missing or a symlink: {APP}")
    identity = {
        "executable": str(APP.relative_to(ROOT)),
        "executable_sha256": sha256_file(APP),
        "compiled_shaders": shader_identity(),
        "inputs": frozen_input_identity(),
    }
    if include_source:
        identity["source"] = source_identity()
    return identity


def critical_runtime_identity(identity: dict) -> dict:
    return {
        "executable": identity["executable"],
        "executable_sha256": identity["executable_sha256"],
        "compiled_shaders": identity["compiled_shaders"],
        "inputs": identity["inputs"],
    }


def assert_same_runtime(expected: dict, actual: dict, where: str) -> None:
    if critical_runtime_identity(expected) != critical_runtime_identity(actual):
        raise ValueError(f"executable, SPIR-V, or immutable input changed {where}")


def timeline(start_s: int, frames: int, interval_s: int, saved_interval_s: int = SAVED_INTERVAL_S) -> list[dict]:
    if start_s < 0 or frames < 1 or interval_s < 1 or saved_interval_s < 1:
        raise ValueError("timeline needs nonnegative start and positive frame/cadence values")
    rows = []
    for index in range(frames):
        requested = start_s + index * interval_s
        saved = (requested // saved_interval_s) * saved_interval_s
        rows.append({
            "frame_index": index,
            "requested_time_s": requested,
            "saved_field_time_s": saved,
        })
    return rows


def still_asset(case: str, camera: str, time_s: int) -> dict:
    return {
        "id": f"{case}-{camera}-{time_s}s",
        "kind": "still",
        "case": case,
        "camera": camera,
        "requested_time_s": time_s,
        "saved_field_time_s": time_s,
        "width": WIDTH,
        "height": HEIGHT,
    }


def diagnostic_asset(case: str, view: str, time_s: int) -> dict:
    return {
        "id": f"{case}-diagnostic-{view}-{time_s}s",
        "kind": "diagnostic",
        "case": case,
        "view": view,
        "requested_time_s": time_s,
        "saved_field_time_s": time_s,
        "width": WIDTH,
        "height": HEIGHT,
    }


def video_asset(label: str, case: str, camera: str, start_s: int, frames: int, fps: int, interval_s: int) -> dict:
    rows = timeline(start_s, frames, interval_s)
    return {
        "id": f"{label}-{case}",
        "kind": "video",
        "case": case,
        "camera": camera,
        "requested_start_time_s": start_s,
        "requested_end_time_s": rows[-1]["requested_time_s"],
        "frame_count": frames,
        "fps": fps,
        "render_interval_s": interval_s,
        "saved_field_interval_s": SAVED_INTERVAL_S,
        "timeline": rows,
        "width": WIDTH,
        "height": HEIGHT,
        "label": label,
        "physical_field_interpolation": False,
        "presentation_cues": "approximate local render cues, not native particles or conserved dye",
    }


def historical_capture_plan() -> list[dict]:
    assets = [still_asset(case, camera, time_s)
              for case in CASES for camera in CAMERAS for time_s in HISTORICAL_TIMES_S]
    assets.extend(diagnostic_asset(case, view, 8160) for case in CASES for view in DIAGNOSTICS)
    clips = (
        ("early-overview", "overview", 0, 16, 8, 60),
        ("mature-runoff", "runoff", 4800, 301, 30, 5),
        ("recession-collection", "collection", 7260, 16, 8, 60),
    )
    assets.extend(video_asset(label, case, camera, start, count, fps, interval)
                  for case in CASES for label, camera, start, count, fps, interval in clips)
    return assets


def current_capture_plan(variant: str) -> list[dict]:
    if variant not in VARIANTS:
        raise ValueError(f"unknown presentation variant: {variant}")
    assets: list[dict] = []
    if variant == "original":
        assets.extend(still_asset(case, camera, t)
                      for case in CASES for camera in CAMERAS for t in CURRENT_STILL_TIMES_S)
        assets.extend(diagnostic_asset(case, view, 14400)
                      for case in CASES for view in DIAGNOSTICS)
        clips = (
            ("development-overview", "overview", 0, 101, 8, 60),
            ("mature-runoff", "runoff", 4800, 301, 30, 5),
            ("recession-collection", "collection", 7260, 120, 8, 60),
        )
        assets.extend(video_asset(label, case, camera, start, count, fps, interval)
                      for case in CASES for label, camera, start, count, fps, interval in clips)
    elif variant in ("motion", "motion-markers"):
        assets.extend(still_asset(case, "runoff", 6000) for case in CASES) if variant == "motion" else None
        assets.extend(video_asset("mature-runoff", case, "runoff", 4800, 301, 30, 5) for case in CASES)
    elif variant in ("readable", "readable-markers"):
        if variant == "readable":
            assets.extend(still_asset(case, camera, 14400) for case in CASES for camera in CAMERAS)
            assets.extend(video_asset("development-overview", case, "overview", 0, 101, 8, 60)
                          for case in CASES)
            assets.extend(video_asset("recession-collection", case, "collection", 7260, 120, 8, 60)
                          for case in CASES)
        assets.extend(video_asset("mature-runoff", case, "runoff", 4800, 301, 30, 5) for case in CASES)
    return assets


def profile_plan(variant: str) -> dict:
    if variant not in VARIANTS:
        raise ValueError(f"unknown presentation variant: {variant}")
    return {
        "cases": list(CASES),
        "camera": "runoff",
        "requested_time_s": 6000,
        "capture_frames": 120,
        "fps": 30,
        "requested_interval_s": 5,
        "requested_end_time_s": 6595,
        "profile_warmup_frames": 12,
        "measured_gpu_scope": "fluid_25d native presentation total",
        "measured_gpu_scope_sample_count": 108,
        "presentation": variant.removesuffix("-markers"),
        "motion_markers": variant.endswith("-markers"),
        "purpose": "matched bounded replay video; GPU timestamp scope only is treated as rendering duration",
        "frames_csv_delta_ms": "requested capture cadence, not rendering performance",
        "process_wall_s": "reported separately; includes startup, capture, encoding and export",
    }


def validate_probe(probe: dict, frames: int, fps: int, width: int = WIDTH, height: int = HEIGHT) -> dict:
    streams = probe.get("streams", [])
    if len(streams) != 1:
        raise ValueError("MP4 must contain exactly one video stream")
    stream = streams[0]
    expected_fps = f"{fps}/1"
    if (int(stream.get("width", -1)), int(stream.get("height", -1))) != (width, height):
        raise ValueError("MP4 dimensions do not match the requested render size")
    if stream.get("r_frame_rate") != expected_fps:
        raise ValueError(f"MP4 frame rate mismatch: {stream.get('r_frame_rate')} != {expected_fps}")
    if stream.get("nb_frames") not in (None, "N/A") and int(stream["nb_frames"]) != frames:
        raise ValueError("MP4 container frame count mismatch")
    if stream.get("nb_read_frames") in (None, "N/A") or int(stream["nb_read_frames"]) != frames:
        raise ValueError("ffprobe could not decode the planned number of MP4 frames")
    duration = float(probe.get("format", {}).get("duration", "nan"))
    expected_duration = (frames - 1) / fps
    if not math.isfinite(duration) or abs(duration - expected_duration) > (1.0 / fps + 0.002):
        raise ValueError(f"MP4 duration mismatch: {duration} vs {expected_duration:.6f}s frame span")
    return {
        "width": width,
        "height": height,
        "fps": fps,
        "frames": frames,
        "decoded_frames": int(stream["nb_read_frames"]),
        "duration_s": duration,
        "frame_span_s": expected_duration,
        "container_duration_tolerance_s": 1.0 / fps + 0.002,
    }


def validate_phase_manifest(path: Path, expected_runtime: dict | None = None) -> dict:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"phase manifest is missing: {path}")
    manifest = json.loads(path.read_text())
    if manifest.get("schema") != "cubey.fluid25d.presentation_capture.v1":
        raise ValueError(f"unsupported phase manifest: {path}")
    if expected_runtime is not None:
        assert_same_runtime(expected_runtime, manifest["runtime_identity"], f"in manifest {path}")
    for asset in manifest.get("assets", []):
        media = path.parent / asset["path"]
        if media.is_symlink() or not media.is_file() or sha256_file(media) != asset["sha256"]:
            raise ValueError(f"phase media hash mismatch: {media}")
        review = asset.get("review_video")
        if review:
            review_path = path.parent / review["path"]
            if (review_path.is_symlink() or not review_path.is_file()
                    or sha256_file(review_path) != review["sha256"]):
                raise ValueError(f"derived review video hash mismatch: {review_path}")
            browser_probe = validate_probe(ffprobe_video(review_path, ignore_editlist=False),
                                           asset["frame_count"], asset["fps"])
            review["browser_playback_probe"] = browser_probe
    for profile in manifest.get("profiles", []):
        for relative, digest in profile.get("artifact_sha256", {}).items():
            artifact = path.parent / relative
            if artifact.is_symlink() or not artifact.is_file() or sha256_file(artifact) != digest:
                raise ValueError(f"profile artifact hash mismatch: {artifact}")
    return manifest


def png_dimensions(path: Path) -> tuple[int, int]:
    with path.open("rb") as stream:
        header = stream.read(24)
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise ValueError(f"capture is not a valid PNG: {path}")
    return int.from_bytes(header[16:20], "big"), int.from_bytes(header[20:24], "big")


def expected_capture_rows(asset: dict, presentation: str | None) -> list[dict]:
    if asset["kind"] in ("still", "diagnostic"):
        requested = asset["requested_time_s"]
        saved_interval_s = asset.get("saved_field_interval_s", SAVED_INTERVAL_S)
        if saved_interval_s <= 0:
            raise ValueError("saved field cadence must be positive")
        return [{
            "frame_index": 0,
            "requested_time_s": requested,
            "saved_field_time_s": (requested // saved_interval_s) * saved_interval_s,
            "visual_delta_s": 0.0,
        }]
    rows = asset["timeline"]
    expected = []
    previous_requested = rows[0]["requested_time_s"]
    previous_saved = rows[0]["saved_field_time_s"]
    for index, row in enumerate(rows):
        requested, saved = row["requested_time_s"], row["saved_field_time_s"]
        if index == 0:
            visual_delta = 0.0
        elif presentation == "original":
            visual_delta = min(60.0, max(0.0, saved - previous_saved))
        else:
            visual_delta = min(60.0, max(0.0, requested - previous_requested))
        expected.append({
            "frame_index": row["frame_index"],
            "requested_time_s": requested,
            "saved_field_time_s": saved,
            "visual_delta_s": visual_delta,
        })
        previous_requested, previous_saved = requested, saved
    return expected


def verify_capture_log(text: str, require_upload_validation: bool, expected_asset: dict | None = None,
                       expected_presentation: str | None = None) -> dict:
    lowered = text.lower()
    if "vulkan validation error" in lowered:
        raise ValueError("Cubey reported a Vulkan validation error")
    if require_upload_validation and "fluid_25d_recording_upload: pass" not in lowered:
        raise ValueError("representative recording upload validation did not pass")
    capture_pattern = re.compile(
        r"^fluid_25d_recording_capture: output_frame=(\d+) requested_s=([-+0-9.eE]+) "
        r"saved_s=([-+0-9.eE]+) camera=(\S+) hydraulic_dispatches=(\d+) "
        r"visual_delta_s=([-+0-9.eE]+) presentation=([a-z-]+)$",
        re.MULTILINE,
    )
    captures = [
        {
            "frame_index": int(match.group(1)),
            "requested_time_s": float(match.group(2)),
            "saved_field_time_s": float(match.group(3)),
            "camera": match.group(4),
            "hydraulic_dispatches": int(match.group(5)),
            "visual_delta_s": float(match.group(6)),
            "presentation": match.group(7),
        }
        for match in capture_pattern.finditer(text)
    ]
    if any(row["hydraulic_dispatches"] != 0 for row in captures):
        raise ValueError("recording replay unexpectedly reported hydraulic dispatches")
    if expected_asset is not None:
        expected_rows = expected_capture_rows(expected_asset, expected_presentation)
        if [row["frame_index"] for row in captures] != [row["frame_index"] for row in expected_rows]:
            raise ValueError("recording capture log has a missing, duplicated, or out-of-order frame")
        for actual, expected in zip(captures, expected_rows):
            for key in ("requested_time_s", "saved_field_time_s", "visual_delta_s"):
                if not math.isclose(actual[key], float(expected[key]), rel_tol=0.0, abs_tol=1e-6):
                    raise ValueError(f"recording capture {key} mismatch at frame {actual['frame_index']}")
            if actual["presentation"] != expected_presentation:
                raise ValueError(f"recording presentation mode mismatch at frame {actual['frame_index']}")
            if ("camera" in expected_asset and actual["camera"] != expected_asset["camera"]):
                raise ValueError(f"recording camera mismatch at frame {actual['frame_index']}")
    elif re.search(r"(?:^|\s)hydraulic_dispatches=(\d+)(?=\s|$)", text):
        if any(int(value) != 0 for value in re.findall(r"(?:^|\s)hydraulic_dispatches=(\d+)(?=\s|$)", text)):
            raise ValueError("recording replay unexpectedly reported hydraulic dispatches")
    return {
        "upload_validation": "PASS" if require_upload_validation else "not_requested_for_video",
        "hydraulic_dispatches": [row["hydraulic_dispatches"] for row in captures],
        "capture_frame_indices": [row["frame_index"] for row in captures],
        "visual_delta_s": [row["visual_delta_s"] for row in captures],
        "presentation_log_values": [row["presentation"] for row in captures],
        "capture_log_validated": expected_asset is not None,
    }


def run_logged(command: list[str], log_root: Path, label: str, timeout: int = 240,
               expected_asset: dict | None = None,
               expected_presentation: str | None = None) -> tuple[subprocess.CompletedProcess[str], dict]:
    stdout_path, stderr_path = log_root / f"{label}.stdout.txt", log_root / f"{label}.stderr.txt"
    if stdout_path.exists() or stderr_path.exists() or stdout_path.is_symlink() or stderr_path.is_symlink():
        raise FileExistsError(f"command logs already exist for {label}")
    started = time.monotonic()
    if log_root.is_symlink():
        raise ValueError(f"command log directory may not be a symlink: {log_root}")
    log_root.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=timeout, check=False)
    wall_s = time.monotonic() - started
    write_text_exclusive(stdout_path, result.stdout)
    write_text_exclusive(stderr_path, result.stderr)
    if result.returncode != 0:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(command)}\n{result.stderr[-4000:]}")
    parsed = verify_capture_log(result.stdout + "\n" + result.stderr,
                                "--fluid25d-recording-gpu-validation" in command,
                                expected_asset, expected_presentation)
    receipt = {
        "command": command,
        "exit_code": result.returncode,
        "wall_s": wall_s,
        "stdout_path": str(stdout_path.relative_to(log_root.parent)),
        "stderr_path": str(stderr_path.relative_to(log_root.parent)),
        **parsed,
    }
    return result, receipt


def app_command(asset: dict, output: Path, presentation: str | None, markers: bool,
                profile_prefix: Path | None = None) -> list[str]:
    source = INPUT_ROOT / "recordings" / asset["case"] / "recording.json"
    args = [
        "rtk", "proxy", str(APP), "--headless", "--width", str(asset["width"]), "--height", str(asset["height"]),
        "--fluid25d-recording", str(source), "--fluid25d-recording-time-seconds",
        str(asset.get("requested_time_s", asset.get("requested_start_time_s"))),
    ]
    if asset["kind"] in ("still", "video"):
        args.extend(("--fluid25d-recording-camera", asset["camera"]))
    if presentation is not None:
        args.extend(("--fluid25d-native-presentation", presentation))
    if markers:
        args.append("--fluid25d-motion-markers")
    if asset["kind"] in ("still", "diagnostic"):
        args.append("--fluid25d-recording-gpu-validation")
        if asset["kind"] == "diagnostic":
            args.extend(("--fluid25d-view", "diagnostics", "--debug-view", asset["view"]))
    else:
        args.extend((
            "--capture", "video", "--frames", str(asset["frame_count"]), "--fps", str(asset["fps"]),
            "--fluid25d-recording-frame-interval-seconds", str(asset["render_interval_s"]),
        ))
    if profile_prefix is not None:
        args.extend(("--profile-output", str(profile_prefix), "--profile-warmup-frames",
                     str(asset.get("profile_warmup_frames", 0))))
    args.extend(("--output", str(output)))
    return args


def find_font() -> Path:
    candidates = (
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
        Path("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"),
        Path("/usr/share/fonts/TTF/DejaVuSans.ttf"),
        Path("/usr/share/fonts/TTF/OpenSans-Regular.ttf"),
        Path("/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf"),
    )
    for path in candidates:
        if path.is_file():
            return path
    raise FileNotFoundError("no known TrueType font is available for review video labels")


def scheduled_rain_label(case: str, saved_time_s: int) -> str:
    manifest = json.loads((INPUT_ROOT / "recordings" / case / "recording.json").read_text())
    history = manifest["protocol"]["rainfall_history"]
    rate = float(history[-1][1])
    phase = "On" if rate > 0 else "Off"
    for (a, ra), (b, rb) in zip(history, history[1:]):
        if a <= saved_time_s < b:
            slope = (rb - ra) / (b - a)
            rate = ra + slope * (saved_time_s - a)
            phase = "Off" if rate == 0 else "Tapering" if rb < ra else "On"
            break
    mm_h = rate * 3_600_000.0
    amount = f"{mm_h:.3g} mm/h scheduled"
    if phase == "Off":
        return f"Scheduled rain off at saved state {saved_time_s}s; {amount}"
    return f"Scheduled rain {phase.lower()} at {amount}"


def ffprobe_video(path: Path, ignore_editlist: bool = True) -> dict:
    args = ["ffprobe", "-v", "error"]
    if ignore_editlist:
        args.extend(("-ignore_editlist", "1"))
    args.extend(("-count_frames",
                 "-show_entries", "format=duration:stream=width,height,r_frame_rate,nb_frames,nb_read_frames",
                 "-of", "json", str(path)))
    result = rtk_output(*args, timeout=60)
    if result.returncode != 0:
        raise RuntimeError(f"ffprobe failed for {path}: {result.stderr}")
    return json.loads(result.stdout)


def caption_video(raw_path: Path, output_path: Path, asset: dict, log_root: Path) -> dict:
    if output_path.exists() or output_path.is_symlink():
        raise FileExistsError(f"derived review video already exists: {output_path}")
    start = asset["requested_start_time_s"]
    interval = asset["render_interval_s"]
    fps = asset["fps"]
    requested_expr = f"{start}+n*{interval}"
    saved_expr = f"floor(({requested_expr})/{SAVED_INTERVAL_S})*{SAVED_INTERVAL_S}"
    line_one = (
        "RECORDED | requested %{eif\\:" + requested_expr + "\\:d}s"
        " | saved field %{eif\\:" + saved_expr + "\\:d}s"
    )
    saved_start = (start // SAVED_INTERVAL_S) * SAVED_INTERVAL_S
    line_two = scheduled_rain_label(asset["case"], saved_start)
    if asset.get("presentation_cues"):
        line_two += " | approximate cues, not native particles"
    font = find_font()
    filters = [
        f"setpts=N/({fps}*TB)",
        "drawbox=x=0:y=0:w=iw:h=58:color=black@0.78:t=fill",
        f"drawtext=fontfile={font}:fontcolor=white:fontsize=16:x=12:y=6:text='{line_one}'",
        f"drawtext=fontfile={font}:fontcolor=white:fontsize=15:x=12:y=33:text='{line_two}'",
    ]
    command = [
        "rtk", "proxy", "ffmpeg", "-nostdin", "-y", "-v", "error", "-ignore_editlist", "1",
        "-i", str(raw_path), "-vf", ",".join(filters), "-an", "-c:v", "libx264",
        "-preset", "veryfast", "-crf", "23", "-pix_fmt", "yuv420p", "-r", str(fps),
        "-frames:v", str(asset["frame_count"]), str(output_path),
    ]
    _, receipt = run_logged(command, log_root, f"caption-{asset['id']}", timeout=240)
    probe = ffprobe_video(output_path)
    checked = validate_probe(probe, asset["frame_count"], fps)
    browser_probe = ffprobe_video(output_path, ignore_editlist=False)
    browser_checked = validate_probe(browser_probe, asset["frame_count"], fps)
    return {
        "path": str(output_path.relative_to(log_root.parent)),
        "sha256": sha256_file(output_path),
        "bytes": output_path.stat().st_size,
        "caption_command": receipt,
        "probe": checked,
        "browser_playback_probe": browser_checked,
        "requested_time_label": "frame-relative requested render time in seconds",
        "saved_field_time_label": "last 60-second saved native state held at requested time",
        "rain_label": line_two,
    }


def capture_asset(asset: dict, phase_root: Path, presentation: str | None, markers: bool) -> dict:
    media_root, log_root = phase_root / "media", phase_root / "logs"
    media_root.mkdir(exist_ok=True)
    log_root.mkdir(exist_ok=True)
    extension = ".png" if asset["kind"] in ("still", "diagnostic") else "-raw.mp4"
    output = media_root / (asset["id"] + extension)
    if output.exists() or output.is_symlink():
        raise FileExistsError(f"capture output already exists: {output}")
    command = app_command(asset, output, presentation, markers)
    _, receipt = run_logged(command, log_root, asset["id"],
                            timeout=600 if asset["kind"] == "video" else 240,
                            expected_asset=asset, expected_presentation=presentation)
    record = {**asset, "path": str(output.relative_to(phase_root)), "sha256": sha256_file(output),
              "bytes": output.stat().st_size, "render_command": receipt}
    if asset["kind"] in ("still", "diagnostic"):
        if png_dimensions(output) != (asset["width"], asset["height"]):
            raise ValueError(f"capture dimensions mismatch: {output}")
    else:
        raw_probe = ffprobe_video(output)
        record["raw_probe"] = validate_probe(raw_probe, asset["frame_count"], asset["fps"])
        review_path = media_root / f"{asset['id']}-review.mp4"
        record["review_video"] = caption_video(output, review_path, asset, log_root)
    return record


def profile_summary(prefix: Path, warmup_frames: int, expected_samples: int) -> dict:
    frames_path = prefix.with_name(prefix.name + ".frames.csv")
    passes_path = prefix.with_name(prefix.name + ".passes.csv")
    if not frames_path.is_file():
        raise ValueError(f"profile frame timings are missing: {frames_path}")
    with frames_path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames or not {"frame_index", "delta_ms"}.issubset(reader.fieldnames):
            raise ValueError(f"unexpected profile frame header: {frames_path}")
        rows = list(reader)
    if passes_path.is_file() is False:
        raise ValueError(f"GPU pass timings are missing: {passes_path}")
    if len(rows) > 120 or len(rows) < expected_samples:
        raise ValueError(f"profile frame row count is outside its 120-frame bound: {frames_path}")
    frame_indices = {int(row["frame_index"]) for row in rows}
    if any(not math.isfinite(float(row["delta_ms"])) or float(row["delta_ms"]) < 0 for row in rows):
        raise ValueError(f"invalid frame scheduler timing in {frames_path}")
    scope = "fluid_25d native presentation total"
    values = []
    scope_frames = set()
    with passes_path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames or not {"frame_index", "kind", "label", "duration_ms"}.issubset(reader.fieldnames):
            raise ValueError(f"unexpected profile pass header: {passes_path}")
        for row in reader:
            if row["kind"] != "gpu" or row["label"] != scope:
                continue
            frame = int(row["frame_index"])
            if frame < warmup_frames:
                continue
            value = float(row["duration_ms"])
            if not math.isfinite(value) or value <= 0:
                raise ValueError(f"invalid GPU presentation duration at frame {frame}: {value}")
            values.append(value)
            scope_frames.add(frame)
    if len(values) != expected_samples or len(scope_frames) != expected_samples:
        raise ValueError(
            f"expected {expected_samples} measured GPU spans for {scope}; got {len(values)}"
        )
    if not scope_frames.issubset(frame_indices):
        raise ValueError("GPU presentation timing rows reference unprofiled frames")
    sorted_values = sorted(values)
    p95 = sorted_values[min(len(sorted_values) - 1, math.ceil(len(sorted_values) * 0.95) - 1)]
    return {
        "capture_frame_count": len(rows),
        "capture_frame_indices": sorted(frame_indices),
        "frames_csv_delta_ms": [float(row["delta_ms"]) for row in rows],
        "frames_csv_delta_interpretation": "headless capture cadence, not rendering duration",
        "measured_scope": scope,
        "measured_gpu_span_count": len(values),
        "measured_gpu_frame_indices": sorted(scope_frames),
        "gpu_span_ms": values,
        "gpu_median_ms": statistics.median(values),
        "gpu_p95_ms": p95,
        "frames_csv": frames_path.name,
        "frames_csv_sha256": sha256_file(frames_path),
        "passes_csv": passes_path.name if passes_path.is_file() else None,
        "passes_csv_sha256": sha256_file(passes_path) if passes_path.is_file() else None,
    }


def capture_profiles(phase_root: Path, variant: str) -> list[dict]:
    profile_root = phase_root / "profiles"
    profile_root.mkdir(exist_ok=True)
    log_root = phase_root / "logs"
    log_root.mkdir(exist_ok=True)
    results = []
    for case in CASES:
        asset = video_asset("profile-mature-runoff", case, "runoff", 6000, 120, 30, 5)
        asset["profile_warmup_frames"] = 12
        image = profile_root / f"{case}-runoff-profile.mp4"
        prefix = profile_root / f"{case}-runoff-profile"
        if image.exists() or prefix.with_name(prefix.name + ".frames.csv").exists():
            raise FileExistsError(f"profile output already exists: {prefix}")
        command = app_command(asset, image, variant.removesuffix("-markers"), variant.endswith("-markers"), prefix)
        _, receipt = run_logged(command, log_root, f"profile-{case}", timeout=240,
                                expected_asset=asset, expected_presentation=variant.removesuffix("-markers"))
        capture_probe = validate_probe(ffprobe_video(image), 120, 30)
        summary = profile_summary(prefix, warmup_frames=12, expected_samples=108)
        artifacts = {
            str(path.relative_to(phase_root)): sha256_file(path)
            for path in sorted(profile_root.glob(prefix.name + ".*")) if path.is_file()
        }
        artifacts[str(image.relative_to(phase_root))] = sha256_file(image)
        results.append({
            "case": case,
            "camera": "runoff",
            "requested_start_time_s": 6000,
            "requested_end_time_s": 6595,
            "saved_field_interval_s": SAVED_INTERVAL_S,
            "render_interval_s": 5,
            "frame_count": 120,
            "fps": 30,
            "presentation": variant.removesuffix("-markers"),
            "motion_markers": variant.endswith("-markers"),
            "warmup_frames_requested": 12,
            "profile_video_path": str(image.relative_to(phase_root)),
            "profile_video_sha256": sha256_file(image),
            "capture_probe": capture_probe,
            "profile": summary,
            "artifact_sha256": artifacts,
            "render_command": receipt,
        })
    return results


def media_record(path: Path, phase_root: Path, kind: str) -> dict:
    return {
        "path": str(path.relative_to(phase_root)),
        "kind": kind,
        "sha256": sha256_file(path),
        "bytes": path.stat().st_size,
    }


def historical_expected_names() -> set[str]:
    names = set()
    for asset in historical_capture_plan():
        names.add(asset["id"] + (".png" if asset["kind"] != "video" else "-raw.mp4"))
    return names


def adopt_historical_baseline(out: Path) -> dict:
    phase_root = out / "baseline"
    media_root = phase_root / "media"
    manifest_path = phase_root / "manifest.json"
    if not phase_root.is_dir() or not media_root.is_dir():
        raise ValueError("historical baseline media directory is missing")
    if manifest_path.exists() or manifest_path.is_symlink():
        raise FileExistsError("historical baseline has already been frozen")
    actual_names = {path.name for path in media_root.iterdir() if path.is_file() and not path.is_symlink()}
    expected = historical_expected_names()
    if actual_names != expected:
        raise ValueError(f"historical baseline media inventory mismatch: missing={sorted(expected-actual_names)}, extra={sorted(actual_names-expected)}")
    inputs = frozen_input_identity()
    assets = []
    by_id = {asset["id"]: asset for asset in historical_capture_plan()}
    for name in sorted(expected):
        path = media_root / name
        if path.suffix == ".png":
            if png_dimensions(path) != (WIDTH, HEIGHT):
                raise ValueError(f"historical baseline PNG has unexpected dimensions: {path}")
            asset_id = name.removesuffix(".png")
            asset = by_id[asset_id]
            assets.append({**asset, **media_record(path, phase_root, asset["kind"]),
                           "gpu_validation_signal_observed": True,
                           "capture_logs_retained": False})
        else:
            raw_id = name.removesuffix("-raw.mp4")
            asset = by_id[raw_id]
            probe = validate_probe(ffprobe_video(path), asset["frame_count"], asset["fps"])
            review_path = media_root / f"{raw_id}-review.mp4"
            derived = caption_video(path, review_path, asset, phase_root / "logs")
            assets.append({**asset, **media_record(path, phase_root, "raw_video"),
                           "raw_probe": probe, "review_video": derived,
                           "capture_logs_retained": False})
    manifest = {
        "schema": "cubey.fluid25d.presentation_capture.v1",
        "phase": "historical_pre_rebuild_baseline",
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "input_identity": inputs,
        "runtime_identity": {
            "executable": str(APP.relative_to(ROOT)),
            "executable_sha256": HISTORICAL_BINARY_SHA256,
            "compiled_shaders": None,
            "shader_hash_limitation": "pre-rebuild SPIR-V hashes were not captured; current build artifacts were replaced before this manifest was assembled",
            "head": HISTORICAL_HEAD,
            "worktree_note": "shared worktree had concurrent uncommitted renderer edits; exact dirty source snapshot was not retained",
            "capture_order_confirmed_before_renderer_rebuild": True,
        },
        "capture_plan": historical_capture_plan(),
        "assets": assets,
        "gpu_validation": {
            "passed_still_and_diagnostic_count": 24,
            "validation_flag": "--fluid25d-recording-gpu-validation",
            "stdout_logs_retained": False,
            "scope": "exact recorded-field upload and presentation immutability, not hydraulic validation",
        },
        "time_contract": {
            "saved_native_interval_s": SAVED_INTERVAL_S,
            "requested_and_saved_times_separate": True,
            "fields_between_saves": "held at the latest saved native state",
            "physical_field_interpolation": False,
            "motion_cues": "approximate local render cues; not live native particles or conserved dye",
        },
        "historical_baseline_scope": "short overview development, mature runoff, and immediate recession excerpts; current authoritative review uses extended story windows",
    }
    write_json_exclusive(manifest_path, manifest)
    return manifest


def write_candidate_environment(out: Path, current: dict | None = None) -> dict:
    candidate_root = out / "candidate"
    candidate_root.mkdir(exist_ok=True)
    path = candidate_root / "environment.json"
    current = current or runtime_identity()
    if path.exists() or path.is_symlink():
        frozen = json.loads(path.read_text())
        assert_same_runtime(frozen, current, "since the first candidate phase")
        return frozen
    write_json_exclusive(path, current)
    return current


def capture_current_baseline(out: Path) -> dict:
    historical_path = out / "baseline" / "manifest.json"
    if not historical_path.exists():
        adopt_historical_baseline(out)
    historical = validate_phase_manifest(historical_path)
    if historical.get("phase") != "historical_pre_rebuild_baseline":
        raise ValueError("historical pre-rebuild media manifest has an unexpected phase")
    phase_root = out / "current-baseline"
    before = runtime_identity()
    if before["executable_sha256"] != EXPECTED_CURRENT_BINARY_SHA256:
        raise ValueError(
            "current-binary baseline requires the stable executable SHA256 "
            f"{EXPECTED_CURRENT_BINARY_SHA256}, got {before['executable_sha256']}"
        )
    candidate_root = out / "candidate"
    if candidate_root.is_symlink():
        raise ValueError("candidate environment may not be a symlink")
    if candidate_root.exists() and any(candidate_root.iterdir()):
        raise FileExistsError("candidate output exists before the current-binary baseline")
    reserve_directory(phase_root)
    candidate_root.mkdir(exist_ok=True)
    environment_path = candidate_root / "environment.json"
    write_json_exclusive(environment_path, before)
    plan = current_capture_plan("original")
    assets = [capture_asset(asset, phase_root, "original", False) for asset in plan]
    profiles = capture_profiles(phase_root, "original")
    after = runtime_identity()
    assert_same_runtime(before, after, "during current-binary original baseline capture")
    manifest = {
        "schema": "cubey.fluid25d.presentation_capture.v1",
        "phase": "current_binary_baseline",
        "variant": "original",
        "presentation": "original",
        "motion_markers": False,
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "runtime_identity": before,
        "runtime_identity_after": after,
        "historical_baseline_manifest_sha256": sha256_file(historical_path),
        "capture_plan": plan,
        "assets": assets,
        "profiles": profiles,
        "profile_plan": profile_plan("original"),
        "recorded_playback_contract": {
            "native_solver_launched": False,
            "recordings_immutable": True,
            "saved_field_interval_s": SAVED_INTERVAL_S,
            "requested_render_clock_is_not_saved_field_time": True,
            "physical_field_interpolation": False,
            "presentation_cues_are_approximate": True,
        },
    }
    write_json_exclusive(phase_root / "manifest.json", manifest)
    return manifest


def capture_candidate_variant(out: Path, variant: str) -> dict:
    if variant not in CANDIDATE_VARIANTS:
        raise ValueError(f"candidate variant must be one of {', '.join(CANDIDATE_VARIANTS)}")
    baseline = validate_phase_manifest(out / "current-baseline" / "manifest.json")
    if baseline.get("phase") != "current_binary_baseline":
        raise ValueError("the same-binary original baseline must be frozen before candidates")
    frozen_runtime = write_candidate_environment(out)
    phase_root = out / "candidate" / variant
    reserve_directory(phase_root)
    before = runtime_identity()
    assert_same_runtime(frozen_runtime, before, "before candidate capture")
    markers = variant.endswith("-markers")
    presentation = variant.removesuffix("-markers")
    asset_plan = current_capture_plan(variant)
    assets = [capture_asset(asset, phase_root, presentation, markers) for asset in asset_plan]
    profiles = capture_profiles(phase_root, variant)
    after = runtime_identity()
    assert_same_runtime(before, after, "during candidate capture")
    assert_same_runtime(frozen_runtime, after, "after candidate capture")
    manifest = {
        "schema": "cubey.fluid25d.presentation_capture.v1",
        "phase": "candidate",
        "variant": variant,
        "presentation": presentation,
        "motion_markers": markers,
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "runtime_identity": frozen_runtime,
        "runtime_identity_after": after,
        "input_baseline_sha256": sha256_file(out / "current-baseline" / "manifest.json"),
        "capture_plan": asset_plan,
        "assets": assets,
        "profiles": profiles,
        "profile_plan": profile_plan(variant),
        "recorded_playback_contract": {
            "native_solver_launched": False,
            "recordings_immutable": True,
            "saved_field_interval_s": SAVED_INTERVAL_S,
            "requested_render_clock_is_not_saved_field_time": True,
            "physical_field_interpolation": False,
            "presentation_cues_are_approximate": True,
        },
    }
    write_json_exclusive(phase_root / "manifest.json", manifest)
    return manifest


def _image_link(asset: dict, phase_dir: Path, review_root: Path, label: str | None = None) -> str:
    media_path = Path(os.path.relpath(phase_dir / asset["path"], review_root)).as_posix()
    caption = label or f"{asset.get('case')} · {asset.get('camera', asset.get('view'))} · saved {asset.get('saved_field_time_s')}s"
    return (f"<figure><a href='{html.escape(media_path)}'><img loading='lazy' src='{html.escape(media_path)}' alt='{html.escape(caption)}'></a>"
            f"<figcaption>{html.escape(caption)}</figcaption></figure>")


def _video_markup(asset: dict, phase_dir: Path, review_root: Path) -> str:
    review = asset.get("review_video")
    relative = review["path"] if review else asset["path"]
    media_path = Path(os.path.relpath(phase_dir / relative, review_root)).as_posix()
    start = asset.get("requested_start_time_s")
    end = asset.get("requested_end_time_s")
    case = asset.get("case", "")
    camera = asset.get("camera", "")
    info = f"{case} · {camera} · requested {start}–{end}s · saved fields held every {SAVED_INTERVAL_S}s"
    return (f"<figure><video controls preload='metadata' src='{html.escape(media_path)}'></video>"
            f"<figcaption>{html.escape(info)}</figcaption></figure>")


def ffmpeg_rgb_pixel_sha256(path: Path) -> str:
    result = rtk_output("ffmpeg", "-v", "error", "-i", str(path), "-frames:v", "1",
                        "-pix_fmt", "rgb24", "-f", "hash", "-hash", "sha256", "-",
                        timeout=60)
    if result.returncode != 0:
        raise RuntimeError(f"could not decode still pixels for {path}: {result.stderr}")
    match = re.search(r"SHA256=([0-9a-f]{64})", result.stdout)
    if not match:
        raise ValueError(f"ffmpeg produced no RGB SHA-256 for {path}")
    return match.group(1)


def ffmpeg_decoded_frame_hashes(path: Path) -> list[str]:
    result = rtk_output("ffmpeg", "-v", "error", "-ignore_editlist", "1", "-i", str(path),
                        "-f", "framemd5", "-", timeout=240)
    if result.returncode != 0:
        raise RuntimeError(f"could not decode video frames for {path}: {result.stderr}")
    hashes = [line.rsplit(",", 1)[-1].strip() for line in result.stdout.splitlines()
              if line and not line.startswith("#")]
    if not hashes:
        raise ValueError(f"ffmpeg produced no decoded frame hashes for {path}")
    return hashes


def compare_historical_original(out: Path, historical: dict, current_baseline: dict) -> dict:
    old_assets = {asset["id"]: asset for asset in historical["assets"]}
    current_assets = {asset["id"]: asset for asset in current_baseline["assets"]}
    stills = []
    for time_s in HISTORICAL_TIMES_S:
        for case in CASES:
            for camera in CAMERAS:
                asset_id = f"{case}-{camera}-{time_s}s"
                old_asset, new_asset = old_assets[asset_id], current_assets[asset_id]
                old_path = out / "baseline" / old_asset["path"]
                new_path = out / "current-baseline" / new_asset["path"]
                old_pixels = ffmpeg_rgb_pixel_sha256(old_path)
                new_pixels = ffmpeg_rgb_pixel_sha256(new_path)
                stills.append({
                    "id": asset_id,
                    "historical_png_sha256": old_asset["sha256"],
                    "current_png_sha256": new_asset["sha256"],
                    "encoded_file_identical": old_asset["sha256"] == new_asset["sha256"],
                    "historical_rgb_pixel_sha256": old_pixels,
                    "current_rgb_pixel_sha256": new_pixels,
                    "decoded_pixels_identical": old_pixels == new_pixels,
                })
    videos = []
    for case in CASES:
        asset_id = f"mature-runoff-{case}"
        old_asset, new_asset = old_assets[asset_id], current_assets[asset_id]
        old_path = out / "baseline" / old_asset["path"]
        new_path = out / "current-baseline" / new_asset["path"]
        old_hashes = ffmpeg_decoded_frame_hashes(old_path)
        new_hashes = ffmpeg_decoded_frame_hashes(new_path)
        matching = sum(a == b for a, b in zip(old_hashes, new_hashes))
        mismatches = [index for index, (a, b) in enumerate(zip(old_hashes, new_hashes)) if a != b]
        videos.append({
            "id": asset_id,
            "historical_frame_count": len(old_hashes),
            "current_frame_count": len(new_hashes),
            "historical_decoded_frame_hashes_sha256": hashlib.sha256("\n".join(old_hashes).encode()).hexdigest(),
            "current_decoded_frame_hashes_sha256": hashlib.sha256("\n".join(new_hashes).encode()).hexdigest(),
            "matching_frame_count": matching,
            "exact_decoded_frames_identical": len(old_hashes) == len(new_hashes) == 301 and matching == 301,
            "first_mismatch_indices": mismatches[:20],
        })
    return {
        "comparison_contract": "old retained pre-rebuild media vs current-binary original at exactly matched requested times, cameras, and mature-runoff frame plan; mismatch is a finding and no media is rewritten",
        "historical_stills": stills,
        "mature_runoff_decoded_videos": videos,
    }


def build_review(out: Path) -> dict:
    if (out / "review").exists() or (out / "review").is_symlink():
        raise FileExistsError("review phase already exists")
    frozen_runtime = json.loads((out / "candidate" / "environment.json").read_text())
    current = runtime_identity()
    assert_same_runtime(frozen_runtime, current, "before review generation")
    historical = validate_phase_manifest(out / "baseline" / "manifest.json")
    current_baseline = validate_phase_manifest(out / "current-baseline" / "manifest.json", frozen_runtime)
    candidates = {name: validate_phase_manifest(out / "candidate" / name / "manifest.json", frozen_runtime)
                  for name in CANDIDATE_VARIANTS}
    variants = {"original": current_baseline, **candidates}
    review_media_checks = {
        "historical": [
            {"id": asset["id"], "probe": asset["review_video"]["browser_playback_probe"]}
            for asset in historical["assets"] if asset.get("review_video")
        ],
        "current_original": [
            {"id": asset["id"], "probe": asset["review_video"]["browser_playback_probe"]}
            for asset in current_baseline["assets"] if asset.get("review_video")
        ],
        "candidates": {
            name: [
                {"id": asset["id"], "probe": asset["review_video"]["browser_playback_probe"]}
                for asset in candidates[name]["assets"] if asset.get("review_video")
            ]
            for name in CANDIDATE_VARIANTS
        },
    }
    preservation_comparison = compare_historical_original(out, historical, current_baseline)
    review_root = out / "review"
    reserve_directory(review_root)

    def phase_dir(kind: str, variant: str | None = None) -> Path:
        return out / kind if variant is None else out / kind / variant

    parts = [
        "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'>",
        "<title>Native rain presentation review</title>",
        "<style>body{font:16px system-ui;background:#17222b;color:#eef4f8;max-width:1500px;margin:auto;padding:24px}a{color:#8ed2ff}img,video{width:100%;height:auto;border-radius:6px}figure{margin:8px}figcaption{font-size:.9rem;line-height:1.4;color:#c1d0da}table{width:100%;border-collapse:collapse}td,th{vertical-align:top;padding:8px;border:1px solid #455661}section{margin:28px 0}pre{white-space:pre-wrap;background:#0e171c;padding:12px}button{margin:4px;padding:8px}</style>",
        "<h1>Native rain presentation review</h1>",
        "<p>This is <b>recorded playback</b> of the frozen 512×512 native depth and depth-integrated momentum fields. It does not run the solver. Both cases use saved states every 60 physical seconds. Requested render time and the saved field currently held are shown separately; no physical fields are interpolated. Rain labels report the frozen nominal schedule, not a measured applied-rain ledger. The moving dots/trails are approximate render cues, not native particles or conserved dye. This gallery does not establish a quiet lake.</p>",
        "<p>The shared scene receives a nominal 120 mm/h schedule through 7200s. The rain-off case tapers from 7200–7260s then schedules zero; the rain-on case continues 120 mm/h. Before 7200s, the two schedules match, so overview and mature-runoff pairs are expected to match; the meaningful scheduled-rain contrast is recession after 7260s. All cameras are observation views. Depth, momentum-derived flow/speed, and wet/dry images are raw-state diagnostic views.</p>",
        "<h2>Provenance and reading guide</h2><ul><li>Historical pre-rebuild captures retain the old executable hash, but their compiled SPIR-V hashes were not captured before the rebuild.</li><li>The current-binary <code>original</code>, <code>motion</code>, and <code>readable</code> runs share one verified executable, compiled SPIR-V tree and immutable input fingerprint. Marker-enabled clips are separate toggles.</li><li>The mature runoff clip requests a render time every 5s at 30fps (150× playback) while native fields remain held at 60s saves. Overview development and collection recession use exact 60s saved knots at 8fps.</li><li>The 30m grid still reads as stepped terrain and a dark triangular patch remains visible; those are unresolved rendering limitations.</li><li>Approximate cues are visual motion aids. They do not represent water parcels or prove a stable pool.</li></ul>",
    ]

    readable = {asset["id"]: asset for asset in variants["readable"]["assets"]}
    parts.append("<section><h2>Three readable story clips</h2><p>Start here: each pair holds the same recording, camera, dimensions and requested-time schedule for rain-off and rain-on. These use the readable render option; native field states remain held at saved 60-second knots.</p>")
    for label in ("development-overview", "mature-runoff", "recession-collection"):
        parts.append(f"<h3>{html.escape(label)}</h3><table><tr>")
        for case in CASES:
            asset = readable[f"{label}-{case}"]
            parts.append(f"<td>{_video_markup(asset, phase_dir('candidate','readable'), review_root)}</td>")
        parts.append("</tr></table>")
    parts.append("</section>")

    by_id = {asset["id"]: asset for asset in historical["assets"]}
    parts.append("<section><h2>Historical pre-rebuild baseline</h2><p>Pre-rebuild clips are short excerpts and remain unchanged: overview 0–900s and collection 7260–8160s. The authoritative story windows below use the extended current-binary captures.</p>")
    for label, camera, start in (("early-overview", "overview", 0), ("mature-runoff", "runoff", 4800), ("recession-collection", "collection", 7260)):
        parts.append(f"<h3>{html.escape(label)}</h3><table><tr>")
        for case in CASES:
            asset = by_id[f"{label}-{case}"]
            parts.append(f"<td>{_video_markup(asset, phase_dir('baseline'), review_root)}</td>")
        parts.append("</tr></table>")
    parts.append("<h3>Historical exact-state stills</h3>")
    for t in HISTORICAL_TIMES_S:
        parts.append(f"<h4>Saved state {t}s</h4><table>")
        for camera in CAMERAS:
            parts.append("<tr>")
            for case in CASES:
                asset = by_id[f"{case}-{camera}-{t}s"]
                parts.append(f"<td>{_image_link(asset, phase_dir('baseline'), review_root)}</td>")
            parts.append("</tr>")
        parts.append("</table>")
    parts.append("<h3>Historical raw depth, flow and wet/dry at saved 8160s</h3><table><tr>")
    for case in CASES:
        parts.append(f"<td><h4>{case}</h4><table>")
        for view in DIAGNOSTICS:
            asset = by_id[f"{case}-diagnostic-{view}-8160s"]
            parts.append(f"<tr><td>{_image_link(asset, phase_dir('baseline'), review_root)}</td></tr>")
        parts.append("</table></td>")
    parts.append("</tr></table></section>")

    original = {asset["id"]: asset for asset in variants["original"]["assets"]}
    parts.append("<section><h2>Current-binary original presentation</h2><p>This is the controlled post-rebuild reference for the presentation variants.</p>")
    for label in ("development-overview", "mature-runoff", "recession-collection"):
        parts.append(f"<h3>{html.escape(label)}</h3><table><tr>")
        for case in CASES:
            parts.append(f"<td>{_video_markup(original[f'{label}-{case}'], phase_dir('current-baseline'), review_root)}</td>")
        parts.append("</tr></table>")
    for t in CURRENT_STILL_TIMES_S:
        parts.append(f"<h3>Exact saved state {t}s · original cameras</h3><table>")
        for camera in CAMERAS:
            parts.append("<tr>")
            for case in CASES:
                asset = original[f"{case}-{camera}-{t}s"]
                parts.append(f"<td>{_image_link(asset, phase_dir('current-baseline'), review_root)}</td>")
            parts.append("</tr>")
        parts.append("</table>")
    parts.append("</section>")

    parts.append("<section><h2>Matched mature-motion A/B · runoff camera</h2><p>Each pair uses the same recording, requested interval, frame count, fps, camera and resolution. Compare the pre-change historical baseline with the post-rebuild original, then the motion option. Fields are held every 60s; the motion option advances only approximate presentation cues on requested render time.</p>")
    motion_video = {asset["id"]: asset for asset in variants["motion"]["assets"] if asset["kind"] == "video"}
    marker_video = {asset["id"]: asset for asset in variants["motion-markers"]["assets"] if asset["kind"] == "video"}
    for case in CASES:
        parts.append(f"<h3>{case} · 4800–6300s</h3><table><tr>")
        for title, source, phase in (("historical", by_id[f"mature-runoff-{case}"], phase_dir("baseline")),
                                     ("original", original[f"mature-runoff-{case}"], phase_dir("current-baseline")),
                                     ("motion", motion_video[f"mature-runoff-{case}"], phase_dir("candidate","motion")),
                                     ("motion + markers", marker_video[f"mature-runoff-{case}"], phase_dir("candidate","motion-markers"))):
            parts.append(f"<td><h4>{title}</h4>{_video_markup(source, phase, review_root)}</td>")
        parts.append("</tr></table>")
    parts.append("</section>")

    readable_markers = {asset["id"]: asset for asset in variants["readable-markers"]["assets"] if asset["kind"] == "video"}
    parts.append("<section><h2>Readable end-state and marker toggle</h2><p>The camera/material change is rendering-only. The separate readable + markers clips isolate the marker toggle at mature runoff.</p>")
    parts.append("<h3>Mature runoff · readable markers enabled</h3><table><tr>")
    for case in CASES:
        parts.append(f"<td>{_video_markup(readable_markers[f'mature-runoff-{case}'], phase_dir('candidate','readable-markers'), review_root)}</td>")
    parts.append("</tr></table>")
    parts.append("<h3>Readable end-state observations at saved 14400s</h3><table>")
    for camera in CAMERAS:
        parts.append("<tr>")
        for case in CASES:
            parts.append(f"<td>{_image_link(readable[f'{case}-{camera}-14400s'], phase_dir('candidate','readable'), review_root)}</td>")
        parts.append("</tr>")
    parts.append("</table></section>")

    parts.append("<section><h2>Bounded matched render timings</h2><p>Each presentation/marker variant profiled a 120-frame video at the same runoff camera, starting at saved 6000s, 30fps, 5 requested physical seconds per frame, and 960×540. Twelve warm-up frames were excluded; the table reports only the GPU timestamp scope for native presentation. Frame CSV delta is capture scheduling cadence, not rendering duration. Process wall time also includes startup and video export.</p><table><tr><th>Variant</th><th>Case</th><th>GPU median (ms)</th><th>GPU p95 (ms)</th><th>Measured spans</th><th>Process wall (s)</th></tr>")
    for variant in VARIANTS:
        for profile in variants[variant]["profiles"]:
            summary = profile["profile"]
            parts.append(f"<tr><td>{html.escape(variant)}</td><td>{html.escape(profile['case'])}</td><td>{summary['gpu_median_ms']:.3f}</td><td>{summary['gpu_p95_ms']:.3f}</td><td>{summary['measured_gpu_span_count']}</td><td>{profile['render_command']['wall_s']:.3f}</td></tr>")
    parts.append("</table></section>")
    parts.append("<section><h2>Provenance</h2><p>Historical, phase and timing metadata are machine-readable in <a href='review.json'>review.json</a>; each phase contains its own media manifest and command logs. The input payload fingerprints are checked before and after candidate capture. Human visual acceptance remains separate.</p></section>")

    report = {
        "schema": "cubey.fluid25d.presentation_review.v1",
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "runtime_identity": frozen_runtime,
        "runtime_identity_after_review": current,
        "historical_baseline_manifest_sha256": sha256_file(out / "baseline" / "manifest.json"),
        "current_original_baseline_manifest_sha256": sha256_file(out / "current-baseline" / "manifest.json"),
        "candidate_manifests": {name: sha256_file(out / "candidate" / name / "manifest.json") for name in CANDIDATE_VARIANTS},
        "normal_ffprobe_review_media_checks": review_media_checks,
        "pre_rebuild_vs_current_original_pixel_comparison": preservation_comparison,
        "input_identity": frozen_runtime["inputs"],
        "human_visual_acceptance": "separate/pending",
        "interpretation": {
            "playback": "recorded native-state fields; no solver launch",
            "rain": "nominal scheduled rate, not a measured applied-rain ledger",
            "fields_between_saves": "held at the latest saved native state; no physical interpolation",
            "presentation_cues": "approximate local render cues, not native particles or conserved dye",
            "lake_claim": "none; calm-lake criterion is not established by this review",
        },
    }
    write_json_exclusive(review_root / "review.json", report)
    write_text_exclusive(review_root / "index.html", "\n".join(parts) + "\n")
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("baseline", "candidate", "review"), required=True)
    parser.add_argument("--variant", choices=CANDIDATE_VARIANTS)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args(argv)
    out = checked_output_root(args.out)
    if args.phase == "baseline":
        if args.variant is not None:
            parser.error("--variant applies only to --phase candidate")
        manifest = capture_current_baseline(out)
        print(json.dumps({"phase": manifest["phase"], "manifest": str(out / "current-baseline" / "manifest.json"),
                          "asset_count": len(manifest["assets"]), "profiles": len(manifest["profiles"])}, sort_keys=True))
    elif args.phase == "candidate":
        if args.variant is None:
            parser.error("candidate phase requires --variant")
        manifest = capture_candidate_variant(out, args.variant)
        print(json.dumps({"phase": "candidate", "variant": args.variant,
                          "manifest": str(out / "candidate" / args.variant / "manifest.json"),
                          "asset_count": len(manifest["assets"]), "profiles": len(manifest["profiles"])}, sort_keys=True))
    else:
        if args.variant is not None:
            parser.error("review phase does not accept --variant")
        report = build_review(out)
        print(json.dumps({"phase": "review", "index": str(out / "review" / "index.html"),
                          "review_manifest": str(out / "review" / "review.json"),
                          "candidate_variants": len(report["candidate_manifests"])}, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"run_native_presentation_v1: {exc}", file=sys.stderr)
        raise
