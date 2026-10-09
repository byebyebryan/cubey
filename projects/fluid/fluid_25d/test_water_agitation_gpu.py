#!/usr/bin/env python3
"""Synthetic surface-agitation renderer controls; no solver or archived inputs."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path
from unittest.mock import patch

import review_scenic_water as review
import run_native_shoreline_raster_v1 as fixtures
from test_scenic_water_gpu import runtime_identity


def run(out, target):
    review.ref.reserve_directory(out)
    identity, rows, checks = runtime_identity(target), [], []
    tunings = {}
    for name, flow, rain in (
        ("off", 0, 0),
        ("both", 1, 1),
        ("flow", 1, 0),
        ("rain", 0, 1),
    ):
        tunings[name] = out / (name + ".json")
        review.ref.write_json_exclusive(
            tunings[name],
            {
                "schema": "cubey.fluid25d.scenic-material.v2",
                "water_ripple_strength": 0,
                "water_normal_strength": 0,
                "water_flow_agitation": flow,
                "water_rain_agitation": rain,
            },
        )
    manifests = {}
    for name, rate, speed, slope, depth in (
        ("calm", 0, 0, 0, 0.5),
        ("rain120", 120, 0, 0, 0.5),
        ("rain1024", 1024, 0, 0, 0.5),
        ("flow", 0, 2.5, 0, 0.5),
        ("downhill", 0, 2.5, 0.25, 0.5),
        ("still-slope", 0, 0, 0.25, 0.5),
        ("film", 1024, 2.5, 0.25, 0.006),
        ("dry", 1024, 0, 0, 0),
    ):
        bed = [
            slope * 10 * (i % fixtures.COLS)
            for i in range(fixtures.COLS * fixtures.ROWS)
        ]
        h = [depth] * len(bed)
        write_asc = fixtures._write_asc

        def write_fields(
            path, values, cols, nrows, momentum=depth * speed, writer=write_asc
        ):
            if path.name.startswith("hUx_"):
                values = [momentum] * len(values)
            writer(path, values, cols, nrows)

        with (
            patch.object(fixtures, "fields", return_value=(bed, h)),
            patch.object(fixtures, "_write_asc", side_effect=write_fields),
        ):
            manifest = fixtures.fixture(out / "fixtures" / name, "fully-wet-lake")
        document = json.loads(manifest.read_text())
        document["protocol"]["rain_rate_m_per_s"] = rate / 3.6e6
        # Constant applied weather for animation/physical-speed comparisons.
        document["protocol"]["rainfall_history"] = [
            [0, rate / 3.6e6],
            [2, rate / 3.6e6],
        ]
        manifest.write_text(json.dumps(document, indent=2) + "\n")
        manifests[name] = manifest
    inputs = {
        str(p): review.ref.sha256_file(p)
        for p in (out / "fixtures").rglob("*")
        if p.is_file()
    }

    def render(
        name,
        case="rain1024",
        tuning="both",
        view="shaded",
        extra=(),
        video=False,
        interval=0.01,
    ):
        if identity != runtime_identity(target):
            raise ValueError("runtime changed during agitation controls")
        path = out / (name + (".mp4" if video else ".png"))
        command = [
            "rtk",
            "proxy",
            str(target.resolve()),
            "--headless",
            "--width",
            "384",
            "--height",
            "288",
            "--fluid25d-recording",
            str(manifests[case]),
            "--fluid25d-recording-gpu-validation",
            "--fluid25d-native-presentation",
            "scenic",
            "--fluid25d-scenic-material",
            "macro",
            "--fluid25d-scenic-tuning",
            str(tunings[tuning]),
            "--fluid25d-scenic-water-view",
            view,
            "--output",
            str(path),
            *extra,
        ]
        if video:
            command += [
                "--capture",
                "video",
                "--frames",
                "4",
                "--fps",
                "15",
                "--fluid25d-recording-frame-interval-seconds",
                str(interval),
            ]
        result = subprocess.run(
            command, capture_output=True, text=True, timeout=90, check=False
        )
        log = result.stdout + result.stderr
        review.ref.write_text_exclusive(out / (name + ".log"), log)
        if result.returncode:
            raise RuntimeError(log)
        receipt = review.ref.verify_capture_log(log, True)
        if "Validation Error" in log or "VUID-" in log:
            raise ValueError("Vulkan validation error")
        rows.append(
            {
                "name": name,
                "path": path.name,
                "sha256": review.ref.sha256_file(path),
                "case": case,
                "tuning": tuning,
                "receipt": receipt,
            }
        )
        print("Agitation GPU control " + name, flush=True)
        return path

    def equal(a, b, why):
        if a.read_bytes() != b.read_bytes():
            raise ValueError("expected identical pixels: " + why)
        checks.append(why)

    def different(a, b, why):
        if a.read_bytes() == b.read_bytes():
            raise ValueError("expected changed pixels: " + why)
        checks.append(why)

    for case in ("calm", "still-slope", "film", "dry"):
        equal(
            render(case + "-off", case, "off"),
            render(case + "-both", case),
            case + ": quiet/dry/film identity",
        )
    rain = render("rain-both")
    equal(rain, render("rain-repeat"), "repeat deterministic")
    equal(rain, render("rain-only", tuning="rain"), "motionless lake needs no flow")
    different(
        rain,
        render("rain-off", tuning="off"),
        "applied rain changes lake shading with streaks hidden",
    )
    equal(
        rain,
        render(
            "rain-streak-strength-zero",
            extra=("--fluid25d-rain-visuals", "--fluid25d-rain-visual-strength", "0"),
        ),
        "hidden or zero-strength streaks retain weather response",
    )
    equal(
        rain,
        render("rain-fall-speed8", extra=("--fluid25d-rain-visual-speed", "8")),
        "fall speed does not control surface response",
    )
    for case in ("flow", "downhill"):
        flowing = render(case + "-on", case, "flow")
        different(
            flowing,
            render(case + "-off", case, "off"),
            case + ": active flow changes shading without rain",
        )
    # Change only material controls, not any bank/source/input fields.
    for view in ("coverage", "depth-bands", "no-detail"):
        equal(
            render(view + "-off", tuning="off", view=view),
            render(view + "-on", view=view),
            view + ": retained diagnostic identity",
        )
    equal(
        render(
            "terrain-off",
            tuning="off",
            extra=("--fluid25d-scenic-terrain-view", "terrain-only"),
        ),
        render("terrain-on", extra=("--fluid25d-scenic-terrain-view", "terrain-only")),
        "terrain identity",
    )
    # Rain bands are entirely unresolved in this macro fixture. Read the actual
    # effective-roughness output, with constant terrain held in all three images.
    totals = []
    for case in ("calm", "rain120", "rain1024"):
        image = render("roughness-" + case, case, "rain", view="roughness")
        raw = subprocess.run(
            [
                "rtk",
                "proxy",
                "ffmpeg",
                "-v",
                "error",
                "-i",
                str(image),
                "-f",
                "rawvideo",
                "-pix_fmt",
                "rgb24",
                "pipe:1",
            ],
            capture_output=True,
            check=True,
            timeout=30,
        ).stdout
        totals.append(sum(raw))
    if not totals[0] < totals[1] < totals[2]:
        raise ValueError(
            "applied rain does not monotonically raise effective roughness"
        )
    checks.append("calm/120/1024 actual roughness output monotone")
    videos = [
        render(
            "motion-" + str(i), case="downhill", tuning="flow", video=True, interval=i
        )
        for i in (0.01, 0.1)
    ]
    videos.append(
        render(
            "motion-fall-speed8",
            case="downhill",
            tuning="flow",
            video=True,
            extra=("--fluid25d-rain-visual-speed", "8"),
        )
    )
    decoded = [
        subprocess.run(
            [
                "rtk",
                "proxy",
                "ffmpeg",
                "-v",
                "error",
                "-ignore_editlist",
                "1",
                "-i",
                str(p),
                "-f",
                "rawvideo",
                "-pix_fmt",
                "rgb24",
                "pipe:1",
            ],
            capture_output=True,
            check=True,
            timeout=30,
        ).stdout
        for p in videos
    ]
    frame_bytes = 384 * 288 * 3
    if (
        any(len(raw) != 4 * frame_bytes for raw in decoded)
        or not decoded[0] == decoded[1] == decoded[2]
    ):
        raise ValueError("physical pacing or falling speed changes surface animation")
    if decoded[0][:frame_bytes] == decoded[0][-frame_bytes:]:
        raise ValueError("flow agitation fails to animate on frozen fields")
    checks.append("held-field animation independent of physical pacing and fall speed")
    if identity != runtime_identity(target) or inputs != {
        str(p): review.ref.sha256_file(p)
        for p in (out / "fixtures").rglob("*")
        if p.is_file()
    }:
        raise ValueError("runtime changed or native fixture mutated")
    review.ref.write_json_exclusive(
        out / "result.json",
        {
            "status": "PASS",
            "runtime": identity,
            "rows": rows,
            "checks": checks,
            "input_files": inputs,
            "roughness_image_sums": totals,
            "scope": "synthetic frozen renderer controls; zero hydraulic dispatches; not solver or owner acceptance",
        },
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--target", required=True, type=Path)
    p.add_argument("--out", type=Path)
    a = p.parse_args()
    if a.out:
        run(a.out.resolve(), a.target)
    else:
        with tempfile.TemporaryDirectory(prefix="cubey-water-agitation-gpu-") as temp:
            run(Path(temp) / "controls", a.target)


if __name__ == "__main__":
    main()
