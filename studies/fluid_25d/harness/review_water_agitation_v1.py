#!/usr/bin/env python3
"""Matched replay evidence for render-only surface agitation; no solver launch."""

from __future__ import annotations

import argparse
import html
import json
import re
import shutil
import statistics
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

import run_native_presentation_v1 as ref
from test_scenic_water_gpu import runtime_identity

STREAM = (
    ref.OUTPUT_ROOT
    / "rain-power2-v1-20261008-a2/cases/rain512/recording/recording.json"
)
LAKE = ref.OUTPUT_ROOT / "natural-lakes-v1-20261008-a1/recording/recording.json"
SCENES = (
    ("streams", STREAM, 3600, "runoff", -0.50),
    ("lake", LAKE, 27000, "collection", -0.50),
    ("wide", STREAM, 3600, "overview", -0.75),
    ("topdown", STREAM, 3600, "runoff", -0.92),
)
VARIANTS = {
    "reference": (0, 0),
    "candidate": (1, 1),
    "flow-only": (1, 0),
    "rain-only": (0, 1),
}


def sources():
    sim = ref.ROOT / "projects/fluid/sim/fluid_25d"
    paths = [
        sim / p
        for p in (
            "fluid_25d_scenic.cpp",
            "fluid_25d_scenic_material.cpp",
            "fluid_25d_scenic_material.h",
            "fluid_25d_recording_app.cpp",
            "fluid_25d_rain_visuals.h",
            "fluid_25d_water_agitation.h",
        )
    ]
    paths += list((sim / "shaders").glob("*.glsl"))
    paths += list((sim / "shaders").glob("fluid_25d_scenic_*.frag"))
    paths += [ref.ROOT / "shaders/cubey/pbr.glsl", Path(__file__).resolve()]
    return {
        str(p.relative_to(ref.ROOT)): ref.sha256_file(p) for p in paths if p.is_file()
    }


def inputs():
    return {
        str(p.relative_to(ref.ROOT)): ref.recording_tree_fingerprint(p.parent)
        for p in (STREAM, LAKE)
    }


def command(scene, path, tuning=None, video=False, width=1280, height=720):
    _name, recording, start, camera, pitch = scene
    args = [
        "rtk",
        "proxy",
        str(ref.APP),
        "--headless",
        "--width",
        str(width),
        "--height",
        str(height),
        "--fluid25d-recording",
        str(recording),
        "--fluid25d-recording-time-seconds",
        str(start),
        "--fluid25d-recording-camera",
        camera,
        "--fluid25d-native-camera-pitch",
        str(pitch),
        "--fluid25d-native-presentation",
        "scenic",
        "--fluid25d-native-surface-highlights",
        "off",
        "--fluid25d-scenic-material",
        "macro",
        "--fluid25d-recording-gpu-validation",
        "--output",
        str(path),
    ]
    if tuning:
        args += ["--fluid25d-scenic-tuning", str(tuning)]
    if video:
        args += [
            "--capture",
            "video",
            "--frames",
            "180",
            "--fps",
            "30",
            "--fluid25d-recording-frame-interval-seconds",
            ".05",
        ]
    return args


def capture(out, label, variant, videos=False):
    phase = out / label
    ref.reserve_directory(phase)
    runtime, pins, rows = runtime_identity(ref.APP), inputs(), []
    source_pins = sources()
    for name in source_pins:
        target = phase / "source" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ref.ROOT / name, target)
    tuning = None
    if variant != "baseline":
        flow, rain = VARIANTS[variant]
        tuning = phase / "tuning.json"
        ref.write_json_exclusive(
            tuning,
            {
                "schema": "cubey.fluid25d.scenic-material.v2",
                "water_flow_agitation": flow,
                "water_rain_agitation": rain,
            },
        )
    for scene in SCENES:
        name = scene[0]
        modes = (False, True) if videos and name in ("streams", "lake") else (False,)
        for video in modes:
            path = phase / (name + (".mp4" if video else ".png"))
            completed, receipt = ref.run_logged(
                command(scene, path, tuning, video),
                phase / "logs",
                path.stem + ("-video" if video else "-still"),
                timeout=180,
                expected_presentation="scenic",
            )
            log = completed.stdout + completed.stderr
            ref.verify_capture_log(log, True)
            if "VUID-" in log or "Validation Error" in log:
                raise ValueError("Vulkan validation error")
            fields = re.findall(r"requested_s=([\d.]+) saved_s=([\d.]+)", log)
            if not fields or len({saved for _, saved in fields}) != 1:
                raise ValueError("comparison must hold one saved water state")
            rate = re.search(r"applied_mm_h=([\d.]+)", log)
            if not rate:
                raise ValueError("missing applied-weather receipt")
            browser = None
            if video:
                browser = phase / (name + "-browser.mp4")
                subprocess.run(
                    [
                        "rtk",
                        "proxy",
                        "ffmpeg",
                        "-nostdin",
                        "-v",
                        "error",
                        "-ignore_editlist",
                        "1",
                        "-i",
                        str(path),
                        "-an",
                        "-c:v",
                        "libx264",
                        "-crf",
                        "18",
                        "-preset",
                        "veryfast",
                        "-pix_fmt",
                        "yuv420p",
                        "-r",
                        "30",
                        "-frames:v",
                        "180",
                        "-movflags",
                        "+faststart",
                        str(browser),
                    ],
                    check=True,
                    timeout=90,
                )
                ref.validate_probe(ref.ffprobe_video(browser), 180, 30, 1280, 720)
            rows.append(
                {
                    "scene": name,
                    "kind": "video" if video else "still",
                    "path": path.name,
                    "browser": browser.name if browser else None,
                    "sha256": ref.sha256_file(path),
                    "receipt": receipt,
                    "saved_s": float(fields[0][1]),
                    "applied_mm_h": float(rate[1]),
                }
            )
            print("Captured " + label + "/" + path.name, flush=True)
    if (
        runtime != runtime_identity(ref.APP)
        or pins != inputs()
        or source_pins != sources()
    ):
        raise ValueError("runtime or immutable inputs changed during capture")
    ref.write_json_exclusive(
        phase / "manifest.json",
        {
            "runtime": runtime,
            "inputs": pins,
            "assets": rows,
            "source_files": source_pins,
            "variant": variant,
            "scope": "held native replay; no solver run",
        },
    )


def profile(out, label="profiles", heights=(720, 1080)):
    phase = out / label
    ref.reserve_directory(phase)
    runtime, rows, pins = runtime_identity(ref.APP), [], inputs()
    for height in heights:
        width = 1280 if height == 720 else 1920
        for batch in range(3):
            for variant in (
                ("reference", "candidate")
                if batch % 2 == 0
                else ("candidate", "reference")
            ):
                prefix = phase / f"{height}-{batch}-{variant}"
                args = command(
                    SCENES[0],
                    prefix.with_suffix(".mp4"),
                    out / variant / "tuning.json",
                    True,
                    width,
                    height,
                )
                args[args.index("--frames") + 1] = "120"
                args += [
                    "--profile-output",
                    str(prefix),
                    "--profile-warmup-frames",
                    "12",
                ]
                _, receipt = ref.run_logged(
                    args,
                    phase / "logs",
                    prefix.name,
                    timeout=180,
                    expected_presentation="scenic",
                )
                rows.append(
                    {
                        "height": height,
                        "batch": batch,
                        "variant": variant,
                        "summary": ref.profile_summary(prefix, 12, 108),
                        "receipt": receipt,
                    }
                )
                print("Profiled " + prefix.name, flush=True)
    if runtime != runtime_identity(ref.APP) or pins != inputs():
        raise ValueError("runtime changed during profiles")
    summary = [
        {
            "height": h,
            "variant": v,
            "median_ms": statistics.median(
                r["summary"]["gpu_median_ms"]
                for r in rows
                if r["height"] == h and r["variant"] == v
            ),
            "p95_ms": statistics.median(
                r["summary"]["gpu_p95_ms"]
                for r in rows
                if r["height"] == h and r["variant"] == v
            ),
        }
        for h in heights
        for v in ("reference", "candidate")
    ]
    ref.write_json_exclusive(
        phase / "result.json",
        {
            "runtime": runtime,
            "rows": rows,
            "summaries": summary,
            "scope": "headless held fields; no CUDA producer; excludes setup",
        },
    )


def report(out, candidate):
    phases = {
        name: json.loads((out / name / "manifest.json").read_text())
        for name in ("baseline", "reference", candidate)
    }
    if (
        phases["reference"]["inputs"] != phases[candidate]["inputs"]
        or phases["baseline"]["inputs"] != phases[candidate]["inputs"]
    ):
        raise ValueError("comparison inputs differ")
    baseline = {
        r["scene"]: r["sha256"]
        for r in phases["baseline"]["assets"]
        if r["kind"] == "still"
    }
    reference = {
        r["scene"]: r["sha256"]
        for r in phases["reference"]["assets"]
        if r["kind"] == "still"
    }
    if baseline != reference:
        raise ValueError("disabled agitation must match the pre-change build exactly")
    if phases["reference"]["runtime"] != phases[candidate]["runtime"]:
        raise ValueError("reference/candidate runtime differs")
    if phases["reference"]["source_files"] != phases[candidate]["source_files"]:
        raise ValueError("reference/candidate sources differ")
    for label, phase in phases.items():
        for row in phase["assets"]:
            if ref.sha256_file(out / label / row["path"]) != row["sha256"]:
                raise ValueError("capture media no longer matches receipt")
        for name, digest in phase.get("source_files", {}).items():
            if ref.sha256_file(out / label / "source" / name) != digest:
                raise ValueError("capture source snapshot no longer matches receipt")
    gpu = json.loads((out / "gpu-controls/result.json").read_text())
    profiles = json.loads((out / "profiles/result.json").read_text())
    recheck = json.loads((out / "profiles-1080-recheck/result.json").read_text())
    if gpu["status"] != "PASS" or any(
        r != phases[candidate]["runtime"]
        for r in (gpu["runtime"], profiles["runtime"], recheck["runtime"])
    ):
        raise ValueError("GPU evidence/runtime differs from captures")
    test_count = 0
    for name in ("host-tests.xml", "gpu-regressions.xml"):
        tests = ET.parse(out / name).getroot()
        if any(
            int(tests.get(k, "0"))
            for k in ("failures", "errors", "skipped", "disabled")
        ):
            raise ValueError("regression failures or missing GPU execution")
        test_count += int(tests.get("tests", "0"))
    perf = []
    primary_profiles = [
        r for r in profiles["summaries"] if r["height"] == 720
    ] + recheck["summaries"]
    for height in (720, 1080):
        values = {r["variant"]: r for r in primary_profiles if r["height"] == height}
        before, after = values["reference"], values["candidate"]
        delta_us = 1000 * (after["median_ms"] - before["median_ms"])
        perf.append(
            f"<li>{height}p{' (repeat)' if height == 1080 else ''}: retained {before['median_ms']:.3f} ms / candidate "
            f"{after['median_ms']:.3f} ms median GPU span; Δ {delta_us:+.1f} µs. "
            f"p95 {before['p95_ms']:.3f} / {after['p95_ms']:.3f} ms.</li>"
        )
    cards = []
    for scene in ("streams", "lake", "wide", "topdown"):
        new = next(
            r
            for r in phases[candidate]["assets"]
            if r["scene"] == scene and r["kind"] == "still"
        )
        videos = all(
            any(
                r["scene"] == scene and r["kind"] == "video"
                for r in phases[p]["assets"]
            )
            for p in ("reference", candidate)
        )
        video_html = (
            ""
            if not videos
            else '<div class="pair">'
            + "".join(
                f'<div><p>{title}</p><video muted loop playsinline controls preload="metadata" poster="{p}/{scene}.png" '
                f'src="{p}/{scene}-browser.mp4"></video></div>'
                for p, title in (
                    ("reference", "Previous smoother surface"),
                    (candidate, "Agitated surface"),
                )
            )
            + "</div>"
        )
        cards.append(
            f"<section><h2>{html.escape(scene.title())}</h2><p>Held water at {new['saved_s']:.0f}s; applied rain {new['applied_mm_h']:.0f} mm/h. "
            f"Streaks and dots hidden to isolate water shading.</p>{video_html}<details open><summary>Matched still comparison</summary>"
            f'<div class="wipe"><img src="{candidate}/{scene}.png"><img class="before" src="reference/{scene}.png"></div>'
            '<input aria-label="Before after comparison" type="range" min="0" max="100" value="50">'
            "<p>Left: previous smoother surface · Right: rougher surface</p></details></section>"
        )
    page = (
        """<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Fluid 2.5D · Water surface agitation</title><style>body{max-width:1200px;margin:auto;padding:24px;background:#111820;color:#dee6ec;font:16px/1.5 system-ui}a{color:#91d5ff}h1,h2{line-height:1.2}.pair{display:grid;grid-template-columns:1fr 1fr;gap:12px}video,img{width:100%;display:block}section{margin:32px 0;padding:16px;background:#1c2631;border-radius:12px}.wipe{position:relative}.before{position:absolute;inset:0;clip-path:inset(0 50% 0 0)}input{width:100%}button{padding:12px;font:inherit}summary{cursor:pointer}@media(max-width:650px){.pair{grid-template-columns:1fr}body{padding:12px}}</style>
<nav><a href="/">All reports</a></nav><h1>Water surface agitation</h1><p>Rendering-only study: flow-driven breakup plus rain-driven micro-disturbance. Same terrain, saved water, absorption, banks and lighting.</p>
<p><strong>Read this:</strong> look for less coherent plastic-looking highlights in streams and softer—not absent—reflections in the lake. The 120 mm/h lake and 512 mm/h streams are different saved runs, not an intensity A/B.</p>
<p>Fine disturbance should become roughness at macro distances, not giant bumps or crawling pixels. This is an artistic proxy, not measured turbulence or a new fluid solver.</p><button id="play">Play paired comparisons</button>"""
        + """<p><strong>Verdict:</strong> a useful, modest reduction in polished highlights, not a complete solution to ribbon-like streams.
The lake retains its deep-water color. I rejected a 16 m flow band: it introduced stippling and then regular shore ridges.
The retained candidate uses 4 m flow / 1.25 m rain bands, mostly unresolved at these macro views. Expect broader highlights,
not individually visible rain rings or surf. Bank tessellation, narrow coverage and absent foam remain unchanged.</p>
<p>Measured median rendering cost increases about 3–4%; 1080p tail timings remain variable. See the validation section below.</p>
<p><strong>Default:</strong> the reviewed rougher setting is now the mountain macro default, accepted by the owner.
It remains configurable. GUI: Scenic materials → Water appearance and daylight →
Flow surface agitation / Rain surface agitation (each 0–1). Both 0 = previous smoother surface; both 1 = rougher default. CLI uses
<a href="candidate/tuning.json">candidate tuning JSON</a> with <code>--fluid25d-scenic-tuning</code>.</p>"""
        + "".join(cards)
        + f"<h2>Validation and cost</h2><p>{test_count} regression CTests passed, plus "
        f"{len(gpu['rows'])} synthetic agitation renders. The compute controls check 64 agitation cases for "
        "resolved slopes, subpixel variance, zero strength and clock wrapping. Native upload checks pass; all replay captures dispatch zero hydraulics.</p>"
        + "<ul>"
        + "".join(perf)
        + "</ul><p>Three alternating pairs per series, 108 warmed GPU samples per run; "
        "held replay, no CUDA producer. This is renderer scope, not total app/solver throughput.</p>"
        + '<p><a href="gpu-controls/result.json">Agitation controls</a> · <a href="profiles/result.json">Profiles</a> · '
        '<a href="host-tests.xml">Host checks</a> · <a href="gpu-regressions.xml">GPU regressions</a> · '
        '<a href="candidate/manifest.json">Capture, runtime, native-input and source pins</a></p>'
        + """
<h2>Evidence boundary</h2><p>Disabled controls match four pre-change captures exactly. Native recordings remain byte-identical. Captures and automated checks are not owner visual acceptance; the candidate remains switchable.</p>
<p>The first 1080p timing series was variable, especially retained rendering. A negative timing delta here is not evidence of a speedup.
The 720p pairs and repeated 1080p medians were consistent: about 3–4% extra median rendering cost.
But repeated 1080p candidate p95 spans still range 1.15–1.56 ms; the retained runs also show intermittent spikes.
That tail variability remains a measured limitation, not a proven performance pass. Desktop GPU activity and clock state are not isolated.
<a href="profiles-1080-recheck/result.json">Bounded 1080p repeat</a>.</p>
<p>Reference principles: <a href="https://onlinelibrary.wiley.com/doi/10.1111/j.1467-8659.2009.01618.x/full">Bruneton et al.: geometry, normals and BRDF</a>; <a href="https://seblagarde.wordpress.com/2013/01/03/water-drop-2b-dynamic-rain-and-its-effects/">Lagarde: rain-driven surface ripple normals</a>. We reuse Cubey's filtered-normal/roughness approach rather than adding another solver.</p>
<script>for(const r of document.querySelectorAll('input'))r.oninput=()=>r.previousElementSibling.querySelector('.before').style.clipPath=`inset(0 ${100-r.value}% 0 0)`;document.querySelector('#play').onclick=async()=>{const v=[...document.querySelectorAll('video')];const play=v.some(x=>x.paused);if(play){for(const x of v){x.pause();x.currentTime=0}await Promise.all(v.map(x=>x.play()))}else for(const x of v)x.pause();document.querySelector('#play').textContent=play?'Pause comparisons':'Play paired comparisons'};</script></html>"""
    )
    ref.write_text_exclusive(out / "index.html", page)
    ref.write_json_exclusive(
        out / "comparison.json",
        {
            "reference_matches_prechange": len(baseline),
            "candidate": candidate,
            "inputs_unchanged": True,
            "regression_tests": test_count,
            "agitation_renders": len(gpu["rows"]),
            "profile_summaries": primary_profiles,
            "first_profile_series": profiles["summaries"],
            "performance_limitation": "1080p tail variability; desktop/clock state not isolated",
            "default": "reviewed rougher macro default; both strengths 1, independently configurable",
        },
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=("capture", "profile", "report"))
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--label", default="candidate")
    p.add_argument("--variant", choices=("baseline", *VARIANTS), default="candidate")
    p.add_argument("--videos", action="store_true")
    p.add_argument("--profile-phase", default="profiles")
    p.add_argument(
        "--profile-heights",
        nargs="+",
        type=int,
        choices=(720, 1080),
        default=(720, 1080),
    )
    a = p.parse_args()
    out = a.out.resolve()
    if (
        out.parent != ref.OUTPUT_ROOT
        or not out.name.startswith("scenic-water-agitation-")
        or a.out.is_symlink()
    ):
        p.error("fresh scenic-water-agitation-* leaf under outputs/fluid required")
    if not re.fullmatch(r"[a-z][a-z0-9-]*", a.label):
        p.error("label must be a safe phase name")
    if a.action == "capture":
        capture(out, a.label, a.variant, a.videos)
    elif a.action == "profile":
        if not re.fullmatch(r"[a-z][a-z0-9-]*", a.profile_phase):
            p.error("profile phase must be a safe directory name")
        profile(out, a.profile_phase, a.profile_heights)
    else:
        report(out, a.label)


if __name__ == "__main__":
    main()
