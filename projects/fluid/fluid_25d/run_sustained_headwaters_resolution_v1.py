"""Compare sustained-headwaters resolution, damping, and opt-in source supply.

This is an opt-in evidence runner. It never changes terrain, solver defaults, or
the V1 demonstration. Each case starts dry and runs the same 256 x 128 m scene.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_APP = ROOT / "build/dev/projects/fluid/fluid_25d/fluid_25d"
GRIDS = {4: (65, 33), 2: (129, 65), 1: (257, 129)}
HYDRAULIC_SECONDS = 900
DYE_SECONDS = 1800
DYE_START_SECONDS = 900
DYE_DURATION_SECONDS = 60
DIAGNOSTIC_INTERVAL = 10


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def read_metrics(path: Path) -> dict[int, dict[str, float]]:
    samples: dict[int, dict[str, float]] = {}
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            frame = int(row["frame_index"]) + 1  # state after N fixed seconds
            samples.setdefault(frame, {})[f"{row['category']}.{row['name']}"] = float(
                row["value"]
            )
    if not samples:
        raise RuntimeError(f"profile has no diagnostic samples: {path}")
    return samples


def solver_gpu_timing(path: Path) -> dict[str, float]:
    with path.open() as stream:
        for line in stream:
            fields = line.rstrip("\n").split(",")
            if fields[:2] == ["gpu", "fluid_25d solver"]:
                return {
                    "count": int(fields[2]),
                    "avg_ms": float(fields[3]),
                    "p95_ms": float(fields[6]),
                }
    raise RuntimeError(f"missing GPU solver span: {path}")


def first_crossing(
    samples: dict[int, dict[str, float]], metric: str, threshold: float
) -> int | None:
    return next(
        (
            second
            for second, values in sorted(samples.items())
            if values.get(metric, 0.0) >= threshold
        ),
        None,
    )


def require_healthy(samples: dict[int, dict[str, float]], label: str) -> None:
    status_key = "fluid_25d.solver.finite_volume_status_flags"
    if any(values.get(status_key, 0.0) != 0.0 for values in samples.values()):
        raise RuntimeError(f"{label}: finite-volume status is nonzero")
    last = samples[max(samples)]
    source = last["fluid_25d.water.cumulative_source_volume_m3"]
    residual = abs(last["fluid_25d.water.conservation_residual_m3"])
    if source <= 0.0 or residual > 0.005 * source:
        raise RuntimeError(
            f"{label}: source/ledger gate failed ({source=}, {residual=})"
        )


def run_capture(
    app: Path,
    output: Path,
    label: str,
    common: list[str],
    frames: int,
    view: str,
    *,
    profile: bool,
    dye: bool,
) -> tuple[float, dict[int, dict[str, float]] | None, dict[str, float] | None]:
    captures = output / "captures"
    logs = output / "logs"
    profiles = output / "profiles"
    command = [
        str(app),
        "--headless",
        "--width",
        "1280",
        "--height",
        "720",
        *common,
        "--fluid25d-catchment-view",
        view,
    ]
    if dye:
        command += [
            "--fluid25d-dye-pulse-start-seconds",
            str(DYE_START_SECONDS),
            "--fluid25d-dye-pulse-duration-seconds",
            str(DYE_DURATION_SECONDS),
        ]
    command += [
        "--capture",
        "png",
        "--frames",
        str(frames),
        "--output",
        str(captures / f"{label}.png"),
    ]
    if profile:
        command += [
            "--profile-output",
            str(profiles / label),
            "--profile-diagnostics",
            "--profile-diagnostic-interval",
            str(DIAGNOSTIC_INTERVAL),
        ]
    start = time.perf_counter()
    with (logs / f"{label}.log").open("w") as log:
        subprocess.run(
            command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True
        )
    wall_seconds = time.perf_counter() - start
    if not profile:
        return wall_seconds, None, None
    prefix = profiles / label
    return (
        wall_seconds,
        read_metrics(prefix.with_suffix(".metrics.csv")),
        solver_gpu_timing(prefix.with_suffix(".summary.txt")),
    )


def run_video(app: Path, output: Path, common: list[str]) -> float:
    command = [
        str(app),
        "--headless",
        "--width",
        "1280",
        "--height",
        "720",
        *common,
        "--fluid25d-catchment-view",
        "composite",
        "--capture",
        "video",
        "--frames",
        str(HYDRAULIC_SECONDS),
        "--fps",
        "30",
        "--output",
        str(output / "captures/dry-to-mature.mp4"),
    ]
    start = time.perf_counter()
    with (output / "logs/dry-to-mature-video.log").open("w") as log:
        subprocess.run(
            command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True
        )
    return time.perf_counter() - start


def run_case(
    app: Path,
    output: Path,
    metres: int,
    damping: float,
    video: bool,
    *,
    source_scale: float = 1.0,
    supply_stills: bool = False,
) -> dict:
    width, height = GRIDS[metres]
    label = f"{metres}m-damping-{damping:.3f}"
    if supply_stills:
        label += f"-source-{source_scale:g}x"
    case_dir = output / label
    for subdir in ("captures", "logs", "profiles"):
        (case_dir / subdir).mkdir(parents=True, exist_ok=False)
    common = [
        "--grid-width",
        str(width),
        "--grid-height",
        str(height),
        "--fluid25d-cell-size-m",
        str(metres),
        "--fluid25d-scenario",
        "sustained-headwaters-demo",
        "--fluid25d-solver",
        "finite-volume",
        "--fluid25d-fixed-delta-seconds",
        "1",
        "--fluid25d-substeps",
        "32",
        "--fluid25d-flow-damping-per-second",
        str(damping),
        "--fluid25d-view",
        "catchment",
    ]
    if source_scale != 1.0:
        common += ["--fluid25d-headwaters-source-scale", str(source_scale)]
    hydraulic_wall, hydraulic, hydraulic_gpu = run_capture(
        app,
        case_dir,
        "hydraulic-f900",
        common,
        HYDRAULIC_SECONDS,
        "composite",
        profile=True,
        dye=False,
    )
    assert hydraulic is not None and hydraulic_gpu is not None
    require_healthy(hydraulic, label + " hydraulic")
    dye_wall, dyed, dye_gpu = run_capture(
        app,
        case_dir,
        "dye-f1800",
        common,
        DYE_SECONDS,
        "transport-inspection",
        profile=True,
        dye=True,
    )
    assert dyed is not None and dye_gpu is not None
    require_healthy(dyed, label + " dye")
    last_hydraulic = hydraulic[max(hydraulic)]
    discharge_key = "fluid_25d.water.cumulative_boundary_outflow_volume_m3"
    late_start_seconds = max(
        second for second in hydraulic if second <= max(hydraulic) - 100
    )
    late_outlet_rate_m3_per_s = (
        last_hydraulic[discharge_key] - hydraulic[late_start_seconds][discharge_key]
    ) / (max(hydraulic) - late_start_seconds)
    last_dye = dyed[max(dyed)]
    source_dye = last_dye["fluid_25d.tracer.cumulative_source_amount_m3"]
    out_dye = last_dye["fluid_25d.tracer.cumulative_boundary_outflow_amount_m3"]
    if source_dye <= 0.0:
        raise RuntimeError(f"{label}: dye source did not inject")
    result = {
        "label": label,
        "grid": [width, height],
        "cell_size_m": metres,
        "damping_per_second": damping,
        "headwaters_source_scale": source_scale,
        "physical_extent_m": [256, 128],
        "source_m3_per_s_total": 0.5 * source_scale,
        "fixed_delta_seconds": 1,
        "substeps": 32,
        "hydraulic_last_sample_seconds": max(hydraulic),
        "first_sampled_outlet_discharge_seconds": first_crossing(
            hydraulic, "fluid_25d.water.cumulative_boundary_outflow_volume_m3", 0.01
        ),
        "hydraulic_source_m3": last_hydraulic[
            "fluid_25d.water.cumulative_source_volume_m3"
        ],
        "hydraulic_outflow_m3": last_hydraulic[discharge_key],
        "hydraulic_late_outlet_rate_m3_per_s": late_outlet_rate_m3_per_s,
        "hydraulic_stored_m3": last_hydraulic["fluid_25d.water.total_water_volume_m3"],
        "hydraulic_wet_cell_count": last_hydraulic["fluid_25d.water.wet_cell_count"],
        "hydraulic_wet_mean_depth_m": last_hydraulic[
            "fluid_25d.water.wet_mean_depth_m"
        ],
        "hydraulic_station_depth_m": {
            station: last_hydraulic[f"fluid_25d.headwaters.station.{station}.depth_m"]
            for station in (
                "source_a",
                "source_b",
                "branch_a",
                "branch_b",
                "confluence",
                "trunk",
                "outlet",
            )
        },
        "hydraulic_residual_m3": last_hydraulic[
            "fluid_25d.water.conservation_residual_m3"
        ],
        "hydraulic_max_depth_m": last_hydraulic["fluid_25d.water.maximum_depth_m"],
        "hydraulic_trunk_velocity_x_m_per_s": last_hydraulic[
            "fluid_25d.headwaters.station.trunk.velocity_x_m_per_s"
        ],
        "hydraulic_gpu_solver": hydraulic_gpu,
        "hydraulic_headless_wall_seconds": hydraulic_wall,
        "dye_last_sample_seconds": max(dyed),
        "dye_source_amount_m3": source_dye,
        "dye_boundary_outflow_amount_m3": out_dye,
        "dye_outflow_fraction": out_dye / source_dye,
        "dye_first_1pct_outflow_seconds": first_crossing(
            dyed,
            "fluid_25d.tracer.cumulative_boundary_outflow_amount_m3",
            0.01 * source_dye,
        ),
        "dye_half_outflow_seconds": first_crossing(
            dyed,
            "fluid_25d.tracer.cumulative_boundary_outflow_amount_m3",
            0.5 * source_dye,
        ),
        "dye_gpu_solver": dye_gpu,
        "dye_headless_wall_seconds": dye_wall,
    }
    if supply_stills:
        run_capture(
            app,
            case_dir,
            "developing-f300",
            common,
            300,
            "composite",
            profile=False,
            dye=False,
        )
        for frame in (1020, 1080, 1200):
            run_capture(
                app,
                case_dir,
                f"transport-f{frame}",
                common,
                frame,
                "transport-inspection",
                profile=False,
                dye=True,
            )
    if video:
        result["video_headless_wall_seconds"] = run_video(app, case_dir, common)
        result["video_frames"] = HYDRAULIC_SECONDS
        result["video_headless_frames_per_wall_second"] = (
            HYDRAULIC_SECONDS / result["video_headless_wall_seconds"]
        )
    print(json.dumps(result, sort_keys=True), flush=True)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=DEFAULT_APP)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument(
        "--mode", choices=("resolution", "damping", "supply"), default="resolution"
    )
    parser.add_argument("--damping-grid-m", type=int, choices=(4, 2, 1), default=2)
    parser.add_argument(
        "--source-scales",
        nargs="+",
        type=float,
        default=[1.0, 2.0, 4.0],
        help="supply-mode source multipliers; review 1x/2x/4x before adding 8x",
    )
    parser.add_argument(
        "--supply-damping",
        type=float,
        default=0.075,
        help="supply-mode momentum damping per second (default: 0.075)",
    )
    parser.add_argument(
        "--video", action="store_true", help="also record 900-frame presentation videos"
    )
    args = parser.parse_args()
    app = args.app.resolve()
    if not app.is_file() or not app.stat().st_mode & 0o111:
        parser.error(f"app is not executable: {app}")
    if any(not math.isfinite(scale) or scale <= 0 for scale in args.source_scales):
        parser.error("source scales must be positive and finite")
    if len(set(args.source_scales)) != len(args.source_scales):
        parser.error("source scales must be distinct")
    if not math.isfinite(args.supply_damping) or args.supply_damping < 0:
        parser.error("supply damping must be finite and nonnegative")
    if args.output_dir is None:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
        args.output_dir = (
            ROOT / "outputs/fluid" / f"sustained-headwaters-resolution-v1-{stamp}"
        )
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    if args.mode == "resolution":
        cases = [(metres, 0.15, 1.0) for metres in (4, 2, 1)]
    elif args.mode == "damping":
        cases = [(args.damping_grid_m, damping, 1.0) for damping in (0.15, 0.075, 0.05)]
    else:
        cases = [(1, args.supply_damping, scale) for scale in args.source_scales]
    report = {
        "schema": (
            "fluid_25d_sustained_headwaters_supply_v1"
            if args.mode == "supply"
            else "fluid_25d_sustained_headwaters_resolution_v1"
        ),
        "mode": args.mode,
        "app": str(app),
        "app_sha256": sha256(app),
        "runner_sha256": sha256(Path(__file__)),
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "cases": [],
        "limitations": [
            "headless solver GPU spans are not complete GUI frame timings",
            "optional video wall time includes readback and encoding, not just rendering",
            "diagnostic arrivals are sampled upper bounds at 10-second intervals",
            "fine-grid outlet opening retains 12 m width but shifts its center by at most 1 m",
            "maximum and wet-mean depth are not bank freeboard or overbank metrics",
        ],
    }
    for metres, damping, scale in cases:
        report["cases"].append(
            run_case(
                app,
                output,
                metres,
                damping,
                args.video,
                source_scale=scale,
                supply_stills=args.mode == "supply",
            )
        )
        (output / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"study evidence: {output}")


if __name__ == "__main__":
    main()
