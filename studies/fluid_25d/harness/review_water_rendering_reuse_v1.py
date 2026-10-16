#!/usr/bin/env python3
"""Rendering-only lake/stream review on immutable existing recordings.

Never invokes a solver. Uses the established capture/timeline/profile helpers.
Each phase is exclusive; interrupted evidence is retained, never overwritten.
"""
from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

import run_native_presentation_v1 as ref

ROOT = ref.ROOT
RECORDINGS = {
    "lake": ROOT / "outputs/fluid/seeded-lake-v1-20261008-a1/recordings/lake-hold/recording.json",
    "rain512": ROOT / "outputs/fluid/rain-power2-v1-20261008-a2/cases/rain512/recording/recording.json",
    "rain1024": ROOT / "outputs/fluid/rain-power2-v1-20261008-a2/cases/rain1024/recording/recording.json",
}


def git_output(*args):
    result = ref.rtk_output("git", *args, timeout=60)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return result.stdout


def source_identity():
    paths = git_output("ls-files", "-z", "--cached", "--others", "--exclude-standard").split("\0")
    return {
        "head": git_output("rev-parse", "HEAD").strip(),
        "status": git_output("status", "--short"),
        "files": {p: ref.sha256_file(ROOT / p) if (ROOT / p).is_file() else None
                  for p in sorted(set(paths)) if p},
    }


def runtime_identity():
    return {"executable": str(ref.APP.relative_to(ROOT)),
            "executable_sha256": ref.sha256_file(ref.APP),
            "compiled_shaders": ref.shader_identity()}


def input_identity():
    result = {}
    for name, path in RECORDINGS.items():
        data = json.loads(path.read_text())
        pins = {"recording.json": ref.sha256_file(path)}
        for item in [data["bed"], data["source_bed"], *data["frames"]]:
            relative = Path(item["path"])
            full = (path.parent / relative).resolve()
            if relative.is_absolute() or not full.is_relative_to(path.parent.resolve()):
                raise ValueError("recording input escapes its recording directory")
            digest = ref.sha256_file(full)
            if digest != item["sha256"]:
                raise ValueError(f"recording payload changed: {name}/{relative}")
            pins[str(relative)] = digest
        result[name] = {"manifest": str(path.relative_to(ROOT)), "files": pins,
                        "saved_times_s": [f["time_s"] for f in data["frames"]],
                        "grid": data["grid"]}
    return result


def preflight(out):
    target = out / "starting-worktree"
    ref.reserve_directory(target)
    identity = source_identity()
    dirty = set(git_output("diff", "--name-only", "-z", "HEAD").split("\0"))
    dirty.update(git_output("ls-files", "-z", "--others", "--exclude-standard").split("\0"))
    for path in sorted(dirty - {""}):
        source = ROOT / path
        if source.is_file():
            dest = target / "files" / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, dest)
    ref.write_json_exclusive(target / "source.json", identity)
    ref.write_text_exclusive(target / "tracked.patch", git_output("diff", "HEAD"))
    ref.write_json_exclusive(target / "runtime.json", runtime_identity())
    ref.write_json_exclusive(target / "inputs.json", input_identity())
    print("Preflight: current dirty work and all recording payload hashes preserved", flush=True)


def cadence(case):
    frames = json.loads(RECORDINGS[case].read_text())["frames"]
    intervals = {b["time_s"] - a["time_s"] for a, b in zip(frames, frames[1:])}
    if len(intervals) != 1:
        raise ValueError("review requires uniform saved-frame cadence")
    return intervals.pop()


def tuning_identity(extra):
    if "--fluid25d-scenic-tuning" not in extra:
        return None
    path = Path(extra[extra.index("--fluid25d-scenic-tuning") + 1]).resolve()
    return {"path": str(path), "sha256": ref.sha256_file(path),
            "document": json.loads(path.read_text())}


def still_plan():
    return [
        ("lake-collection", "lake", "collection", 1800, []),
        ("lake-lower", "lake", "collection", 1800,
         ["--fluid25d-native-camera-pitch", "-0.40"]),
        ("lake-reverse", "lake", "collection", 1800,
         ["--fluid25d-native-camera-yaw", "2.62"]),
        ("rain512-collection", "rain512", "collection", 21150, []),
        ("rain512-overview", "rain512", "overview", 21150, []),
        ("rain512-streams", "rain512", "runoff", 6750, []),
        ("rain1024-collection", "rain1024", "collection", 17775, []),
        ("rain1024-overview", "rain1024", "overview", 17775, []),
    ]


def asset(case, camera, time_s, frames=1, fps=15, interval_s=1):
    result = ref.still_asset(case, camera, time_s) if frames == 1 else ref.video_asset(
        "surface-motion", case, camera, time_s, frames, fps, interval_s)
    result.update(width=1280, height=720, saved_field_interval_s=cadence(case))
    if frames > 1:
        result["timeline"] = ref.timeline(time_s, frames, interval_s, cadence(case))
    return result


def capture_one(phase, name, request, extra, profile=False):
    suffix = "mp4" if request["kind"] == "video" else "png"
    image = phase / "media" / f"{name}.{suffix}"
    image.parent.mkdir(exist_ok=True)
    if image.exists():
        raise FileExistsError(image)
    prefix = phase / "profiles" / name if profile else None
    if prefix:
        prefix.parent.mkdir(exist_ok=True)
    command = ref.app_command(request, image, "scenic", False, prefix)
    command[command.index("--fluid25d-recording") + 1] = str(RECORDINGS[request["case"]])
    command += ["--fluid25d-scenic-material", "macro", *extra]
    _, receipt = ref.run_logged(command, phase / "logs", name, timeout=300,
                               expected_asset=request, expected_presentation="scenic")
    row = {"name": name, "path": str(image.relative_to(phase)), "asset": request,
           "sha256": ref.sha256_file(image), "receipt": receipt}
    if request["kind"] == "video":
        row["probe"] = ref.validate_probe(ref.ffprobe_video(image), request["frame_count"],
                                          request["fps"], request["width"], request["height"])
        # Remux away the capture edit-list for ordinary browser playback.
        browser = image.with_name(image.stem + "-browser.mp4")
        _, remux = ref.run_logged(
            ["rtk", "proxy", "ffmpeg", "-nostdin", "-v", "error", "-ignore_editlist", "1",
             "-i", str(image), "-c", "copy", "-movflags", "+faststart", str(browser)],
            phase / "logs", name + "-browser")
        row["browser"] = {"path": str(browser.relative_to(phase)),
                          "sha256": ref.sha256_file(browser), "receipt": remux,
                          "probe": ref.validate_probe(ref.ffprobe_video(browser, False),
                                                      request["frame_count"], request["fps"],
                                                      request["width"], request["height"])}
    if profile:
        row["profile"] = ref.profile_summary(prefix, 12, 108)
    print("Captured " + phase.name + "/" + name, flush=True)
    return row


def capture(out, phase_name, extra, selection, repeats, cases):
    phase = out / phase_name
    ref.reserve_directory(phase)
    source, runtime, inputs = source_identity(), runtime_identity(), input_identity()
    tuning = tuning_identity(extra)
    if tuning:
        ref.write_json_exclusive(phase / "tuning.json", tuning)
    rows = []
    if selection in ("all", "stills"):
        for name, case, camera, t, camera_args in still_plan():
            if case in cases:
                rows.append(capture_one(phase, name, asset(case, camera, t), extra + camera_args))
    if selection in ("all", "motion"):
        if "lake" in cases:
            rows.append(capture_one(phase, "lake-motion", asset("lake", "collection", 1500, 180), extra))
        if "rain512" in cases:
            rows.append(capture_one(phase, "rain512-camera-motion",
                                asset("rain512", "collection", 20925, 180),
                                extra + ["--fluid25d-native-camera-sweep", "0.6",
                                         "--fluid25d-native-camera-pitch", "-0.40"]))
    if selection in ("all", "profiles"):
        for batch in range(repeats):
            for width, height in ((1280, 720), (1920, 1080)):
                # All 120 frames remain inside the same 225 s saved interval.
                # The established timeline contract uses whole physical seconds.
                request = asset("rain512", "collection", 20925, 120, 30, 1)
                request.update(width=width, height=height, profile_warmup_frames=12)
                rows.append(capture_one(phase, f"profile-{height}p-{batch}", request, extra, True))
    if inputs != input_identity() or runtime != runtime_identity():
        raise ValueError("runtime or numerical inputs changed during this capture phase")
    if source != source_identity():
        raise ValueError("source changed during this capture phase")
    if tuning != tuning_identity(extra):
        raise ValueError("material tuning changed during this capture phase")
    manifest = {"schema": "cubey.fluid25d.water-rendering-reuse.v1", "phase": phase_name,
                "source": source, "runtime": runtime, "inputs": inputs,
                "extra_args": extra, "tuning": tuning, "assets": rows,
                "hydraulic_execution": "none; existing recordings only",
                "human_visual_acceptance": "deferred"}
    ref.write_json_exclusive(phase / "manifest.json", manifest)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--preflight", action="store_true")
    parser.add_argument("--phase")
    parser.add_argument("--matrix", action="store_true", help="capture independent rendering ablations")
    parser.add_argument("--performance", action="store_true", help="rotate same-runtime 720p/1080p GPU comparisons")
    parser.add_argument("--selection", choices=("all", "stills", "motion", "profiles"), default="all")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--cases", nargs="+", choices=tuple(RECORDINGS), default=list(RECORDINGS))
    parser.add_argument("--extra", nargs=argparse.REMAINDER, default=[])
    args = parser.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to((ROOT / "outputs/fluid").resolve()) or out.is_symlink():
        parser.error("review output must be a non-symlink directory inside outputs/fluid")
    if not 1 <= args.repeats <= 3:
        parser.error("profile repeats must be between 1 and 3")
    if args.phase and (Path(args.phase).name != args.phase or args.phase in (".", "..")):
        parser.error("phase must be a single new directory name")
    out.mkdir(parents=True, exist_ok=True)
    if args.preflight:
        preflight(out)
    if args.phase:
        capture(out, args.phase, args.extra, args.selection, args.repeats, args.cases)
    if args.matrix:
        matrix = [
            ("controls-off", None, list(RECORDINGS), []),
            ("wet-only", "wet-normal.json", ["lake", "rain512"], []),
            ("detail-refined", "detail-v2.json", list(RECORDINGS), []),
            ("daylight-only", "daylight.json", ["lake", "rain512"], []),
            ("daylight-refined", "daylight-detail.json", list(RECORDINGS), []),
        ]
        for name, tuning, cases, extra in matrix:
            if tuning:
                extra += ["--fluid25d-scenic-tuning", str(out / tuning)]
            capture(out, name, extra, "stills", 1, cases)
    if args.performance:
        recipes = [("off", []),
            ("surface", ["--fluid25d-scenic-tuning", str(out / "surface-tuning.json")]),
            ("candidate", ["--fluid25d-scenic-tuning", str(out / "final-tuning.json")])]
        for batch in range(3):
            for name, extra in recipes[batch:] + recipes[:batch]:
                capture(out, f"final-perf-{name}-{batch}", extra, "profiles", 1, ["rain512"])
    if not args.preflight and not args.phase and not args.matrix and not args.performance:
        parser.error("select --preflight and/or --phase")


if __name__ == "__main__":
    main()
