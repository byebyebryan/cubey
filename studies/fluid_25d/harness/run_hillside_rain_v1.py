#!/usr/bin/env python3
"""Bounded continuous-rain evidence on the existing immutable mountain crop.

Uniform rain is a new forcing experiment, not a replay of point-source physics.
Numerical health, network support, offscreen visual review, and human desktop
acceptance are separate gates. No existing evidence is overwritten.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from decimal import Decimal
import json
from pathlib import Path
from typing import Any

import run_hillside_sustained_flow_v2 as v2
import run_hillside_motion_v4 as media
import run_hillside_readability_v5 as v5

ROOT = v2.ROOT
SPEC = Path(__file__).resolve().parent / "fixtures/hillside-rain-study-v1/temperate-mountain-rain.json"
BASELINE_CLOSURE = ROOT / "outputs/fluid/hillside-readability-v5-20261001-o0PsLm/final-local-closure.json"
RAIN = "fluid_25d.rain"
WATER = "fluid_25d.water"
FLAGS = ("fluid_25d.solver", "finite_volume_status_flags")
DT = 2
AREA = Decimal(512 * 512 * 30 * 30)
ROI_AREA = Decimal(21 * 21 * 30 * 30)
RATES = {"equal-input": Decimal("1.52587890625"), "main": Decimal("12")}
REGIONS = ("upper", "transit", "collection")
RAIN_FIELDS = (
    "physical_time_s", "rate_mm_per_hour", "total_input_m3_per_s",
    "cumulative_depth_m", "scheduled_volume_m3", "enabled", "material_wet_cells",
    "material_active_cells", "converged_water_volume_m3", "converged_moving_cells",
    "largest_corridor_cells", "largest_corridor_span_m",
)
REGION_FIELDS = (
    "valid", "water_volume_m3", "direct_rain_volume_m3", "net_lateral_storage_m3",
    "maximum_depth_m", "material_wet_cells", "active_cells",
)
CAPTURE_CASES = (
    ("overview-topdown", "overview", "-1.55", "composite"),
    ("valley-oblique", "travel", "-0.72", "composite"),
    ("collection-oblique", "collection", "-0.72", "composite"),
)
WATER_FIELDS = (
    "cumulative_source_volume_m3", "cumulative_sink_volume_m3",
    "cumulative_boundary_outflow_volume_m3", "total_water_volume_m3", "conservation_residual_m3",
)


def sha(path: Path) -> str:
    return v2.sha256_file(path)


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text())
    if not isinstance(value, dict):
        raise ValueError(f"report is not an object: {path}")
    return value


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def new_phase(out: Path, phase: str) -> Path:
    out.mkdir(parents=True, exist_ok=True)
    folder = out / phase
    folder.mkdir()  # Fresh phases and cases only; never overwrite old receipts.
    return folder


def identity(app: Path) -> dict[str, Any]:
    spec = read_json(SPEC)
    fixed = {"schema": "cubey.fluid25d.hillside_rain_study.v1", "crop_xzwh": [1152, 1408, 512, 512],
             "cell_size_m": 30, "fixed_delta_seconds": DT, "substeps": 16,
             "gravity_m_per_s2": 9.81, "damping_per_second": 0.15,
             "rates_mm_per_hour": {name: str(rate) for name, rate in RATES.items()},
             "primary_horizon_seconds": 7200, "conditional_main_extension_seconds": 14400,
             "material_depth_m": "0.01", "active_speed_m_per_s": "0.02",
             "convergence_depth_above_direct_rain_m": "0.01", "corridor_edge_exclusion_m": 120,
             "corridor_minimum_span_m": 300, "corridor_support_duration_seconds": 900,
             "regions_xzwh": {"upper": [203, 203, 21, 21], "transit": [128, 224, 21, 21],
                               "collection": [48, 241, 21, 21]}}
    if any(spec.get(key) != value for key, value in fixed.items()):
        raise ValueError("rain experiment differs from the predefined protocol")
    manifest_path = ROOT / spec["manifest"]
    if sha(manifest_path) != spec["manifest_sha256"]:
        raise ValueError("terrain manifest changed")
    manifest = read_json(manifest_path)
    elevation = manifest["files"]["elevation"]
    if (manifest.get("schema") != "cubey.terrain.heightfield.v1" or
            manifest["grid"]["sample_spacing_m"] != 30 or
            elevation["sha256"] != spec["elevation_sha256"] or
            sha(manifest_path.parent / elevation["path"]) != spec["elevation_sha256"]):
        raise ValueError("immutable native terrain changed")
    shaders = v2.shader_map_identity(app.parent / "shaders")
    solver_sources = media._current_solver_source_hashes()
    solver_shaders = {name: shaders["files"][name] for name in media.SOLVER_SPIRV_MODULES}
    baseline = read_json(BASELINE_CLOSURE)["input_identity"]
    if (solver_sources != baseline["solver_source_hashes"] or
            solver_shaders != baseline["solver_spirv_hashes"]):
        raise ValueError("finite-volume solver changed from the sealed V5 checkpoint")
    return {
        "app_sha256": sha(app), "shader_map_sha256": shaders["shader_map_sha256"],
        "shader_hashes": shaders["files"], "solver_source_hashes": solver_sources,
        "solver_spirv_hashes": solver_shaders, "spec_sha256": sha(SPEC),
        "runner_sha256": sha(Path(__file__)), "manifest_sha256": sha(manifest_path),
        "elevation_sha256": elevation["sha256"],
        "transformed_crop_sha256": spec["transformed_crop_sha256"],
        "retained_solver_checkpoint_sha256": sha(BASELINE_CLOSURE),
    }


def arguments(rate: Decimal, *, headless: bool = True, camera: str = "overview",
              view: str = "composite", markers: bool = False) -> list[str]:
    if rate not in RATES.values():
        raise ValueError("only the two predefined rainfall rates are part of this study")
    if camera not in ("overview", "travel", "collection"):
        raise ValueError("rain cameras are not source-relative")
    if view not in ("composite", "water-isolation", "flow-inspection"):
        raise ValueError("unsupported rain review view")
    args = (["--headless"] if headless else []) + [
        "--width", "1280", "--height", "720",
        "--fluid25d-scenario", "hillside-rain-study", "--fluid25d-solver", "finite-volume",
        "--terrain-heightfield", str(ROOT / read_json(SPEC)["manifest"]),
        "--fluid25d-terrain-crop-x", "1152", "--fluid25d-terrain-crop-z", "1408",
        "--grid-width", "512", "--grid-height", "512", "--fluid25d-cell-size-m", "30",
        "--fluid25d-fixed-delta-seconds", "2", "--fluid25d-substeps", "16",
        "--fluid25d-gravity-m-per-s2", "9.81", "--fluid25d-flow-damping-per-second", "0.15",
        "--fluid25d-rainfall-rate-mm-per-hour", str(rate),
        "--fluid25d-view", "catchment", "--fluid25d-catchment-view", view,
        "--fluid25d-render-height-scale", "1", "--fluid25d-terrain-palette-low-m", "770",
        "--fluid25d-terrain-palette-high-m", "2274", "--fluid25d-hillside-camera", camera,
        "--fluid25d-hillside-depth-cues", "--fluid25d-terrain-thin-water-composite",
    ]
    if markers:
        args += ["--fluid25d-motion-markers", "--fluid25d-motion-marker-mode", "local"]
    return args


def profile_arguments(folder: Path, rate: Decimal, horizon: int) -> list[str]:
    return arguments(rate) + ["--frames", str(horizon // DT), "--capture", "png",
                             "--output", str(folder / "final.png"), "--profile-diagnostics",
                             "--profile-diagnostic-interval", "1", "--profile-output", str(folder / "profile")]


def capture_arguments(folder: Path, camera: str, pitch: str, view: str, horizon: int) -> list[str]:
    return arguments(RATES["main"], camera=camera, view=view, markers=True) + [
        "--frames", str(horizon // DT), "--capture", "video", "--fps", "30", "--output", str(folder / "raw.mp4"),
        "--fluid25d-natural-flow-home-pitch-radians", pitch, "--profile-diagnostics",
        "--profile-diagnostic-interval", str(horizon // DT - 1), "--profile-output", str(folder / "profile"),
    ]


def expected_artifacts(output: str) -> set[str]:
    return {"child.log", output, *("profile" + suffix for suffix in
            (".frames.csv", ".passes.csv", ".metrics.csv", ".summary.txt", ".trace.json"))}


def validate_profile(frames: dict[int, dict[tuple[str, str], Decimal]], rate: Decimal,
                     horizon: int) -> dict[str, Any]:
    if horizon <= 0 or horizon % DT or sorted(frames) != list(range(horizon // DT)):
        raise ValueError("profile must contain every fixed-step frame exactly once")
    required = {FLAGS, *((WATER, name) for name in WATER_FIELDS),
                *((RAIN, name) for name in RAIN_FIELDS),
                *((f"{RAIN}.region.{region}", name) for region in REGIONS for name in REGION_FIELDS),
                *(("fluid_25d.hydraulic_identity", name) for name in ("hash_hi_u32", "hash_lo_u32"))}
    maximum_residual = Decimal(0)
    previous_source = previous_export = Decimal(0)
    longest = streak = 0
    snapshots: dict[str, Any] = {}
    for frame in range(horizon // DT):
        values = frames[frame]
        if missing := required - values.keys():
            raise ValueError(f"incomplete rain profile at frame {frame}: {sorted(missing)}")
        if any(not value.is_finite() for value in values.values()):
            raise ValueError(f"nonfinite profile value at frame {frame}")
        get = lambda name: values[(RAIN, name)]
        t = Decimal((frame + 1) * DT)
        depth = rate * t / Decimal(3600000)
        expected_input = AREA * depth
        # Nominal public rate -> float32 native depth source -> six-decimal CSV.
        # This 1 ppm input bound is distinct from unchanged water conservation.
        input_rounding = max(Decimal("0.003"), expected_input * Decimal("0.000001"))
        if values[FLAGS] != 0 or get("enabled") != 1 or get("physical_time_s") != t:
            raise ValueError(f"solver, continuous-rain state or clock failed at frame {frame}")
        if (abs(get("rate_mm_per_hour") - rate) > Decimal("0.000001") or
                abs(get("total_input_m3_per_s") - AREA * rate / Decimal(3600000)) > Decimal("0.001") or
                abs(get("cumulative_depth_m") - depth) > Decimal("0.0000006") or
                abs(get("scheduled_volume_m3") - expected_input) > input_rounding):
            raise ValueError(f"rain supply schedule failed at frame {frame}")
        source, sink, export, stored, reported = [values[(WATER, key)] for key in WATER_FIELDS]
        residual = stored - source + sink + export
        if (abs(source - expected_input) > input_rounding or sink != 0 or
                min(source, sink, export, stored) < 0 or
                source < previous_source or export < previous_export or
                abs(residual - reported) > Decimal("0.000003") or
                max(abs(residual), abs(reported)) > max(Decimal("0.003"), source * Decimal("0.0003"))):
            raise ValueError(f"water ledger failed at frame {frame}")
        maximum_residual = max(maximum_residual, abs(residual), abs(reported))
        previous_source, previous_export = source, export
        for field in ("material_wet_cells", "material_active_cells", "converged_moving_cells", "largest_corridor_cells"):
            if not 0 <= get(field) <= 512 * 512 or get(field) != get(field).to_integral_value():
                raise ValueError(f"invalid {field} at frame {frame}")
        if (get("material_active_cells") > get("material_wet_cells") or
                get("largest_corridor_cells") > get("converged_moving_cells") or
                not 0 <= get("converged_water_volume_m3") <= stored + Decimal("0.001") or
                not 0 <= get("largest_corridor_span_m") <= 15330):
            raise ValueError(f"inconsistent convergence observations at frame {frame}")
        streak = streak + DT if get("largest_corridor_span_m") >= 300 else 0
        longest = max(longest, streak)
        for region in REGIONS:
            category = f"{RAIN}.region.{region}"
            roi = lambda key: values[(category, key)]
            direct = ROI_AREA * depth
            if (roi("valid") != 1 or abs(roi("direct_rain_volume_m3") - direct) > max(Decimal("0.003"), direct * Decimal("0.000001")) or
                    abs(roi("net_lateral_storage_m3") - roi("water_volume_m3") + roi("direct_rain_volume_m3")) > Decimal("0.000003") or
                    roi("water_volume_m3") < 0 or roi("maximum_depth_m") < 0 or
                    not 0 <= roi("active_cells") <= roi("material_wet_cells") <= 441 or
                    any(roi(key) != roi(key).to_integral_value() for key in ("active_cells", "material_wet_cells"))):
                raise ValueError(f"fixed {region} region failed at frame {frame}")
        if int(t) in (120, 600, 1800, 3600, 7200, 10800, 14400):
            snapshots[str(t)] = {f"{cat}.{key}": str(value) for (cat, key), value in values.items()
                                 if cat == RAIN or cat.startswith(RAIN + ".region.") or cat == WATER}
    final = frames[horizon // DT - 1]
    gathering = [region for region in REGIONS
                 if final[(f"{RAIN}.region.{region}", "net_lateral_storage_m3")] >
                 max(Decimal(1), final[(f"{RAIN}.region.{region}", "direct_rain_volume_m3")] * Decimal("0.1"))]
    return {
        "numerical_gate_passed": True, "frames_checked": horizon // DT, "horizon_seconds": horizon,
        "nominal_rain_rate_mm_per_hour": str(rate), "maximum_absolute_water_residual_m3": str(maximum_residual),
        "final_input_m3": str(previous_source), "final_export_m3": str(previous_export),
        "longest_continuous_corridor_support_seconds": longest, "gathering_regions": gathering,
        "network_support_gate_passed": longest >= 900 and bool(gathering), "snapshots": snapshots,
        "interpretation": "Corridor support is interior D4-connected moving water at least 1 cm above direct rain depth; region excess is net lateral storage, not gross inflow. Numerical support is not visual acceptance.",
    }


def compare_prefix(short: dict, extended: dict, *, boundary: str = "Uninterrupted four-hour replay from dry start, exact two-hour prefix; not GPU checkpoint restoration.") -> dict[str, Any]:
    count = 0
    for frame, values in short.items():
        old = {key: value for key, value in values.items() if not v5._is_timing_metric(key)}
        new = {key: value for key, value in extended.get(frame, {}).items() if not v5._is_timing_metric(key)}
        if old != new:
            raise ValueError(f"exact non-timing replay changed the physical prefix at frame {frame}")
        count += len(old)
    return {"passed": True, "frames": len(short), "exact_non_timing_values": count,
            "boundary": boundary}


def run_case(app: Path, folder: Path, label: str, args: list[str]) -> dict[str, Any]:
    folder.mkdir()
    before = identity(app)
    command = ["rtk", "proxy", str(app), *args]
    run = media._run_command(command, folder / "child.log")
    record = {"label": label, "command": command, **run, "before": before, "after": identity(app)}
    record["artifacts"] = {path.name: {"path": str(path.resolve()), "sha256": sha(path)}
                           for path in folder.iterdir() if path.is_file()}
    write_json(folder / "execution.json", record)
    if run["exit_code"] != 0 or before != record["after"]:
        raise ValueError(f"{label} child failed or execution inputs changed")
    log = (folder / "child.log").read_text()
    if read_json(SPEC)["transformed_crop_sha256"] not in log:
        raise ValueError(f"{label} child did not attest the pinned runtime terrain crop")
    return record


def profile_case(app: Path, folder: Path, label: str, rate: Decimal, horizon: int) -> dict[str, Any]:
    run_case(app, folder, label, profile_arguments(folder, rate, horizon))
    frames = v2._read_metrics(folder / "profile.metrics.csv", decimal_values=True)
    return {"label": label, "rate_mm_per_hour": str(rate), "horizon_seconds": horizon,
            "execution_receipt": {"path": str((folder / "execution.json").resolve()), "sha256": sha(folder / "execution.json")},
            "summary": validate_profile(frames, rate, horizon)}


def profiles_phase(app: Path, out: Path, *, smoke: bool = False) -> dict[str, Any]:
    phase = "smoke" if smoke else "profiles"
    folder = new_phase(out, phase)
    report: dict[str, Any] = {"schema": "cubey.fluid25d.hillside_rain_v1.profiles", "phase": phase,
                              "phase_checks_passed": False, "input_identity": identity(app), "cases": []}
    write_json(folder / "profiles.json", report)
    try:
        # Independent correctness jobs may overlap; their timings are not an
        # isolated performance benchmark. Pacing remains a later serial phase.
        with ThreadPoolExecutor(max_workers=1 if smoke else 2) as workers:
            jobs = [workers.submit(profile_case, app, folder / name, name, rate, 120 if smoke else 7200)
                    for name, rate in RATES.items()]
            for job in jobs:
                report["cases"].append(job.result())
                write_json(folder / "profiles.json", report)
        if not smoke and not report["cases"][-1]["summary"]["network_support_gate_passed"]:
            extended = profile_case(app, folder / "main-extended", "main-extended", RATES["main"], 14400)
            short = v2._read_metrics(folder / "main/profile.metrics.csv", decimal_values=True)
            long = v2._read_metrics(folder / "main-extended/profile.metrics.csv", decimal_values=True)
            extended["two_hour_prefix"] = compare_prefix(short, long)
            report["cases"].append(extended)
        if identity(app) != report["input_identity"]:
            raise ValueError("inputs changed across rainfall profiles")
        report["phase_checks_passed"] = True
        report["selected_main_case"] = report["cases"][-1]["label"]
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        write_json(folder / "profiles.json", report)
        raise
    write_json(folder / "profiles.json", report)
    return report


def checked_execution(record: dict[str, Any], expected_folder: Path, app: Path,
                      expected_args: list[str], output: str) -> dict[str, Any]:
    receipt = record["execution_receipt"]
    path = expected_folder / "execution.json"
    if receipt["path"] != str(path.resolve()) or receipt["sha256"] != sha(path):
        raise ValueError("execution receipt changed or moved")
    execution = read_json(path)
    if (execution["exit_code"] != 0 or execution["before"] != identity(app) or execution["after"] != execution["before"] or
            execution["command"] != ["rtk", "proxy", str(app), *expected_args] or
            execution["label"] != record["label"] or
            set(execution["artifacts"]) != expected_artifacts(output)):
        raise ValueError("execution inputs changed or child did not succeed")
    for name, artifact in execution["artifacts"].items():
        target = expected_folder / name
        if artifact["path"] != str(target.resolve()) or artifact["sha256"] != sha(target):
            raise ValueError("execution artifact changed or moved")
    return execution


def checked_profiles(app: Path, out: Path) -> dict[str, Any]:
    report = read_json(out / "profiles/profiles.json")
    if (report.get("phase_checks_passed") is not True or report["input_identity"] != identity(app) or
            [case["label"] for case in report["cases"]] not in (["equal-input", "main"], ["equal-input", "main", "main-extended"])):
        raise ValueError("complete unchanged rainfall profiles required")
    for case in report["cases"]:
        folder = out / "profiles" / case["label"]
        rate, horizon = Decimal(case["rate_mm_per_hour"]), case["horizon_seconds"]
        expected = RATES["main" if case["label"] == "main-extended" else case["label"]]
        if rate != expected or horizon != (14400 if case["label"] == "main-extended" else 7200):
            raise ValueError("profile no longer uses the predefined experiment")
        checked_execution(case, folder, app, profile_arguments(folder, rate, horizon), "final.png")
        frames = v2._read_metrics(folder / "profile.metrics.csv", decimal_values=True)
        if validate_profile(frames, rate, horizon) != case["summary"]:
            raise ValueError("retained profile summary differs from independently recomputed data")
        if case["label"] == "main-extended":
            short = v2._read_metrics(out / "profiles/main/profile.metrics.csv", decimal_values=True)
            if compare_prefix(short, frames) != case.get("two_hour_prefix"):
                raise ValueError("extended prefix receipt differs")
    if report["selected_main_case"] != report["cases"][-1]["label"]:
        raise ValueError("selected main case differs from the fixed extension policy")
    if (len(report["cases"]) == 3) == report["cases"][1]["summary"]["network_support_gate_passed"]:
        raise ValueError("four-hour extension does not follow the predefined policy")
    return report


def compatibility_arguments(folder: Path) -> tuple[list[str], Path]:
    baseline = BASELINE_CLOSURE.parent / "source-compatibility-final"
    old_receipt = baseline / "review.json"
    closure = read_json(BASELINE_CLOSURE)
    if sha(old_receipt) != closure["artifact_receipts"]["source-compatibility-final/review.json"]["sha256"]:
        raise ValueError("sealed point-source compatibility receipt changed")
    receipt = read_json(old_receipt)
    old_metrics = baseline / "profile.metrics.csv"
    if sha(old_metrics) != receipt["artifact_hashes"]["profile.metrics.csv"]:
        raise ValueError("sealed point-source metrics changed")
    args = receipt["command"][3:]
    for flag, target in (("--profile-output", folder / "profile"), ("--output", folder / "final.png")):
        args[args.index(flag) + 1] = str(target)
    return args, old_metrics


def compatibility_phase(app: Path, out: Path) -> dict[str, Any]:
    folder = new_phase(out, "point-source-compatibility")
    args, baseline = compatibility_arguments(folder / "run")
    recipe = v2.pinned_inputs(512, app)
    run_case(app, folder / "run", "point-source-compatibility", args)
    if recipe != v2.pinned_inputs(512, app):
        raise ValueError("point-source recipe or terrain changed during compatibility replay")
    old = v2._read_metrics(baseline, decimal_values=True)
    new = v2._read_metrics(folder / "run/profile.metrics.csv", decimal_values=True)
    if sorted(old) != list(range(64)) or new.keys() != old.keys():
        raise ValueError("point-source compatibility cadence changed")
    comparison = compare_prefix(old, new, boundary="Retained 64-step point-source replay, not a four-hour extension.")
    result = {"passed": True, "input_identity": identity(app), "recipe_identity": recipe,
              "baseline_metrics_sha256": sha(baseline), "comparison": comparison,
              "scope": "Exact non-timing metrics including hydraulic and source-marker identities for the retained 64-step point-source command, not full-horizon solver parity.",
              "execution_receipt": {"path": str((folder / "run/execution.json").resolve()),
                                    "sha256": sha(folder / "run/execution.json")},
              "label": "point-source-compatibility"}
    write_json(folder / "compatibility.json", result)
    return result


def checked_compatibility(app: Path, out: Path) -> dict[str, Any]:
    folder = out / "point-source-compatibility"
    report = read_json(folder / "compatibility.json")
    args, baseline = compatibility_arguments(folder / "run")
    checked_execution(report, folder / "run", app, args, "final.png")
    old = v2._read_metrics(baseline, decimal_values=True)
    new = v2._read_metrics(folder / "run/profile.metrics.csv", decimal_values=True)
    if sorted(old) != list(range(64)) or new.keys() != old.keys():
        raise ValueError("point-source compatibility cadence changed")
    if (report.get("passed") is not True or report["input_identity"] != identity(app) or
            report["recipe_identity"] != v2.pinned_inputs(512, app) or report["baseline_metrics_sha256"] != sha(baseline) or
            report["comparison"] != compare_prefix(old, new, boundary="Retained 64-step point-source replay, not a four-hour extension.")):
        raise ValueError("point-source compatibility receipt differs from current evidence")
    return report


def video_filter(camera: str) -> str:
    return ",".join([
        media._drawtext_clause(f"Mountain rain: 12 mm/h | {camera} | no point source or drain", 14),
        media._drawtext_clause(r"Physical time %{eif\:n*2+2\:d} s | 60x encoded playback", 44),
        media._drawtext_clause("Depth: fixed 0.01 / 0.1 / 1 / 10 m | dots are local motion cues, not rain", 74),
    ])


def encoding_command(folder: Path, camera: str, horizon: int) -> list[str]:
    return ["rtk", "proxy", "ffmpeg", "-hide_banner", "-loglevel", "error", "-n", "-i", str(folder / "raw.mp4"),
            "-vf", video_filter(camera), "-frames:v", str(horizon // DT), "-c:v", "libx264",
            "-preset", "veryfast", "-crf", "18", "-threads", "2", "-pix_fmt", "yuv420p", str(folder / "labelled.mp4")]


def still_command(folder: Path, seconds: int) -> list[str]:
    return ["rtk", "proxy", "ffmpeg", "-hide_banner", "-loglevel", "error", "-n", "-i", str(folder / "labelled.mp4"),
            "-vf", f"select=eq(n\\,{seconds // DT - 1})", "-frames:v", "1", str(folder / f"t{seconds:05d}.png")]


def comparison_command(out: Path, name: str) -> list[str]:
    title = f"Mountain rain: {RATES[name]} mm/h | physical time 120 min | identical overview camera"
    return ["rtk", "proxy", "ffmpeg", "-hide_banner", "-loglevel", "error", "-n",
            "-i", str(out / "profiles" / name / "final.png"), "-vf", media._drawtext_clause(title, 14),
            "-frames:v", "1", str(out / "captures" / f"{name}-120min.png")]


def captures_phase(app: Path, out: Path) -> dict[str, Any]:
    profiles = checked_profiles(app, out)
    main = profiles["cases"][-1]
    horizon = main["horizon_seconds"]
    folder = new_phase(out, "captures")
    report: dict[str, Any] = {"schema": "cubey.fluid25d.hillside_rain_v1.captures", "phase_checks_passed": False,
                              "profiles_sha256": sha(out / "profiles/profiles.json"), "cases": [], "comparisons": []}
    baseline = v2._read_metrics(out / "profiles" / main["label"] / "profile.metrics.csv", decimal_values=True)
    try:
        for name in RATES:
            target = folder / f"{name}-120min.png"
            labelled = media._run_command(comparison_command(out, name), folder / f"{name}-comparison.log")
            if labelled["exit_code"] != 0:
                raise ValueError("matched rainfall comparison label failed")
            v5._validate_png(target)
            report["comparisons"].append({"case": name, "path": str(target.resolve()), "sha256": sha(target),
                                           "physical_seconds": 7200, "source_frame": 3599, "encoding": labelled})
        write_json(folder / "captures.json", report)
        for label, camera, pitch, view in CAPTURE_CASES:
            case_dir = folder / label
            raw = case_dir / "raw.mp4"
            run_case(app, case_dir, label, capture_arguments(case_dir, camera, pitch, view, horizon))
            frames = v2._read_metrics(case_dir / "profile.metrics.csv", decimal_values=True)
            compared = 0
            for frame in (0, horizon // DT - 1):
                expected = {key: value for key, value in baseline[frame].items() if not v5._is_timing_metric(key)}
                if any(frames.get(frame, {}).get(key) != value for key, value in expected.items()):
                    raise ValueError(f"capture changes numerical profile at frame {frame}: {label}")
                compared += len(expected)
            raw_probe = media.probe_video(raw, case_dir / "raw.ffprobe.log", horizon // DT)
            labelled = case_dir / "labelled.mp4"
            command = encoding_command(case_dir, camera, horizon)
            encoded = media._run_command(command, case_dir / "label.log")
            if encoded["exit_code"] != 0:
                raise ValueError("label encoding failed")
            probe = media.probe_video(labelled, case_dir / "labelled.ffprobe.log", horizon // DT)
            stills = []
            for seconds in (600, 3600, 7200, 14400):
                if seconds > horizon:
                    continue
                png = case_dir / f"t{seconds:05d}.png"
                extracted = media._run_command(still_command(case_dir, seconds),
                                               case_dir / f"t{seconds:05d}.log")
                if extracted["exit_code"] != 0:
                    raise ValueError("exact-time video frame extraction failed")
                v5._validate_png(png)
                stills.append({"path": str(png.resolve()), "sha256": sha(png), "physical_seconds": seconds,
                               "source_frame": seconds // DT - 1, "extraction": extracted})
            report["cases"].append({"label": label, "horizon_seconds": horizon,
                                     "execution_receipt": {"path": str((case_dir / "execution.json").resolve()), "sha256": sha(case_dir / "execution.json")},
                                     "raw_probe": raw_probe, "label_encoding": encoded, "labelled_probe": probe,
                                     "labelled_path": str(labelled.resolve()), "labelled_sha256": sha(labelled),
                                     "exact_numeric_values_at_first_and_last_frames": compared, "stills": stills})
            write_json(folder / "captures.json", report)
        report["phase_checks_passed"] = True
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        write_json(folder / "captures.json", report)
        raise
    write_json(folder / "captures.json", report)
    return report


def review_phase(app: Path, out: Path, *, write: bool = True) -> dict[str, Any]:
    import run_hillside_rain_pacing_v1 as pacing
    pacing.checked(app, out)
    checked_compatibility(app, out)
    profiles = checked_profiles(app, out)
    captures = read_json(out / "captures/captures.json")
    if (captures.get("phase_checks_passed") is not True or
            captures["profiles_sha256"] != sha(out / "profiles/profiles.json") or
            [case["label"] for case in captures["cases"]] != ["overview-topdown", "valley-oblique", "collection-oblique"]):
        raise ValueError("complete matched captures required")
    baseline = v2._read_metrics(out / "profiles" / profiles["cases"][-1]["label"] / "profile.metrics.csv", decimal_values=True)
    horizon = profiles["cases"][-1]["horizon_seconds"]
    if [item["case"] for item in captures["comparisons"]] != list(RATES):
        raise ValueError("matched two-hour rain comparison stills missing")
    for item in captures["comparisons"]:
        target = out / "captures" / f"{item['case']}-120min.png"
        encoded = item["encoding"]
        log = out / "captures" / f"{item['case']}-comparison.log"
        if (item["path"] != str(target.resolve()) or item["sha256"] != sha(target) or
                item["physical_seconds"] != 7200 or item["source_frame"] != 3599 or encoded["exit_code"] != 0 or
                encoded["command"] != comparison_command(out, item["case"]) or
                encoded["log_path"] != str(log.resolve()) or encoded["log_sha256"] != sha(log)):
            raise ValueError("matched rainfall comparison receipt differs")
        v5._validate_png(target)
    for case, (label, camera, pitch, view) in zip(captures["cases"], CAPTURE_CASES):
        folder = out / "captures" / case["label"]
        if case["horizon_seconds"] != horizon:
            raise ValueError("capture horizon differs from the selected passing profile")
        checked_execution(case, folder, app, capture_arguments(folder, camera, pitch, view, horizon), "raw.mp4")
        frames = v2._read_metrics(folder / "profile.metrics.csv", decimal_values=True)
        compared = 0
        for frame in (0, horizon // DT - 1):
            expected = {key: value for key, value in baseline[frame].items() if not v5._is_timing_metric(key)}
            if any(frames.get(frame, {}).get(key) != value for key, value in expected.items()):
                raise ValueError("capture does not match first/last numerical profile")
            compared += len(expected)
        if compared != case["exact_numeric_values_at_first_and_last_frames"]:
            raise ValueError("capture numerical comparison receipt changed")
        for probe_name in ("raw_probe", "labelled_probe"):
            probe = case[probe_name]
            basename = "raw" if probe_name == "raw_probe" else "labelled"
            v5._validate_probe_binding(probe, folder / f"{basename}.ffprobe.log", case["label"])
            if (probe["path"] != str((folder / f"{basename}.mp4").resolve()) or
                    sha(Path(probe["path"])) != probe["file_sha256"] or not probe["validated"] or
                    probe["expected_frames"] != horizon // DT or probe["frame_count"] != horizon // DT):
                raise ValueError("video changed after probing")
        if (case["labelled_path"] != str((folder / "labelled.mp4").resolve()) or
                sha(folder / "labelled.mp4") != case["labelled_sha256"]):
            raise ValueError("labelled capture changed or moved")
        encoded = case["label_encoding"]
        if (encoded["command"] != encoding_command(folder, camera, horizon) or encoded["exit_code"] != 0 or
                encoded["log_path"] != str((folder / "label.log").resolve()) or
                sha(folder / "label.log") != encoded["log_sha256"]):
            raise ValueError("video labels differ from the fixed physical clock or encoding receipt")
        if [item["physical_seconds"] for item in case["stills"]] != [t for t in (600, 3600, 7200, 14400) if t <= horizon]:
            raise ValueError("missing or duplicate exact-time stills")
        for still in case["stills"]:
            path = folder / f"t{still['physical_seconds']:05d}.png"
            if (still["path"] != str(path.resolve()) or sha(path) != still["sha256"] or
                    still["source_frame"] != still["physical_seconds"] // DT - 1 or
                    still["extraction"]["command"] != still_command(folder, still["physical_seconds"]) or
                    still["extraction"]["exit_code"] != 0 or
                    still["extraction"]["log_path"] != str((folder / f"t{still['physical_seconds']:05d}.log").resolve()) or
                    still["extraction"]["log_sha256"] != sha(folder / f"t{still['physical_seconds']:05d}.log")):
                raise ValueError("exact-time still changed or moved")
            v5._validate_png(path)
    result = {"schema": "cubey.fluid25d.hillside_rain_v1.review", "automated_numerical_and_media_gate_passed": True,
              "profiles_sha256": sha(out / "profiles/profiles.json"), "captures_sha256": sha(out / "captures/captures.json"),
              "windowed_pacing_sha256": sha(out / "windowed-pacing/pacing.json"),
              "point_source_compatibility_sha256": sha(out / "point-source-compatibility/compatibility.json"),
              "input_identity": identity(app), "main_observations": profiles["cases"][-1]["summary"],
              "human_animation_and_live_gui_acceptance_pending": True,
              "limits": "New rainfall forcing, not numerical equality with the point-source case. No full-horizon strict terrain CPU/GPU parity, calibrated hydrology, river/lake promotion, or live desktop acceptance claimed."}
    if write:
        target = out / "review.json"
        if target.exists():
            raise ValueError("review already exists; do not overwrite historical evidence")
        write_json(target, result)
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=v2.DEFAULT_APP)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--phase", choices=("smoke", "profiles", "compatibility", "captures", "review"), required=True)
    options = parser.parse_args(argv)
    try:
        app, out = options.app.resolve(), options.out.resolve()
        result = (profiles_phase(app, out, smoke=options.phase == "smoke") if options.phase in ("smoke", "profiles")
                  else compatibility_phase(app, out) if options.phase == "compatibility"
                  else captures_phase(app, out) if options.phase == "captures" else review_phase(app, out))
        print(json.dumps({"phase": options.phase, "passed": True, "output": str(out),
                          "cases": len(result.get("cases", []))}))
        return 0
    except Exception as error:
        print(json.dumps({"phase": options.phase, "passed": False, "error": f"{type(error).__name__}: {error}"}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
