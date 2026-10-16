#!/usr/bin/env python3
"""Bounded matched-surface rendering study. Never runs a hydraulic solver."""
from __future__ import annotations

import argparse
import html
import json
from pathlib import Path

import run_native_bank_render_v1 as bank
import run_native_presentation_v1 as reference

MODES = (("triangular", 1), ("bilinear", 1), ("bspline", 1), ("bspline", 2), ("bspline", 4))


def mode_name(sampling: str, subdivision: int) -> str:
    if (sampling, subdivision) not in MODES:
        raise ValueError("unsupported study mode")
    return sampling + (f"-{subdivision}x" if sampling == "bspline" else "")


def mode_args(sampling: str, subdivision: int) -> list[str]:
    mode_name(sampling, subdivision)
    extra = ["--fluid25d-native-water-sampling", sampling]
    if sampling == "bspline":
        extra += ["--fluid25d-native-surface-subdivision", str(subdivision)]
    return extra


def study_plan() -> list[tuple[dict, bool]]:
    # Slow physical recession plus moving trails at the runoff junction.
    return [(reference.video_asset("junction", "rain-on", "runoff", 4800, 241, 24, 5), True),
            (reference.video_asset("recession", "rain-off", "collection", 7260, 120, 8, 60), False)]


def capture(root: Path, sampling: str, subdivision: int, video: bool) -> None:
    phase = root / mode_name(sampling, subdivision)
    reference.reserve_directory(phase)
    before = bank.identity()
    before["study_runner_sha256"] = reference.sha256_file(Path(__file__))
    assets = []
    extra = mode_args(sampling, subdivision)
    for camera, time_s in (("collection", 14400), ("runoff", 6000), ("overview", 6000)):
        asset = reference.still_asset("rain-off", camera, time_s)
        asset.update(width=1920, height=1080)
        assets.append(bank.capture(asset, phase, False, extra))
    # Same geometry, palette and film opacity, only terrain depth testing differs.
    for mode in ("solid", "no-occlusion", "contours"):
        asset = reference.still_asset("rain-off", "collection", 14400)
        asset.update(id=asset["id"]+"-"+mode, width=1920, height=1080)
        assets.append(bank.capture(asset, phase, False, extra + ["--fluid25d-native-water-debug", mode]))
    if video:
        for asset, markers in study_plan():
            assets.append(bank.capture(asset, phase, markers, extra))
    after = bank.identity()
    reference.assert_same_runtime(before, after, "reconstruction capture")
    if before["fluid_source_sha256"] != after["fluid_source_sha256"]:
        raise ValueError("source changed during reconstruction capture")
    reference.write_json_exclusive(phase / "manifest.json", {
        "schema": "cubey.fluid25d.bank_reconstruction.v2", "sampling": sampling,
        "subdivision": subdivision, "runtime_identity": before, "assets": assets,
        "numerical_fields_modified": False, "human_visual_acceptance": "pending",
        "displayed_terrain_approximated": sampling == "bspline",
    })
    print(phase / "manifest.json")


def profile(root: Path) -> None:
    phase = root / "profile"
    reference.reserve_directory(phase)
    before = bank.identity()
    results = []
    for sampling, subdivision in MODES:
        name = mode_name(sampling, subdivision)
        asset = reference.video_asset("profile-"+name, "rain-off", "runoff", 6000, 120, 30, 5)
        asset["profile_warmup_frames"] = 12
        prefix = phase / name
        command = reference.app_command(asset, prefix.with_suffix(".mp4"), "readable", True, prefix)
        command += mode_args(sampling, subdivision)
        _, receipt = reference.run_logged(command, phase / "logs", name, timeout=600,
                                          expected_asset=asset, expected_presentation="readable")
        summary = reference.profile_summary(prefix, 12, 108)
        results.append({"sampling": sampling, "subdivision": subdivision, "command": receipt,
                        **summary, "passes_existing_1ms_gpu_gate": summary["gpu_p95_ms"] <= 1.0})
    after = bank.identity()
    reference.assert_same_runtime(before, after, "reconstruction profile")
    reference.write_json_exclusive(phase / "manifest.json", {
        "runtime_identity": before, "profiles": results,
        "scope": "native presentation total, captured playback with markers; not live CUDA cost"})
    print(json.dumps(results, indent=2))


def review(root: Path) -> None:
    phase = root / "review"
    reference.reserve_directory(phase)
    names = [mode_name(*m) for m in MODES]
    manifests = {name: json.loads((root / name / "manifest.json").read_text()) for name in names}
    base = manifests["triangular"]
    for name, manifest in manifests.items():
        reference.assert_same_runtime(base["runtime_identity"], manifest["runtime_identity"], name)
    videos = []
    for before in (a for a in base["assets"] if a["kind"] == "video"):
        after = next(a for a in manifests["bspline-4x"]["assets"] if a["id"] == before["id"])
        for key in ("timeline", "camera", "motion_markers", "case", "width", "height"):
            if before[key] != after[key]:
                raise ValueError("unmatched comparison " + key)
        paths = [root / name / a["review_video"]["path"]
                 for name, a in (("triangular", before), ("bspline-4x", after))]
        output = phase / (before["id"] + ".mp4")
        font = reference.find_font()
        filters = (f"[0:v]drawtext=fontfile={font}:text='TRIANGULAR reference':x=12:y=62:"
                   "fontsize=20:fontcolor=white:box=1:boxcolor=black@0.8[a];"
                   f"[1:v]drawtext=fontfile={font}:text='B-SPLINE 4x experiment':x=12:y=62:"
                   "fontsize=20:fontcolor=white:box=1:boxcolor=black@0.8[b];[a][b]hstack=inputs=2[v]")
        reference.run_logged(["rtk", "proxy", "ffmpeg", "-nostdin", "-n", "-v", "error",
            "-i", str(paths[0]), "-i", str(paths[1]), "-filter_complex", filters, "-map", "[v]",
            "-an", "-c:v", "libx264", "-crf", "20", "-pix_fmt", "yuv420p", str(output)],
            phase / "logs", before["id"], timeout=240)
        probe = reference.validate_probe(reference.ffprobe_video(output, ignore_editlist=False),
                                         before["frame_count"], before["fps"], 1920, 540)
        poster = output.with_suffix(".png")
        reference.run_logged(["rtk", "proxy", "ffmpeg", "-nostdin", "-n", "-v", "error",
            "-i", str(output), "-vf", f"select=eq(n\\,{min(200,before['frame_count']-1)})",
            "-frames:v", "1", str(poster)], phase / "logs", before["id"]+"-poster", timeout=60)
        videos.append({"id": before["id"], "path": output.name,
                       "sha256": reference.sha256_file(output), "probe": probe, "poster": poster.name})
    def figures(selected: list[str]) -> str:
        return "".join(f"<figure><figcaption>{html.escape(name)}</figcaption><a href='../{name}/media/"
                      "rain-off-collection-14400s.png'><img src='../"+name+
                      "/media/rain-off-collection-14400s.png'></a></figure>" for name in selected)
    stills = figures(["triangular", "bspline-4x"])
    optional = "<details><summary>Other study modes (bilinear, B-spline 1x/2x)</summary>" + figures(names[1:4]) + "</details>"
    clips = "".join(f"<h2>{v['id']}</h2><video controls loop preload='metadata' "
                    f"poster='{v['poster']}' src='{v['path']}'></video>" for v in videos)
    reference.write_text_exclusive(phase / "index.html", "<!doctype html><meta charset='utf-8'>"
        "<title>Bank reconstruction study</title><style>body{background:#16202a;color:#eee;"
        "font:16px system-ui;margin:24px}img,video{width:100%;max-width:1920px}figure{margin:24px 0}</style>"
        "<h1>Bank reconstruction study — experimental, not promoted</h1>"
        "<p>Same saved rain fields, camera, physical clock and material. B-spline approximates BOTH "
        "displayed terrain and water; it does not refine the solver. 4x means 7.5 m display triangles "
        "over native 30 m samples. Dots/trails remain approximate motion cues.</p>"
        "<p>Compare shape and missing/slightly widened streams, not just smoothness. Human visual "
        "acceptance is pending. Read ../RESULTS.md for gates and limitations.</p>" + clips +
        "<h2>Full-resolution collection comparison (click)</h2>" + stills + optional)
    reference.write_json_exclusive(phase / "review.json", {"videos": videos, "human_visual_acceptance": "pending"})
    print(phase / "index.html")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("capture", "profile", "review"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--sampling", choices=("triangular", "bilinear", "bspline"), default="triangular")
    parser.add_argument("--subdivision", type=int, choices=(1,2,4), default=1)
    parser.add_argument("--video", action="store_true")
    args = parser.parse_args()
    if args.out.is_symlink():
        raise ValueError("output may not be a symlink")
    root = args.out.resolve()
    if root.parent != reference.OUTPUT_ROOT.resolve() or not root.name.startswith("bank-reconstruction-v2-"):
        raise ValueError("output must be a fresh bank-reconstruction-v2-* leaf under outputs/fluid")
    if args.phase == "capture":
        capture(root, args.sampling, args.subdivision, args.video)
    elif args.phase == "profile":
        profile(root)
    else:
        review(root)


if __name__ == "__main__":
    main()
