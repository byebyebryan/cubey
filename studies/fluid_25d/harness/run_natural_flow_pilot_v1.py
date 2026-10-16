#!/usr/bin/env python3
"""Frozen four-case native-terrain pilot; optional matched winner captures.

This runner never generates or alters terrain and never tunes forcing from
results. Run hydraulics, review its evidence, then run the evidence phase.
"""
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
FIXTURES = Path(__file__).resolve().parent / "fixtures/native-flow-study-v1"
SITES = {
    "a": "cache/terrain/sources/v1/desert-canyon-study/desert-low-relief/heightfield.json",
    "b": "cache/terrain/sources/v1/landscape-variations/dry-upland/heightfield.json",
}
PALETTE_RANGES_M = {"a": (108, 145), "b": (374, 407)}
Q_VALUES = (30, 60)
DT = 2
FRAMES = 3600
FPS = 30
PROFILE_INTERVAL = 1
GAUGES = ("upstream", "midstream", "downstream")
BOUNDARY = "fluid_25d.natural_flow.boundary_ledger"
EXPECTED = "expected_window_outflow_m3"
OTHER = "other_noncorner_edge_outflow_m3"
CORNER = "corner_outflow_m3"
NONEDGE = "non_edge_outflow_m3"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(app: Path, args: list[str], log: Path) -> dict:
    if log.exists():
        raise RuntimeError(f"refusing to overwrite run log: {log}")
    started = time.perf_counter()
    with log.open("w") as stream:
        result = subprocess.run(["rtk", "proxy", str(app), *args], cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    return {"exit_code": result.returncode, "wall_seconds": time.perf_counter() - started, "log": str(log.relative_to(ROOT))}


def label_video(source: Path, q: int, out: Path) -> dict:
    target = source.with_name(source.stem + "-labelled.mp4")
    if target.exists():
        raise RuntimeError(f"refusing to overwrite labelled video: {target}")
    text = f"2 h physical evolution | 60x playback | continuous source {q} m3/s | green source / amber observed exit"
    physical_time = r"Physical time %{eif\:floor(t*60+2)\:d} s / 7200 s"
    filters = f"drawtext=text='{text}':x=16:y=16:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.7:boxborderw=8,drawtext=text='{physical_time}':x=16:y=52:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.7:boxborderw=8"
    args = ["rtk", "proxy", "ffmpeg", "-nostdin", "-n", "-i", str(source), "-vf", filters, "-c:v", "libx264", "-crf", "20", "-an", str(target)]
    log = out / (source.stem + "-labelling.log")
    with log.open("w") as stream:
        result = subprocess.run(args, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"video labelling failed; raw evidence retained: {source}")
    return {"path": str(target.relative_to(ROOT)), "sha256": sha(target)}


def arguments(site: str, q: int, width: int = 1280, height: int = 720) -> list[str]:
    recipe = FIXTURES / f"site-{site}.json"
    data = json.loads(recipe.read_text())
    x, z, w, h = data["crop_xzwh"]
    low, high = PALETTE_RANGES_M[site]
    return [
        "--headless", "--width", str(width), "--height", str(height),
        "--fluid25d-scenario", "natural-flow-study", "--fluid25d-solver", "finite-volume",
        "--fluid25d-natural-flow-recipe", str(recipe),
        "--fluid25d-natural-flow-source-m3-per-s", str(q),
        "--terrain-heightfield", str(ROOT / SITES[site]),
        "--fluid25d-terrain-crop-x", str(x), "--fluid25d-terrain-crop-z", str(z),
        "--grid-width", str(w), "--grid-height", str(h), "--fluid25d-cell-size-m", "30",
        "--fluid25d-fixed-delta-seconds", str(DT), "--fluid25d-substeps", "8",
        "--fluid25d-gravity-m-per-s2", "9.81", "--fluid25d-flow-damping-per-second", "0.15",
        "--fluid25d-view", "catchment", "--fluid25d-render-height-scale", "1",
        "--fluid25d-terrain-palette-low-m", str(low),
        "--fluid25d-terrain-palette-high-m", str(high),
    ]


def read_metrics(path: Path) -> dict[int, dict[tuple[str, str], float]]:
    frames: dict[int, dict] = {}
    with path.open() as stream:
        for row in csv.DictReader(stream):
            value = float(row["value"])
            if not math.isfinite(value):
                raise ValueError("profile contains a nonfinite metric")
            frame = frames.setdefault(int(row["frame_index"]), {})
            key = (row["category"], row["name"])
            if key in frame:
                raise ValueError("profile contains duplicate metric keys")
            frame[key] = value
    if not frames:
        raise ValueError("empty diagnostic profile")
    return frames


def summarize(frames: dict[int, dict], q: float) -> dict:
    times = sorted(frames)
    if times != list(range(FRAMES)):
        raise ValueError("profile does not span the frozen physical horizon")
    first, last = frames[times[0]], frames[times[-1]]
    water = lambda values, name: values[("fluid_25d.water", name)]
    bad_flags = [f for f in times if frames[f][("fluid_25d.solver", "finite_volume_status_flags")] != 0]
    residual = max(abs(water(frames[f], "conservation_residual_m3")) for f in times)
    source = water(last, "cumulative_source_volume_m3")
    tolerance = max(0.003, source * 3e-4)
    ledger_error = max(abs(sum(frames[f][(BOUNDARY, n)] for n in (EXPECTED, OTHER, CORNER, NONEDGE)) - water(frames[f], "cumulative_boundary_outflow_volume_m3")) for f in times)
    late_start = max(f for f in times if (f + 1) * DT <= FRAMES * DT - 600)
    late = frames[late_start]
    span = (times[-1] - late_start) * DT
    expected_rate = (last[(BOUNDARY, EXPECTED)] - late[(BOUNDARY, EXPECTED)]) / span
    all_rate = (water(last, "cumulative_boundary_outflow_volume_m3") - water(late, "cumulative_boundary_outflow_volume_m3")) / span
    other_rate = (last[(BOUNDARY, OTHER)] - late[(BOUNDARY, OTHER)]) / span
    arrivals = {}
    for name in GAUGES:
        category = f"fluid_25d.natural_flow.gauge.{name}"
        wet = [f for f in times if frames[f][(category, "center_depth_m")] >= 0.01]
        arrivals[name] = (wet[0] + 1) * DT if wet else None
    first_out = next(((f + 1) * DT for f in times if frames[f][(BOUNDARY, EXPECTED)] >= 1.0), None)
    negative_volume = any(water(frames[f], name) < 0 for f in times for name in ("total_water_volume_m3", "maximum_depth_m", "cumulative_source_volume_m3", "cumulative_sink_volume_m3", "cumulative_boundary_outflow_volume_m3"))
    numeric = not bad_flags and not negative_volume and residual <= tolerance and ledger_error <= 1e-4 and abs(source - q * FRAMES * DT) <= tolerance and water(last, "cumulative_sink_volume_m3") == 0 and last[(BOUNDARY, NONEDGE)] == 0
    late_wet = all(frames[f][(f"fluid_25d.natural_flow.gauge.{name}", "center_depth_m")] >= 0.01 for f in times if f >= late_start for name in GAUGES)
    sustained = numeric and late_wet and expected_rate >= q * 0.5 and all_rate >= q * 0.8
    first_sustained_end = None
    for index, f in enumerate(times):
        if f < 600 // DT:
            continue
        start = f - 600 // DT
        before = frames.get(start)
        if before is None:
            continue
        current = frames[f]
        window_expected_rate = (current[(BOUNDARY, EXPECTED)] - before[(BOUNDARY, EXPECTED)]) / 600
        window_all_rate = (water(current, "cumulative_boundary_outflow_volume_m3") - water(before, "cumulative_boundary_outflow_volume_m3")) / 600
        if window_expected_rate >= q * 0.5 and window_all_rate >= q * 0.8 and all(frames[t][(f"fluid_25d.natural_flow.gauge.{name}", "center_depth_m")] >= 0.01 for t in times[max(0, index-600//DT):index+1] for name in GAUGES):
            first_sustained_end = (f + 1) * DT
            break
    return {
        "numeric_health_checks_passed": numeric,
        "strict_cpu_gpu_parity": "not checked by this profile",
        "sustained_expected_route_checks_passed": sustained,
        "late_gauge_centres_materially_wet_throughout": late_wet,
        "first_measured_600s_sustained_window_end_seconds": first_sustained_end,
        "physical_horizon_seconds": FRAMES * DT,
        "front_definition": "first gauge centre depth >=0.01m; expected-window cumulative outflow >=1m3; diagnostic only",
        "gauge_front_arrival_seconds": arrivals,
        "first_expected_outflow_seconds": first_out,
        "late_window_seconds": [(late_start + 1) * DT, FRAMES * DT],
        "late_expected_outflow_m3_per_s": expected_rate,
        "late_total_boundary_outflow_m3_per_s": all_rate,
        "late_other_edge_outflow_m3_per_s": other_rate,
        "max_abs_water_residual_m3": residual,
        "water_ledger_tolerance_m3": tolerance,
        "max_abs_boundary_attribution_error_m3": ledger_error,
        "source_volume_m3": source,
        "stored_water_m3": water(last, "total_water_volume_m3"),
        "max_depth_final_m": water(last, "maximum_depth_m"),
        "slow_pooled_wet_fraction_final": water(last, "slow_pooled_wet_fraction"),
        "nonzero_solver_flag_frames": bad_flags,
    }


def gpu_timings(path: Path) -> dict:
    with path.open() as stream:
        values = [float(r["duration_ms"]) for r in csv.DictReader(stream) if r["kind"] == "gpu" and r["label"] == "fluid_25d solver"]
    values = values[10:]
    if not values or any(not math.isfinite(v) or v < 0 for v in values):
        raise ValueError("missing or malformed GPU solver timing")
    ordered = sorted(values)
    return {"samples_after_first_10": len(values), "mean_ms_per_2s_outer_step": statistics.mean(values), "p95_ms_per_2s_outer_step": ordered[int(0.95 * (len(ordered) - 1))], "scope": "GPU aggregate solve only,48x48 native30m,8substeps; excludes readback/render/encoding"}


def hydraulics(app: Path, out: Path) -> None:
    if out.exists() and any(out.iterdir()):
        raise RuntimeError(f"refusing to overwrite evidence: {out}")
    out.mkdir(parents=True, exist_ok=True)
    report = {"schema": "cubey.fluid25d.natural_flow_pilot.v1", "app_sha256": sha(app), "runner_sha256": sha(Path(__file__)), "frame_time": "profile frame_index f is state after (f+1)*2 physical seconds", "frozen_Q_m3_per_s": list(Q_VALUES), "initial_water": "dry", "all_edges": "outward-only; expected outlet is measurement,not a drain", "cases": []}
    (out / "frozen-inputs.json").write_text(json.dumps(report, indent=2) + "\n")
    for site in SITES:
        for q in Q_VALUES:
            label = f"site-{site}-q{q}"
            prefix = out / label
            args = arguments(site, q) + ["--frames", str(FRAMES), "--capture", "png", "--fluid25d-catchment-view", "water-isolation", "--fluid25d-natural-flow-home-pitch-radians", "-1.55", "--profile-output", str(prefix), "--profile-diagnostics", "--profile-diagnostic-interval", str(PROFILE_INTERVAL), "--output", str(out / f"{label}-final.png")]
            execution = run(app, args, out / f"{label}.log")
            case = {"site": site, "q_m3_per_s": q, "execution": execution, "command": [str(app), *args], "recipe_sha256": sha(FIXTURES / f"site-{site}.json"), "metrics": None}
            if execution["exit_code"] == 0:
                try:
                    case["metrics"] = summarize(read_metrics(prefix.with_suffix(".metrics.csv")), q)
                    case["gpu_timing"] = gpu_timings(prefix.with_suffix(".passes.csv"))
                except (ValueError, KeyError, OSError) as error:
                    case["error"] = str(error)
            report["cases"].append(case)
            (out / "hydraulics.json").write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps({"case": label, "execution": execution, "metrics": case["metrics"]}), flush=True)
            if execution["exit_code"]:
                raise RuntimeError(f"hydraulic command failed; evidence retained: {execution['log']}")
            if case.get("error") or not case["metrics"]["numeric_health_checks_passed"]:
                raise RuntimeError(f"hydraulic validation failed; evidence retained: {out / 'hydraulics.json'}")
    if sha(app) != report["app_sha256"]:
        raise RuntimeError("app changed during the frozen hydraulic matrix")


def evidence(app: Path, out: Path, winner: str) -> None:
    report = json.loads((out / "hydraulics.json").read_text())
    if sha(app) != report["app_sha256"]:
        raise RuntimeError("app differs from hydraulic evidence; do not mix builds")
    site, q_text = winner.split(":")
    q = int(q_text)
    case = next(c for c in report["cases"] if c["site"] == site and c["q_m3_per_s"] == q)
    if sha(FIXTURES / f"site-{site}.json") != case["recipe_sha256"]:
        raise RuntimeError("recipe differs from hydraulic evidence; do not mix inputs")
    if not case["metrics"] or not case["metrics"]["sustained_expected_route_checks_passed"]:
        raise RuntimeError("winner must clear the fixed sustained-flow and numerical profile gates")
    selected = {"winner": winner, "hydraulic_app_sha256": report["app_sha256"], "runner_sha256": sha(Path(__file__)), "video_physical_seconds": FRAMES * DT, "video_fps": FPS, "playback_acceleration": DT * FPS, "captures": []}
    common = arguments(site, q)
    # Choose timing from the hydraulic measurements before observing dye visuals.
    established = case["metrics"]["first_measured_600s_sustained_window_end_seconds"]
    if established is None:
        raise RuntimeError("no measured sustained window precedes dye")
    dye_start = math.ceil(max(3600, established + 300) / 60) * 60
    if dye_start + 120 >= FRAMES * DT:
        raise RuntimeError("no dye observation time remains inside the frozen horizon")
    selected["dye_pulse_physical_seconds"] = [dye_start, dye_start + 120]
    dye = ["--fluid25d-dye-pulse-start-seconds", str(dye_start), "--fluid25d-dye-pulse-duration-seconds", "120"]
    for label, view, pitch, extra in (
        ("flow-top-down", "water-isolation", "-1.55", []),
        ("flow-oblique", "composite", "-0.72", []),
        ("dye-top-down", "transport-inspection", "-1.55", dye),
        ("dye-oblique", "transport-inspection", "-0.72", dye),
    ):
        args = common + ["--frames", str(FRAMES), "--capture", "video", "--fps", str(FPS), "--fluid25d-catchment-view", view, "--fluid25d-natural-flow-home-pitch-radians", pitch, *extra, "--output", str(out / f"{label}.mp4")]
        execution = run(app, args, out / f"{label}.log")
        record = {"label": label, "execution": execution, "command": [str(app), *args]}
        selected["captures"].append(record)
        (out / "evidence.json").write_text(json.dumps(selected, indent=2) + "\n")
        if execution["exit_code"]:
            raise RuntimeError(f"capture failed: {label}")
        record["labelled_video"] = label_video(out / f"{label}.mp4", q, out)
        (out / "evidence.json").write_text(json.dumps(selected, indent=2) + "\n")
    for label, frames, extra in (("startup-oracle", 30, []), ("dye-long-oracle", FRAMES, dye)):
        args = common + ["--frames", str(frames), "--capture", "png", "--fluid25d-catchment-view", "transport-inspection" if extra else "water-isolation", *extra, "--fluid25d-gpu-oracle-validation", "--profile-output", str(out / label), "--profile-diagnostics", "--profile-diagnostic-interval", "1", "--output", str(out / f"{label}.png")]
        execution = run(app, args, out / f"{label}.log")
        selected.setdefault("strict_oracle_checks", []).append({"label": label, "physical_seconds": frames * DT, "execution": execution, "command": [str(app), *args]})
        (out / "evidence.json").write_text(json.dumps(selected, indent=2) + "\n")
        if execution["exit_code"]:
            raise RuntimeError(f"strict oracle failed; evidence retained: {out / 'evidence.json'}")
    if sha(app) != report["app_sha256"]:
        raise RuntimeError("app changed during the frozen winner evidence replay")
    print(json.dumps(selected, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("hydraulics", "evidence"), required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--app", type=Path, default=ROOT / "build/dev/projects/fluid/fluid_25d/fluid_25d")
    parser.add_argument("--winner", help="review-selected site:Q, for example a:30; evidence phase only")
    args = parser.parse_args()
    out, app = args.output_dir.resolve(), args.app.resolve()
    if not app.is_file():
        raise SystemExit("missing Fluid app")
    if args.phase == "evidence":
        if not args.winner:
            raise SystemExit("evidence phase requires a reviewed --winner site:Q")
        evidence(app, out, args.winner)
    else:
        if args.winner:
            raise SystemExit("winner is chosen after the hydraulic matrix")
        hydraulics(app, out)


if __name__ == "__main__":
    main()
