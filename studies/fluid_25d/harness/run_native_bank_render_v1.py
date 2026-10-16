#!/usr/bin/env python3
"""Small staged bank-render review; frozen fields, never a hydraulic run."""
from __future__ import annotations

import argparse
import html
import json
from pathlib import Path

import run_native_presentation_v1 as reference


def review_plan() -> list[tuple[dict, bool]]:
    return [
        (reference.video_asset("junction", "rain-on", "runoff", 4800, 301, 30, 5), True),
        (reference.video_asset("recession", "rain-off", "collection", 7260, 120, 8, 60), False),
        (reference.video_asset("continued-rain", "rain-on", "collection", 7260, 120, 8, 60), False),
    ]


def identity() -> dict:
    result = reference.runtime_identity()
    source_root = reference.ROOT / "projects/fluid/sim/fluid_25d"
    paths = sorted(p for p in source_root.rglob("*") if p.is_file()
                   and p.suffix in (".cpp", ".h", ".glsl", ".vert", ".frag", ".comp"))
    paths += [Path(__file__), reference.ROOT / "projects/fluid/fluid_25d/fluid_25d_project_config.h"]
    result["fluid_source_sha256"] = {
        str(p.relative_to(reference.ROOT)): reference.sha256_file(p) for p in paths}
    return result


def capture(asset: dict, root: Path, markers: bool, extra: list[str]) -> dict:
    media, logs = root / "media", root / "logs"
    media.mkdir(exist_ok=True)
    logs.mkdir(exist_ok=True)
    video = asset["kind"] == "video"
    output = media / (asset["id"] + ("-raw.mp4" if video else ".png"))
    command = reference.app_command(asset, output, "readable", markers)
    command.extend(extra)
    _, receipt = reference.run_logged(command, logs, asset["id"], timeout=600 if video else 240,
                                      expected_asset=asset, expected_presentation="readable")
    record = {**asset, "motion_markers": markers, "render_command": receipt,
              "path": str(output.relative_to(root)), "sha256": reference.sha256_file(output)}
    if video:
        record["raw_probe"] = reference.validate_probe(reference.ffprobe_video(output),
                                                        asset["frame_count"], asset["fps"])
        record["review_video"] = reference.caption_video(
            output, media / f"{asset['id']}-review.mp4", asset, logs)
    elif reference.png_dimensions(output) != (asset["width"], asset["height"]):
        raise ValueError("unexpected diagnostic dimensions")
    return record


def profiles(root: Path) -> list[dict]:
    results = []
    for sampling in ("triangular", "bilinear"):
        for case in reference.CASES:
            asset = reference.video_asset("profile-" + sampling, case, "runoff", 6000, 120, 30, 5)
            asset["profile_warmup_frames"] = 12
            prefix = root / f"{sampling}-{case}"
            command = reference.app_command(asset, prefix.with_suffix(".mp4"), "readable", True, prefix)
            command += ["--fluid25d-native-water-sampling", sampling]
            _, receipt = reference.run_logged(command, root / "logs", f"{sampling}-{case}",
                                               timeout=600, expected_asset=asset,
                                               expected_presentation="readable")
            summary = reference.profile_summary(prefix, 12, 108)
            results.append({"case": case, "sampling": sampling, "command": receipt, **summary})
            if summary["gpu_p95_ms"] > 1.0:
                raise ValueError(f"presentation GPU p95 exceeds 1 ms: {sampling}/{case}")
    return results


def review(out: Path) -> None:
    root = out / "review"
    reference.reserve_directory(root)
    base = json.loads((out / "baseline/manifest.json").read_text())
    candidate = json.loads((out / "candidate-bilinear/manifest.json").read_text())
    if base["runtime_identity"]["inputs"] != candidate["runtime_identity"]["inputs"]:
        raise ValueError("before/after numerical inputs differ")
    videos = []
    for before, after in zip(base["assets"][:3], candidate["assets"][:3], strict=True):
        for key in ("id", "timeline", "camera", "case", "motion_markers", "width", "height"):
            if before[key] != after[key]:
                raise ValueError(f"unmatched before/after {key}")
        paths = []
        for phase, asset in (("baseline", before), ("candidate-bilinear", after)):
            clip = asset["review_video"]
            path = out / phase / clip["path"]
            if reference.sha256_file(path) != clip["sha256"]:
                raise ValueError("review input clip changed")
            paths.append(path)
        output = root / (before["id"] + "-comparison.mp4")
        font = reference.find_font()
        filters = (
            f"[0:v]drawtext=fontfile={font}:text='BEFORE - triangle depth':"
            "x=12:y=62:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.8[a];"
            f"[1:v]drawtext=fontfile={font}:text='AFTER - continuous depth':"
            "x=12:y=62:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.8[b];"
            "[a][b]hstack=inputs=2[v]"
        )
        _, receipt = reference.run_logged([
            "rtk", "proxy", "ffmpeg", "-nostdin", "-n", "-v", "error", "-i", str(paths[0]),
            "-i", str(paths[1]), "-filter_complex", filters, "-map", "[v]", "-an",
            "-c:v", "libx264", "-crf", "20", "-pix_fmt", "yuv420p", str(output),
        ], root / "logs", before["id"], timeout=240)
        probe = reference.validate_probe(reference.ffprobe_video(output, ignore_editlist=False),
                                          before["frame_count"], before["fps"], 1920, 540)
        poster = output.with_suffix(".png")
        poster_frame = min(200, before["frame_count"] - 1)
        reference.run_logged([
            "rtk", "proxy", "ffmpeg", "-nostdin", "-n", "-v", "error", "-i", str(output),
            "-vf", f"select=eq(n\\,{poster_frame})", "-frames:v", "1", str(poster),
        ], root / "logs", before["id"] + "-poster", timeout=60)
        videos.append({"id": before["id"], "path": output.name, "sha256": reference.sha256_file(output),
                       "probe": probe, "command": receipt, "poster": poster.name,
                       "poster_sha256": reference.sha256_file(poster), "poster_frame": poster_frame})
    sections = "\n".join(
        f"<h2>{html.escape(v['id'])}</h2><video controls loop preload='metadata' poster='{v['poster']}' src='{v['path']}'></video>"
        for v in videos)
    reference.write_text_exclusive(root / "index.html", "<!doctype html><meta charset='utf-8'>"
        "<title>Water/bank review</title><style>body{background:#16202a;color:#eee;"
        "font:16px system-ui;margin:24px}video{width:100%;max-width:1920px}</style>"
        "<h1>Water/bank rendering — three matched comparisons</h1>"
        "<p>Left: triangle depth. Right: continuous four-sample display depth. Same saved fields, "
        "camera and physical clock; no hydraulic run. Dots/trails are approximate render cues.</p>"
        "<p>Geometry and depth palette are unchanged. This softens interpolation artifacts; it "
        "does not add terrain detail or simulation resolution. Saved hydraulic fields advance "
        "every 60 seconds. Human visual acceptance is pending.</p>" + sections)
    reference.write_json_exclusive(root / "review.json", {
        "schema": "cubey.fluid25d.bank_render_review.v1", "videos": videos,
        "before_identity": base["runtime_identity"], "after_identity": candidate["runtime_identity"],
        "human_visual_acceptance": "pending",
    })


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("baseline", "diagnose", "candidate", "profile", "review"))
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--surface", default="triangular", choices=("triangular", "bilinear"))
    args = parser.parse_args()
    if args.out.is_symlink():
        raise ValueError("--out may not be a symlink")
    out = args.out.resolve()
    if out.parent != reference.OUTPUT_ROOT.resolve() or not out.name.startswith("bank-render-v1-"):
        raise ValueError("--out must be a fresh bank-render-v1-* leaf under outputs/fluid")
    if args.phase == "review":
        review(out)
        return
    root = out / (args.phase + ("-" + args.surface if args.phase == "candidate" else ""))
    reference.reserve_directory(root)
    before = identity()
    reference.write_json_exclusive(root / "runtime-before.json", before)
    assets = []
    extra = [] if args.surface == "triangular" else ["--fluid25d-native-water-sampling", args.surface]
    measured = profiles(root) if args.phase == "profile" else []
    if args.phase == "diagnose":
        for camera, time_s in (("runoff", 6000), ("collection", 14400), ("overview", 6000)):
            for mode in ("shaded", "solid", "unlit", "normals", "wireframe", "wet-mask"):
                asset = reference.still_asset("rain-off", camera, time_s)
                asset["id"] += "-" + mode
                assets.append(capture(asset, root, False, extra + ["--fluid25d-native-water-debug", mode]))
    elif args.phase != "profile":
        for asset, markers in review_plan():
            assets.append(capture(asset, root, markers, extra))
        for camera, time_s in (("runoff", 6000), ("collection", 14400)):
            assets.append(capture(reference.still_asset("rain-off", camera, time_s), root, False, extra))
    after = identity()
    reference.assert_same_runtime(before, after, "bank-render phase")
    if before["fluid_source_sha256"] != after["fluid_source_sha256"]:
        raise ValueError("render source changed during capture phase")
    reference.write_json_exclusive(root / "manifest.json", {
        "schema": "cubey.fluid25d.bank_render_review.v1", "phase": args.phase,
        "surface": args.surface, "runtime_identity": before, "runtime_after": after,
        "assets": assets, "profiles": measured, "numerical_fields_interpolated_in_time": False,
        "scope": "render-only, immutable recordings, no hydraulic dispatches",
    })
    print(root / "manifest.json")


if __name__ == "__main__":
    main()
