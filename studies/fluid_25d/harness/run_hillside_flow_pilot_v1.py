#!/usr/bin/env python3
"""One frozen source-only macro-terrain case; never edits or generates terrain."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DT = 2
SUBSTEPS = 16
Q = 100
HORIZON = 1800
PROGRESS = "fluid_25d.hillside.progress"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def arguments(recipe: Path, manifest: Path, palette: tuple[float, float]) -> list[str]:
    data = json.loads(recipe.read_text())
    if data["schema"] != "cubey.fluid25d.hillside_flow_study.v1" or "expected_outlet" in data:
        raise ValueError("pilot requires a source-only hillside recipe")
    x, z, width, height = data["crop_xzwh"]
    return [
        "--headless", "--width", "1280", "--height", "720",
        "--fluid25d-scenario", "hillside-flow-study", "--fluid25d-solver", "finite-volume",
        "--fluid25d-natural-flow-recipe", str(recipe),
        "--fluid25d-natural-flow-source-m3-per-s", str(Q),
        "--terrain-heightfield", str(manifest),
        "--fluid25d-terrain-crop-x", str(x), "--fluid25d-terrain-crop-z", str(z),
        "--grid-width", str(width), "--grid-height", str(height),
        "--fluid25d-cell-size-m", "30", "--fluid25d-fixed-delta-seconds", str(DT),
        "--fluid25d-substeps", str(SUBSTEPS), "--fluid25d-gravity-m-per-s2", "9.81",
        "--fluid25d-flow-damping-per-second", "0.15", "--fluid25d-view", "catchment",
        "--fluid25d-render-height-scale", "1", "--fluid25d-terrain-palette-low-m", str(palette[0]),
        "--fluid25d-terrain-palette-high-m", str(palette[1]),
    ]


def run(app: Path, args: list[str], prefix: Path) -> dict:
    log = prefix.with_suffix(".log")
    if log.exists():
        raise ValueError(f"refusing to overwrite {log}")
    started = time.perf_counter()
    with log.open("w") as stream:
        result = subprocess.run(["rtk", "proxy", str(app), *args], cwd=ROOT,
                                stdout=stream, stderr=subprocess.STDOUT)
    return {"exit_code": result.returncode, "wall_seconds": time.perf_counter() - started,
            "command": [str(app), *args], "log": str(log.relative_to(ROOT))}


def read_metrics(path: Path) -> dict[int, dict[tuple[str, str], float]]:
    frames: dict[int, dict[tuple[str, str], float]] = {}
    with path.open() as stream:
        for row in csv.DictReader(stream):
            value = float(row["value"])
            key = row["category"], row["name"]
            frame = frames.setdefault(int(row["frame_index"]), {})
            if not math.isfinite(value) or key in frame:
                raise ValueError("nonfinite or duplicate profile metric")
            frame[key] = value
    if not frames:
        raise ValueError("missing diagnostics")
    return frames


def summarize(frames: dict[int, dict], horizon: int) -> dict:
    expected = list(range(horizon // DT))
    if sorted(frames) != expected:
        raise ValueError("profile does not cover every frozen outer step")
    last = frames[expected[-1]]
    water = lambda values, name: values[("fluid_25d.water", name)]
    tolerance = max(0.003, Q * horizon * 3e-4)
    flags = [f for f in expected if frames[f][("fluid_25d.solver", "finite_volume_status_flags")] != 0]
    residual = max(abs(water(v, "conservation_residual_m3")) for v in frames.values())
    source = water(last, "cumulative_source_volume_m3")
    negative = any(water(v, name) < 0 for v in frames.values() for name in (
        "total_water_volume_m3", "maximum_depth_m", "cumulative_source_volume_m3",
        "cumulative_sink_volume_m3", "cumulative_boundary_outflow_volume_m3"))
    arrivals = {}
    for drop in (20, 50, 100, 200):
        wet = [f for f in expected if frames[f][(PROGRESS, f"below_source_wet_cells_{drop}m")] > 0]
        arrivals[str(drop)] = (wet[0] + 1) * DT if wet else None
    healthy = (not flags and not negative and residual <= tolerance and
               abs(source - Q * horizon) <= tolerance and
               water(last, "cumulative_sink_volume_m3") == 0)
    return {
        "numeric_profile_checks_passed": healthy,
        "strict_cpu_gpu_parity": "separate oracle check; not inferred from profile",
        "band_first_material_water_seconds": arrivals,
        "front_definition": "depth >=0.01m, below LOWEST source bed; diagnostic only",
        "maximum_wetted_bed_drop_m": last[(PROGRESS, "maximum_wetted_bed_drop_m")],
        "farthest_materially_wet_distance_m": last[(PROGRESS, "farthest_materially_wet_distance_m")],
        "source_region_water_volume_m3": last[(PROGRESS, "source_region_water_volume_m3")],
        "materially_wet_cell_count": last[(PROGRESS, "materially_wet_cell_count")],
        "lower_band_volume_m3": {str(d): last[(PROGRESS, f"below_source_water_volume_{d}m")]
                                 for d in (20, 50, 100, 200)},
        "source_volume_m3": source, "stored_water_m3": water(last, "total_water_volume_m3"),
        "boundary_export_m3": water(last, "cumulative_boundary_outflow_volume_m3"),
        "maximum_depth_m": water(last, "maximum_depth_m"),
        "max_abs_water_residual_m3": residual, "water_ledger_tolerance_m3": tolerance,
        "nonzero_solver_flag_frames": flags,
        "downhill_progress_observed": healthy and arrivals["100"] is not None,
        "not_claimed": "a mapped river, real hydrology, terrain erosion, waterfall fidelity, or required outlet arrival",
    }


def gpu_timings(path: Path) -> dict:
    with path.open() as stream:
        values = [float(row["duration_ms"]) for row in csv.DictReader(stream)
                  if row["kind"] == "gpu" and row["label"] == "fluid_25d solver"]
    values = values[10:]
    if not values or any(not math.isfinite(v) or v < 0 for v in values):
        raise ValueError("missing or invalid GPU solver timings")
    return {"samples_after_first_10": len(values), "mean_ms_per_2s_step": statistics.mean(values),
            "p95_ms_per_2s_step": sorted(values)[int(0.95 * (len(values) - 1))],
            "scope": "aggregate GPU solve,16 substeps; excludes readback,render,encoding and CPU oracle"}


def label_video(source: Path) -> dict:
    target = source.with_name(source.stem + "-labelled.mp4")
    title = "Hillside supply | dry start | 100 m3/s continuous input | no prescribed drain | 60x playback"
    physical = r"Physical time %{eif\:floor(t*60+2)\:d} s / 1800 s"
    filters = (f"drawtext=text='{title}':x=12:y=12:fontsize=17:fontcolor=white:box=1:boxcolor=black@0.8:boxborderw=6,"
               f"drawtext=text='{physical}':x=12:y=42:fontsize=17:fontcolor=white:box=1:boxcolor=black@0.8:boxborderw=6")
    cmd = ["rtk", "proxy", "ffmpeg", "-nostdin", "-n", "-i", str(source), "-vf", filters,
           "-c:v", "libx264", "-crf", "20", "-an", str(target)]
    with source.with_name(source.stem + "-label.log").open("w") as stream:
        result = subprocess.run(cmd, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"labelling failed; raw capture retained at {source}")
    return {"path": str(target.relative_to(ROOT)), "sha256": sha(target), "playback_acceleration": 60}


def report_checks_passed(report: dict) -> bool:
    cases = report.get("cases", [])
    if report.get("error") or not cases or any(case["exit_code"] != 0 or case.get("error") for case in cases):
        return False
    if report["phase"] == "evidence":
        return len(cases) == 3 and report.get("strict_startup_oracle", {}).get("exit_code") == 0
    summary = report.get("summary", {})
    return bool(summary.get("numeric_profile_checks_passed") and
                (report["phase"] != "hydraulics" or summary.get("downhill_progress_observed")))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=ROOT / "build/dev/projects/fluid/fluid_25d/fluid_25d")
    parser.add_argument("--recipe", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--palette-low", type=float, required=True)
    parser.add_argument("--palette-high", type=float, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--phase", choices=("hydraulics", "evidence", "benchmark"), default="hydraulics")
    args = parser.parse_args()
    args.app = args.app.resolve()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    report_path = args.out / f"{args.phase}.json"
    if report_path.exists():
        raise ValueError(f"refusing to overwrite {report_path}")
    common = arguments(args.recipe.resolve(), args.manifest.resolve(), (args.palette_low, args.palette_high))
    report = {"schema": "cubey.fluid25d.hillside_flow_pilot.v1", "app_sha256": sha(args.app),
              "phase": args.phase,
              "recipe_sha256": sha(args.recipe), "runner_sha256": sha(Path(__file__)),
              "frozen_Q_m3_per_s": Q, "fixed_delta_seconds": DT, "substeps": SUBSTEPS,
              "terrain": "raw immutable native30m; starts dry", "drain": "none",
              "boundaries": "all perimeter faces outward-only; not chosen destinations",
              "frame_time": "f is state after (f+1)*2 physical seconds", "cases": []}
    if args.phase in ("hydraulics", "benchmark"):
        horizon = HORIZON if args.phase == "hydraulics" else 120
        prefix = args.out / args.phase
        cmd = common + ["--frames", str(horizon // DT), "--capture", "png",
            "--fluid25d-catchment-view", "water-isolation", "--fluid25d-natural-flow-home-pitch-radians", "-1.55",
            "--profile-output", str(prefix), "--profile-diagnostics", "--profile-diagnostic-interval", "1",
            "--output", str(prefix.with_suffix(".png"))]
        execution = run(args.app, cmd, prefix)
        report["cases"].append(execution)
        if execution["exit_code"] == 0:
            try:
                report["summary"] = summarize(read_metrics(prefix.with_suffix(".metrics.csv")), horizon)
                report["gpu_timings"] = gpu_timings(prefix.with_suffix(".passes.csv"))
            except (ValueError, KeyError, OSError) as error:
                report["error"] = f"profile analysis failed: {error}"
    else:
        hydraulic = json.loads((args.out / "hydraulics.json").read_text())
        if (hydraulic["app_sha256"] != report["app_sha256"] or
            hydraulic["recipe_sha256"] != report["recipe_sha256"] or
            not hydraulic.get("summary", {}).get("downhill_progress_observed")):
            raise ValueError("evidence requires matching validated app/recipe with downhill progress")
        for name, view, pitch, extra in (
            ("whole-domain", "composite", "-0.72", []),
            ("source-context", "water-isolation", "-0.72", ["--fluid25d-hillside-source-context"]),
            ("source-top-down", "water-isolation", "-1.55", ["--fluid25d-hillside-source-context"]),
        ):
            prefix = args.out / name
            cmd = common + ["--frames", str(HORIZON // DT), "--capture", "video", "--fps", "30",
                "--fluid25d-catchment-view", view, "--fluid25d-natural-flow-home-pitch-radians", pitch,
                *extra, "--output", str(prefix.with_suffix(".mp4"))]
            execution = run(args.app, cmd, prefix)
            report["cases"].append(execution)
            if execution["exit_code"] != 0:
                break
            try:
                execution["labelled_video"] = label_video(prefix.with_suffix(".mp4"))
            except (RuntimeError, OSError) as error:
                execution["error"] = f"video labelling failed: {error}"
                break
        prefix = args.out / "startup-oracle"
        execution = run(args.app, common + ["--frames", "30", "--capture", "png",
            "--fluid25d-gpu-oracle-validation", "--output", str(prefix.with_suffix(".png"))], prefix)
        report["strict_startup_oracle"] = execution
    if sha(args.app) != report["app_sha256"] or sha(args.recipe) != report["recipe_sha256"]:
        report["error"] = (report.get("error", "") +
                           "; app or recipe changed during the frozen phase").lstrip("; ")
    report["phase_checks_passed"] = report_checks_passed(report)
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    if not report["phase_checks_passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
