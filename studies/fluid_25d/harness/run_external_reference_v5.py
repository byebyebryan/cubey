#!/usr/bin/env python3
"""Bounded ANUGA 4.0.1 DE1 controls for the hillside-rain V5 study.

This is an opt-in external reference harness, not a Cubey solver or a product
path. Each invocation writes to one new leaf below the frozen study's
``reference-cases`` directory. Native solver streams should be captured by the
caller (``>run.raw.log 2>&1``); structured metrics are written to summary.json.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[3]
STUDY_ROOT = REPO_ROOT / "outputs/fluid/hillside-rain-v5-reference-20261002-ROWcGq"
SETUP_ROOT = STUDY_ROOT / "reference-setup"
CASES_ROOT = STUDY_ROOT / "reference-cases"
PAPER_N = 0.03
PAPER_UNIT_DISCHARGE = 0.001
PAPER_SLOPE = 0.1
G = 9.81


def _validate_output_leaf_name(name: str) -> str:
    """Reject paths, traversal, and empty output names before any filesystem write."""
    if not name or Path(name).is_absolute() or Path(name).name != name:
        raise ValueError("--output must be one fresh leaf name, not a path")
    if name in {".", ".."}:
        raise ValueError("--output cannot be '.' or '..'")
    return name


def _parse_sample_times(text: str | None, horizon: float) -> tuple[int, ...] | None:
    if text is None:
        return None
    try:
        values = tuple(int(part.strip()) for part in text.split(","))
    except ValueError as exc:
        raise ValueError("--times must be comma-separated integer seconds") from exc
    if not values or any(value <= 0 for value in values):
        raise ValueError("--times must contain positive integer seconds")
    if tuple(sorted(set(values))) != values:
        raise ValueError("--times must be strictly increasing without duplicates")
    if any(value > horizon for value in values):
        raise ValueError("--times values must not exceed --horizon")
    return values


def _arg_horizon(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("--horizon must be a number") from exc
    if not math.isfinite(parsed) or parsed <= 0.0 or parsed > 86400.0:
        raise argparse.ArgumentTypeError("--horizon must be finite and in (0, 86400] seconds")
    return parsed


def _arg_dt_cap(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("--dt-cap must be a number") from exc
    if not math.isfinite(parsed) or parsed < 1e-3:
        raise argparse.ArgumentTypeError("--dt-cap must be finite and >= 0.001 seconds")
    return parsed


def _new_output_leaf(name: str) -> Path:
    """Create only a fresh single-level case leaf; never replace existing data."""
    name = _validate_output_leaf_name(name)
    CASES_ROOT.mkdir(parents=True, exist_ok=True)
    leaf = CASES_ROOT / name
    leaf.mkdir(exist_ok=False)
    return leaf


def _configure_study_root(path: Path | None) -> None:
    if path is None:
        return
    resolved = path.expanduser().resolve()
    if not resolved.is_dir():
        raise ValueError(f"--study-root must already exist and be a directory: {resolved}")
    if not (resolved / "reference-setup").is_dir():
        raise ValueError("--study-root must contain the prepared reference-setup directory")
    global STUDY_ROOT, SETUP_ROOT, CASES_ROOT
    STUDY_ROOT = resolved
    SETUP_ROOT = resolved / "reference-setup"
    CASES_ROOT = resolved / "reference-cases"


def _write_run_provenance(output: Path, mode: str) -> Path:
    """Snapshot exact runner bytes before any solver or probe subprocess starts."""
    runner_path = Path(__file__).resolve()
    runner_bytes = runner_path.read_bytes()
    runner_snapshot = output / "runner-source.py"
    runner_snapshot.write_bytes(runner_bytes)
    files = {
        "runner-source.py": hashlib.sha256(runner_bytes).hexdigest(),
    }
    if mode == "probe":
        probe_path = SETUP_ROOT / "probe_anuga.py"
        probe_bytes = probe_path.read_bytes()
        (output / "probe-source.py").write_bytes(probe_bytes)
        files["probe-source.py"] = hashlib.sha256(probe_bytes).hexdigest()
    payload = {
        "study_root": str(STUDY_ROOT),
        "output_leaf": str(output),
        "mode": mode,
        "command_argv": sys.argv,
        "python_executable": sys.executable,
        "python_version": sys.version,
        "omp_num_threads": os.environ.get("OMP_NUM_THREADS"),
        "source_snapshots_sha256": files,
        "source_snapshot_precedes_solver_execution": True,
    }
    try:
        payload["anuga_version"] = importlib.metadata.version("anuga")
    except importlib.metadata.PackageNotFoundError:
        payload["anuga_version"] = None
    manifest = output / "run-provenance.json"
    manifest.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    return manifest


def _runtime():
    try:
        import numpy as np
        import anuga
        from anuga.config import MULTIPROCESSOR_OPENMP
        from anuga.abstract_2d_finite_volumes.generic_boundary_conditions import Boundary
        from anuga.operators.base_operator import Operator
        from anuga.operators.rate_operators import Rate_operator
        from anuga.shallow_water import sw_domain_openmp_ext
    except ImportError as exc:
        raise RuntimeError(
            "Use the output-local reference venv at "
            f"{SETUP_ROOT / '.venv/bin/python'}"
        ) from exc
    version = importlib.metadata.version("anuga")
    if version != "4.0.1":
        raise RuntimeError(f"pinned ANUGA 4.0.1 required, found {version}")
    if os.environ.get("OMP_NUM_THREADS") != "1":
        raise RuntimeError("set OMP_NUM_THREADS=1 before importing ANUGA")
    return np, anuga, MULTIPROCESSOR_OPENMP, Boundary, Operator, Rate_operator, sw_domain_openmp_ext


def _make_domain(m: int, n: int, len1: float, len2: float):
    np, anuga, openmp, _, Operator, _, _ = _runtime()
    domain = anuga.rectangular_cross_domain(m, n, len1=len1, len2=len2)
    domain.set_flow_algorithm("DE1")
    domain.set_multiprocessor_mode(openmp)
    domain.set_store(False)
    domain.set_name("hillside_rain_v5_external_reference")

    class GlobalStepObserver(Operator):
        """Count all steps across yield segments and record maximum actual dt."""

        def __init__(self, target_domain):
            self.steps = 0
            self.maximum_timestep = 0.0
            super().__init__(target_domain, label="reference_global_step_observer")

        def __call__(self):
            dt = float(self.domain.get_timestep())
            self.steps += 1
            self.maximum_timestep = max(self.maximum_timestep, dt)

    domain.reference_step_observer = GlobalStepObserver(domain)
    return domain


def _install_reflective(domain, tags=("left", "right", "top", "bottom")):
    _, anuga, _, _, _, _, _ = _runtime()
    wall = anuga.Reflective_boundary(domain)
    domain.set_boundary({tag: wall for tag in tags})


def _volume_and_boundary(domain) -> dict[str, float]:
    return {
        "water_volume_m3": float(domain.get_water_volume()),
        "signed_boundary_flux_integral_m3": float(domain.get_boundary_flux_integral()),
        "fractional_step_volume_integral_m3": float(
            domain.get_fractional_step_volume_integral()
        ),
    }


def _run_lake(*, partial: bool, horizon: float = 100.0) -> dict[str, Any]:
    np, _, _, _, _, _, _ = _runtime()
    domain = _make_domain(20, 10, 100.0, 50.0)
    bed_fn = lambda x, y: 1.0 * x / 100.0 + 0.25 * y / 50.0
    domain.set_quantity("elevation", function=bed_fn, location="vertices")
    z = domain.quantities["elevation"].centroid_values.copy()
    level = 0.65 if partial else 1.5
    domain.set_quantity(
        "stage", np.maximum(level, z), location="centroids"
    )
    domain.set_quantity("xmomentum", 0.0, location="centroids")
    domain.set_quantity("ymomentum", 0.0, location="centroids")
    domain.set_quantity("friction", 0.0)
    _install_reflective(domain)
    initial_depth = (
        domain.quantities["stage"].centroid_values
        - domain.quantities["elevation"].centroid_values
    ).copy()
    initial_volume = float(domain.get_water_volume())
    domain.set_evolve_max_timestep(1.0)
    start = time.perf_counter()
    yielded = [float(t) for t in domain.evolve(yieldstep=horizon, finaltime=horizon)]
    wall = time.perf_counter() - start
    bed = domain.quantities["elevation"].centroid_values
    stage = domain.quantities["stage"].centroid_values
    depth = stage - bed
    mx = domain.quantities["xmomentum"].centroid_values
    my = domain.quantities["ymomentum"].centroid_values
    speed = np.zeros_like(depth)
    wet_final = depth > 1e-5
    speed[wet_final] = np.hypot(mx[wet_final], my[wet_final]) / depth[wet_final]
    wet = initial_depth > 1e-8
    dry = ~wet
    return {
        "control": "partial_lake_at_rest" if partial else "fully_wet_lake_at_rest",
        "algorithm": domain.get_flow_algorithm(),
        "mesh_rectangles": [20, 10],
        "triangles": int(domain.number_of_elements),
        "horizon_seconds": horizon,
        "yield_times_seconds": yielded,
        "cpu_wall_seconds": wall,
        "initial_wet_triangles": int(np.count_nonzero(wet)),
        "initial_dry_triangles": int(np.count_nonzero(dry)),
        "initial_volume_m3": initial_volume,
        "final_volume_m3": float(domain.get_water_volume()),
        "max_abs_depth_change_m": float(np.max(np.abs(depth - initial_depth))),
        "max_speed_m_per_s": float(np.max(speed)),
        "min_raw_stage_minus_bed_m": float(np.min(depth)),
        "all_native_fields_finite": bool(
            np.all(np.isfinite(depth)) and np.all(np.isfinite(mx)) and np.all(np.isfinite(my))
        ),
        "nonnegative_depth_health_pass": bool(np.min(depth) >= -1e-10),
        "signed_boundary_flux_integral_m3": float(domain.get_boundary_flux_integral()),
        "passes_1e-5_targets": bool(
            np.all(np.isfinite(depth)) and np.min(depth) >= -1e-10
            and np.max(np.abs(depth - initial_depth)) <= 1e-5
            and np.max(speed) <= 1e-5
        ),
    }


def _run_dam_break(horizon: float = 5.0) -> dict[str, Any]:
    np, _, _, _, _, _, _ = _runtime()
    length, width, dam_x, h_left = 100.0, 10.0, 50.0, 1.0
    domain = _make_domain(100, 5, length, width)
    domain.set_quantity("elevation", 0.0, location="centroids")
    x = domain.centroid_coordinates[:, 0]
    initial_h = np.where(x < dam_x, h_left, 0.0)
    domain.set_quantity("stage", initial_h, location="centroids")
    domain.set_quantity("xmomentum", 0.0, location="centroids")
    domain.set_quantity("ymomentum", 0.0, location="centroids")
    domain.set_quantity("friction", 0.0)
    _install_reflective(domain)
    initial_volume = float(domain.get_water_volume())
    domain.set_evolve_max_timestep(0.25)
    start = time.perf_counter()
    yielded = [float(t) for t in domain.evolve(yieldstep=horizon, finaltime=horizon)]
    wall = time.perf_counter() - start
    h = (
        domain.quantities["stage"].centroid_values
        - domain.quantities["elevation"].centroid_values
    )
    area = domain.areas
    x = domain.centroid_coordinates[:, 0]
    front_threshold = 1e-4
    wet = h > front_threshold
    measured_front = float(np.max(x[wet])) if np.any(wet) else 0.0
    c0 = math.sqrt(G * h_left)
    expected_front = dam_x + 2.0 * c0 * horizon
    xi = (x - dam_x) / horizon
    expected_h = np.where(
        x <= dam_x - c0 * horizon,
        h_left,
        np.where(
            x < expected_front,
            ((2.0 * c0 - xi) / 3.0) ** 2 / G,
            0.0,
        ),
    )
    profile_l1 = float(np.sum(area * np.abs(h - expected_h)))
    analytic_mass = float(np.sum(area * expected_h))
    relative_l1 = profile_l1 / analytic_mass if analytic_mass else math.inf
    front_relative_error = abs(measured_front - expected_front) / (expected_front - dam_x)
    return {
        "control": "flat_dry_dam_break_ritter",
        "algorithm": domain.get_flow_algorithm(),
        "mesh_rectangles": [100, 5],
        "triangles": int(domain.number_of_elements),
        "horizon_seconds": horizon,
        "yield_times_seconds": yielded,
        "cpu_wall_seconds": wall,
        "dam_x_m": dam_x,
        "analytical_front_x_m": expected_front,
        "measured_front_x_m_at_h_gt_1e-4": measured_front,
        "front_relative_error_normalized_by_travel": front_relative_error,
        "front_target_5pct_pass": front_relative_error <= 0.05,
        "normalized_depth_profile_l1_error": relative_l1,
        "profile_target_10pct_pass": relative_l1 <= 0.10,
        "initial_volume_m3": initial_volume,
        "final_volume_m3": float(domain.get_water_volume()),
        "signed_boundary_flux_integral_m3": float(domain.get_boundary_flux_integral()),
        "minimum_raw_stage_minus_bed_m": float(np.min(h)),
        "all_native_fields_finite": bool(np.all(np.isfinite(h))),
        "nonnegative_depth_health_pass": bool(np.min(h) >= -1e-10),
        "maximum_depth_m": float(np.max(h)),
    }


def _run_flat_rain(horizon: float = 600.0, dt_cap: float = 0.5) -> dict[str, Any]:
    np, _, _, _, _, Rate_operator, _ = _runtime()
    domain = _make_domain(4, 4, 120.0, 120.0)
    domain.g = float(np.float32(9.81))
    bed_level = 1000.0
    domain.set_quantity("elevation", bed_level, location="centroids")
    domain.set_quantity("stage", bed_level, location="centroids")
    domain.set_quantity("xmomentum", 0.0, location="centroids")
    domain.set_quantity("ymomentum", 0.0, location="centroids")
    domain.set_quantity("friction", 0.0)
    _install_reflective(domain)
    rain_rate = float(np.float32(12.0) / np.float32(3600000.0))
    rain = Rate_operator(
        domain, rate=rain_rate, label="flat_elevated_datum_rain_float32_conversion",
        monitor=False,
    )
    domain.set_evolve_max_timestep(dt_cap)
    initial_volume = float(domain.get_water_volume())
    total_area = float(np.sum(domain.areas))
    expected_depth = rain_rate * horizon
    start = time.perf_counter()
    yielded = [float(t) for t in domain.evolve(yieldstep=horizon, finaltime=horizon)]
    wall = time.perf_counter() - start
    raw_depth = (
        domain.quantities["stage"].centroid_values
        - domain.quantities["elevation"].centroid_values
    )
    final_volume = float(domain.get_water_volume())
    source_volume = float(rain.cumulative_influx)
    boundary_flux = float(domain.get_boundary_flux_integral())
    return {
        "control": "closed_flat_constant_rain_elevated_bed_datum",
        "algorithm": domain.get_flow_algorithm(),
        "bed_datum_m": bed_level,
        "mesh_rectangles": [4, 4],
        "triangles": int(domain.number_of_elements),
        "domain_area_m2": total_area,
        "gravity_m_per_second_squared": float(domain.g),
        "rain_operator_label": rain.label,
        "rain_rate_m_per_second_float32_conversion": rain_rate,
        "horizon_seconds": horizon,
        "dt_cap_seconds": dt_cap,
        "number_of_steps": domain.reference_step_observer.steps,
        "maximum_observed_timestep_seconds": domain.reference_step_observer.maximum_timestep,
        "yield_times_seconds": yielded,
        "cpu_wall_seconds": wall,
        "expected_uniform_depth_m": expected_depth,
        "raw_stage_minus_bed_min_m": float(np.min(raw_depth)),
        "raw_stage_minus_bed_max_m": float(np.max(raw_depth)),
        "max_abs_depth_error_from_expected_m": float(
            np.max(np.abs(raw_depth - expected_depth))
        ),
        "initial_volume_m3": initial_volume,
        "final_volume_m3": final_volume,
        "rate_operator_cumulative_influx_m3": source_volume,
        "signed_boundary_flux_integral_m3": boundary_flux,
        "native_water_balance_residual_m3": final_volume - initial_volume - source_volume - boundary_flux,
        "all_native_fields_finite": bool(np.all(np.isfinite(raw_depth))),
        "nonnegative_depth_health_pass": bool(np.min(raw_depth) >= -1e-10),
    }


def _run_damped_incline(horizon: float = 10.0, dt_cap: float = 0.5) -> dict[str, Any]:
    np, _, _, _, Operator, _, _ = _runtime()
    slope = 0.25
    initial_depth = 0.02
    gamma = float(np.float32(0.15))
    gravity = float(np.float32(9.81))
    domain = _make_domain(64, 8, 1920.0, 240.0)
    domain.g = gravity
    domain.set_quantity("elevation", function=lambda x, y: -slope * x, location="vertices")
    domain.set_quantity(
        "stage", function=lambda x, y: -slope * x + initial_depth, location="vertices"
    )
    domain.set_quantity("xmomentum", 0.0, location="centroids")
    domain.set_quantity("ymomentum", 0.0, location="centroids")
    domain.set_quantity("friction", 0.0)
    _install_reflective(domain)

    class ExactLinearDrag(Operator):
        def __init__(self, target_domain):
            self.applied_time = 0.0
            self.maximum_timestep = 0.0
            super().__init__(target_domain, label="damped_incline_exp_linear_drag")

        def __call__(self):
            dt = float(self.domain.get_timestep())
            self.maximum_timestep = max(self.maximum_timestep, dt)
            factor = math.exp(-gamma * dt)
            self.xmom_c[:] *= factor
            self.ymom_c[:] *= factor
            self.applied_time += dt

    drag = ExactLinearDrag(domain)
    domain.set_evolve_max_timestep(dt_cap)
    initial_volume = float(domain.get_water_volume())
    start = time.perf_counter()
    yielded = [float(t) for t in domain.evolve(yieldstep=horizon, finaltime=horizon)]
    wall = time.perf_counter() - start
    coords = domain.centroid_coordinates
    interior = (
        (coords[:, 0] >= 480.0) & (coords[:, 0] <= 1440.0)
        & (coords[:, 1] >= 60.0) & (coords[:, 1] <= 180.0)
    )
    depth = (
        domain.quantities["stage"].centroid_values
        - domain.quantities["elevation"].centroid_values
    )
    hu = domain.quantities["xmomentum"].centroid_values
    velocity = np.divide(hu, depth, out=np.zeros_like(hu), where=depth > 1e-5)
    expected_velocity = gravity * slope / gamma * (1.0 - math.exp(-gamma * horizon))
    sample = velocity[interior]
    return {
        "control": "uniform_damped_incline_acceleration",
        "algorithm": domain.get_flow_algorithm(),
        "mesh_rectangles": [64, 8],
        "mesh_rectangle_size_m": [30.0, 30.0],
        "triangles": int(domain.number_of_elements),
        "slope_m_per_m": slope,
        "initial_depth_m": initial_depth,
        "gravity_m_per_second_squared_float32": gravity,
        "gamma_per_second_float32": gamma,
        "drag_operator_label": drag.label,
        "horizon_seconds": horizon,
        "dt_cap_seconds": dt_cap,
        "number_of_steps": domain.reference_step_observer.steps,
        "maximum_observed_timestep_seconds": domain.reference_step_observer.maximum_timestep,
        "yield_times_seconds": yielded,
        "cpu_wall_seconds": wall,
        "sample_box_m": [480.0, 60.0, 960.0, 120.0],
        "sample_triangle_count": int(np.count_nonzero(interior)),
        "applied_drag_time_seconds": drag.applied_time,
        "analytic_velocity_m_per_second": expected_velocity,
        "interior_mean_velocity_m_per_second": float(np.mean(sample)),
        "interior_median_velocity_m_per_second": float(np.median(sample)),
        "interior_velocity_min_m_per_second": float(np.min(sample)),
        "interior_velocity_max_m_per_second": float(np.max(sample)),
        "interior_mean_relative_error": float(abs(np.mean(sample) - expected_velocity) / expected_velocity),
        "raw_stage_minus_bed_min_m": float(np.min(depth)),
        "raw_stage_minus_bed_max_m": float(np.max(depth)),
        "initial_volume_m3": initial_volume,
        "final_volume_m3": float(domain.get_water_volume()),
        "signed_boundary_flux_integral_m3": float(domain.get_boundary_flux_integral()),
        "all_native_fields_finite": bool(
            np.all(np.isfinite(depth)) and np.all(np.isfinite(hu))
        ),
        "nonnegative_depth_health_pass": bool(np.min(depth) >= -1e-10),
    }


def _run_published_sheet(horizon: float = 3000.0, dt_cap: float | None = None) -> dict[str, Any]:
    np, _, _, Boundary, Operator, _, _ = _runtime()
    domain = _make_domain(20, 5, 400.0, 100.0)
    domain.set_quantity(
        "elevation", function=lambda x, y: -PAPER_SLOPE * x,
        location="vertices",
    )
    analytic_h = (PAPER_N * PAPER_UNIT_DISCHARGE / math.sqrt(PAPER_SLOPE)) ** 0.6
    domain.set_quantity("stage", function=lambda x, y: -PAPER_SLOPE * x,
                        location="vertices")
    domain.set_quantity("xmomentum", 0.0, location="centroids")
    domain.set_quantity("ymomentum", 0.0, location="centroids")
    domain.set_quantity("friction", PAPER_N)

    class UniformUpstreamDischarge(Boundary):
        """Analytic uniform q boundary; stage is local face bed plus h*."""

        def __init__(self, target_domain):
            super().__init__()
            self.domain = target_domain

        def evaluate(self, vol_id, edge_id):
            z = self.domain.quantities["elevation"].edge_values[vol_id, edge_id]
            return [float(z + analytic_h), PAPER_UNIT_DISCHARGE, 0.0]

        def evaluate_segment(self, domain, segment_edges):
            ids = np.asarray(segment_edges, dtype=np.int64)
            if ids.size == 0:
                return
            cells = domain.boundary_cells[ids]
            edges = domain.boundary_edges[ids]
            z = domain.quantities["elevation"].edge_values[cells, edges]
            domain.quantities["stage"].boundary_values[ids] = z + analytic_h
            domain.quantities["xmomentum"].boundary_values[ids] = PAPER_UNIT_DISCHARGE
            domain.quantities["ymomentum"].boundary_values[ids] = 0.0

    import anuga
    wall = anuga.Reflective_boundary(domain)
    inflow = UniformUpstreamDischarge(domain)
    domain.set_boundary({"left": inflow, "right": wall, "top": wall, "bottom": wall})

    if dt_cap is not None:
        domain.set_evolve_max_timestep(dt_cap)
    initial_volume = float(domain.get_water_volume())
    start = time.perf_counter()
    yielded = [float(t) for t in domain.evolve(yieldstep=horizon, finaltime=horizon)]
    wall_seconds = time.perf_counter() - start
    z = domain.quantities["elevation"].centroid_values
    depth = domain.quantities["stage"].centroid_values - z
    hu = domain.quantities["xmomentum"].centroid_values
    velocity = np.divide(hu, depth, out=np.zeros_like(hu), where=depth > 1e-5)
    centroids = domain.centroid_coordinates
    interior = (
        (centroids[:, 0] >= 100.0)
        & (centroids[:, 0] <= 300.0)
        & (np.abs(centroids[:, 1] - 50.0) < 10.0)
        & (depth > 1e-5)
    )
    q = PAPER_UNIT_DISCHARGE
    rel_h = np.abs(depth[interior] - analytic_h) / analytic_h
    u_star = q / analytic_h
    rel_u = np.abs(velocity[interior] - u_star) / u_star
    flux = float(domain.get_boundary_flux_integral())
    final_volume = float(domain.get_water_volume())
    return {
        "control": "davies_roberts_2015_section_3_2_coarse_sheet",
        "paper": "https://www.mssanz.org.au/modsim2015/L5/davies.pdf",
        "algorithm": domain.get_flow_algorithm(),
        "mesh_rectangles": [20, 5],
        "triangles": int(domain.number_of_elements),
        "triangle_area_min_m2": float(np.min(domain.areas)),
        "triangle_area_max_m2": float(np.max(domain.areas)),
        "slope_m_per_m": PAPER_SLOPE,
        "manning_n": PAPER_N,
        "upstream_total_discharge_m3_per_s": PAPER_UNIT_DISCHARGE * 100.0,
        "upstream_unit_discharge_m2_per_s": PAPER_UNIT_DISCHARGE,
        "upstream_boundary_label": "uniform_analytic_stage_plus_depth_and_hu=q",
        "analytical_depth_m": analytic_h,
        "analytical_velocity_m_per_s": u_star,
        "horizon_seconds": horizon,
        "evolve_max_timestep_cap_seconds": dt_cap,
        "yield_times_seconds": yielded,
        "number_of_steps": domain.reference_step_observer.steps,
        "maximum_observed_timestep_seconds": domain.reference_step_observer.maximum_timestep,
        "global_step_count_scope": "Operator counter across all evolve yield segments",
        "cpu_wall_seconds": wall_seconds,
        "cpu_wall_seconds_per_simulated_second": wall_seconds / horizon,
        "interior_centroid_count": int(np.count_nonzero(interior)),
        "interior_depth_relative_error_max": float(np.max(rel_h)) if rel_h.size else None,
        "interior_depth_relative_error_p95": float(np.quantile(rel_h, 0.95)) if rel_h.size else None,
        "interior_velocity_relative_error_max": float(np.max(rel_u)) if rel_u.size else None,
        "interior_velocity_relative_error_p95": float(np.quantile(rel_u, 0.95)) if rel_u.size else None,
        "interior_depth_target_5pct_pass": bool(rel_h.size and np.max(rel_h) <= 0.05),
        "interior_velocity_target_5pct_pass": bool(rel_u.size and np.max(rel_u) <= 0.05),
        "initial_volume_m3": initial_volume,
        "final_volume_m3": final_volume,
        "signed_boundary_flux_integral_m3": flux,
        "rain_or_fractional_source_integral_m3": float(domain.get_fractional_step_volume_integral()),
        "boundary_flux_closure_residual_m3": final_volume - initial_volume - flux,
        "minimum_raw_stage_minus_bed_m": float(np.min(depth)),
        "all_native_fields_finite": bool(
            np.all(np.isfinite(depth)) and np.all(np.isfinite(hu))
        ),
        "nonnegative_depth_health_pass": bool(np.min(depth) >= -1e-10),
        "maximum_speed_m_per_s": float(np.max(np.divide(
            np.hypot(
                domain.quantities["xmomentum"].centroid_values,
                domain.quantities["ymomentum"].centroid_values,
            ),
            depth,
            out=np.zeros_like(depth),
            where=depth > 1e-5,
        ))),
    }


def _run_mountain_timing(
    output: Path,
    horizon: float = 30.0,
    dt_cap: float = 0.5,
    sample_times: tuple[int, ...] | None = None,
) -> dict[str, Any]:
    np, _, _, Boundary, Operator, Rate_operator, native_ext = _runtime()
    bed_path = STUDY_ROOT / "inputs/mountain-bed.f32"
    digest = hashlib.sha256(bed_path.read_bytes()).hexdigest()
    expected_digest = "02f1577d96f3ea4f65b75c7b21b6e1b2c83248cebb8b87a528fff6193435bc3f"
    if digest != expected_digest:
        raise RuntimeError(f"mountain input SHA256 mismatch: {digest}")
    grid = np.fromfile(bed_path, dtype="<f4").reshape((512, 512))
    domain = _make_domain(512, 512, 15360.0, 15360.0)
    centroids = domain.centroid_coordinates
    cols = np.floor(centroids[:, 0] / 30.0).astype(np.int32)
    rows = np.floor(centroids[:, 1] / 30.0).astype(np.int32)
    triangle_bed = grid[rows, cols]
    domain.set_quantity("elevation", triangle_bed, location="centroids")
    domain.set_quantity("stage", triangle_bed, location="centroids")
    domain.set_quantity("xmomentum", 0.0, location="centroids")
    domain.set_quantity("ymomentum", 0.0, location="centroids")
    domain.set_quantity("friction", 0.0)
    gravity = float(np.float32(9.81))
    domain.g = gravity

    if sample_times is None:
        sample_times = tuple(t for t in (60, 120, 300, 600) if t <= horizon)
        if not sample_times:
            sample_times = (int(horizon),)
    if any(t <= 0 or t > horizon for t in sample_times):
        raise ValueError("mountain sample times must be positive and <= horizon")
    sample_set = set(sample_times)

    # Static geometry stays double precision and is common to every observation.
    mesh_counts = np.bincount(rows * 512 + cols, minlength=512 * 512)
    if not np.all(mesh_counts == 4):
        raise AssertionError("strict aligned mesh must have four triangles per raster cell")
    np.savez(
        output / "native-mesh.npz",
        centroid_xy_m=centroids.copy(),
        triangle_area_m2=domain.areas.copy(),
        elevation_centroid_m=domain.quantities["elevation"].centroid_values.copy(),
        raster_row=rows,
        raster_col=cols,
    )

    class FaceBedDryBoundary(Boundary):
        def __init__(self, target_domain):
            super().__init__()
            self.domain = target_domain

        def evaluate(self, vol_id, edge_id):
            z = self.domain.quantities["elevation"].edge_values[vol_id, edge_id]
            return [float(z), 0.0, 0.0]

        def evaluate_segment(self, domain, segment_edges):
            ids = np.asarray(segment_edges, dtype=np.int64)
            if ids.size == 0:
                return
            cells = domain.boundary_cells[ids]
            edges = domain.boundary_edges[ids]
            z = domain.quantities["elevation"].edge_values[cells, edges]
            domain.quantities["stage"].boundary_values[ids] = z
            domain.quantities["xmomentum"].boundary_values[ids] = 0.0
            domain.quantities["ymomentum"].boundary_values[ids] = 0.0

    dry = FaceBedDryBoundary(domain)
    domain.set_boundary({tag: dry for tag in ("left", "right", "top", "bottom")})

    class ExactLinearDrag(Operator):
        def __init__(self, target_domain, gamma=float(np.float32(0.15))):
            self.gamma = float(gamma)
            self.applied_time = 0.0
            super().__init__(target_domain, label="v5_reference_exp_linear_drag")

        def __call__(self):
            dt = float(self.domain.get_timestep())
            factor = math.exp(-self.gamma * dt)
            self.xmom_c[:] *= factor
            self.ymom_c[:] *= factor
            self.applied_time += dt

    class FilmMomentumCutoff(Operator):
        def __init__(self, target_domain, threshold=1e-4):
            self.threshold = float(threshold)
            super().__init__(target_domain, label="v5_reference_momentum_film_cutoff_1e-4m")

        def __call__(self):
            depth = self.domain.quantities["stage"].centroid_values - self.domain.quantities["elevation"].centroid_values
            dry_mask = depth < self.threshold
            self.xmom_c[dry_mask] = 0.0
            self.ymom_c[dry_mask] = 0.0

    drag = ExactLinearDrag(domain)
    film = FilmMomentumCutoff(domain)
    rain_rate = float(np.float32(12.0) / np.float32(3600000.0))
    rain = Rate_operator(
        domain,
        rate=rain_rate,
        label="v5_constant_rain_12mm_per_hour_float32_conversion",
        monitor=False,
    )
    domain.set_evolve_max_timestep(dt_cap)
    initial_volume = float(domain.get_water_volume())
    start = time.perf_counter()
    yield_step = float(math.gcd(*sample_times)) if len(sample_times) > 1 else float(sample_times[0])
    yielded = []
    sample_ledgers = []
    for t in domain.evolve(yieldstep=yield_step, finaltime=horizon):
        time_value = float(t)
        yielded.append(time_value)
        nearest = int(round(time_value))
        if nearest not in sample_set:
            continue
        depth = (
            domain.quantities["stage"].centroid_values
            - domain.quantities["elevation"].centroid_values
        ).copy()
        xmom = domain.quantities["xmomentum"].centroid_values.copy()
        ymom = domain.quantities["ymomentum"].centroid_values.copy()
        np.savez(
            output / f"native-{nearest}.npz",
            depth_m=depth,
            xmomentum_m2_per_s=xmom,
            ymomentum_m2_per_s=ymom,
        )
        cell_count = 512 * 512
        weights = domain.areas
        flat_cell = rows * 512 + cols
        cell_area = 30.0 * 30.0
        depth_grid = np.bincount(
            flat_cell, weights=weights * depth, minlength=cell_count
        ) / cell_area
        xmom_grid = np.bincount(
            flat_cell, weights=weights * xmom, minlength=cell_count
        ) / cell_area
        ymom_grid = np.bincount(
            flat_cell, weights=weights * ymom, minlength=cell_count
        ) / cell_area
        ux_grid = np.divide(
            xmom_grid, depth_grid, out=np.zeros_like(depth_grid),
            where=depth_grid >= 1e-4,
        )
        uy_grid = np.divide(
            ymom_grid, depth_grid, out=np.zeros_like(depth_grid),
            where=depth_grid >= 1e-4,
        )
        shape = (512, 512)
        depth_grid.reshape(shape).astype("<f4").tofile(output / f"{nearest}-depth.f32")
        ux_grid.reshape(shape).astype("<f4").tofile(output / f"{nearest}-ux.f32")
        uy_grid.reshape(shape).astype("<f4").tofile(output / f"{nearest}-uy.f32")
        sample_ledgers.append({
            "time_seconds": nearest,
            "water_volume_m3": float(domain.get_water_volume()),
            "rain_cumulative_influx_m3": float(rain.cumulative_influx),
            "signed_boundary_flux_integral_m3": float(domain.get_boundary_flux_integral()),
            "native_raw_depth_min_m": float(np.min(depth)),
            "native_raw_depth_max_m": float(np.max(depth)),
            "native_fields_finite": bool(
                np.all(np.isfinite(depth)) and np.all(np.isfinite(xmom)) and np.all(np.isfinite(ymom))
            ),
        })
    wall_seconds = time.perf_counter() - start
    final_volume = float(domain.get_water_volume())
    source_volume = float(rain.cumulative_influx)
    boundary_flux = float(domain.get_boundary_flux_integral())
    residual = final_volume - initial_volume - source_volume - boundary_flux
    final_depth = (
        domain.quantities["stage"].centroid_values
        - domain.quantities["elevation"].centroid_values
    )
    return {
        "control": "strict_full_native_mesh_mountain_runtime_probe",
        "anuga_version": importlib.metadata.version("anuga"),
        "algorithm": domain.get_flow_algorithm(),
        "multiprocessor_mode": int(domain.get_multiprocessor_mode()),
        "omp_num_threads": os.environ.get("OMP_NUM_THREADS"),
        "native_cpu_extension": str(Path(native_ext.__file__).resolve()),
        "native_cpu_extension_sha256": hashlib.sha256(Path(native_ext.__file__).read_bytes()).hexdigest(),
        "input_path": str(bed_path),
        "input_sha256": digest,
        "input_shape": [512, 512],
        "input_dx_m": 30.0,
        "rectangles": [512, 512],
        "triangles": int(domain.number_of_elements),
        "triangles_per_raster_cell": 4,
        "cell_bed_is_sampled_at_triangle_centroids_without_averaging": True,
        "rain_operator_label": rain.label,
        "rain_rate_mm_per_hour": 12.0,
        "rain_rate_m_per_second_float32_conversion": rain_rate,
        "gravity_m_per_second_squared_float32": gravity,
        "drag_operator_label": drag.label,
        "drag_gamma_per_second": drag.gamma,
        "film_cutoff_operator_label": film.label,
        "film_cutoff_m": film.threshold,
        "horizon_seconds": horizon,
        "yield_times_seconds": yielded,
        "max_timestep_cap_seconds": dt_cap,
        "number_of_steps": domain.reference_step_observer.steps,
        "maximum_observed_timestep_seconds": domain.reference_step_observer.maximum_timestep,
        "applied_drag_time_seconds": drag.applied_time,
        "cpu_wall_seconds": wall_seconds,
        "cpu_wall_seconds_per_simulated_second": wall_seconds / horizon,
        "estimated_7200s_cpu_wall_seconds": wall_seconds / horizon * 7200.0,
        "sample_times_seconds": list(sample_times),
        "sample_ledgers": sample_ledgers,
        "native_mesh_file": str(output / "native-mesh.npz"),
        "projected_field_pattern": "{time}-depth.f32 / {time}-ux.f32 / {time}-uy.f32",
        "native_field_pattern": "native-{time}.npz containing double depth and x/y momentum",
        "initial_volume_m3": initial_volume,
        "final_volume_m3": final_volume,
        "rain_operator_cumulative_influx_m3": source_volume,
        "signed_boundary_flux_integral_m3": boundary_flux,
        "native_water_balance_residual_m3": residual,
        "min_raw_stage_minus_bed_m": float(np.min(final_depth)),
        "max_raw_stage_minus_bed_m": float(np.max(final_depth)),
        "all_native_fields_finite": bool(
            np.all(np.isfinite(final_depth))
            and np.all(np.isfinite(domain.quantities["xmomentum"].centroid_values))
            and np.all(np.isfinite(domain.quantities["ymomentum"].centroid_values))
        ),
        "nonnegative_depth_health_pass": bool(np.min(final_depth) >= -1e-10),
    }


def _run_subdomain(
    output: Path,
    *,
    geometry: str,
    mesh_dx: int,
    horizon: float = 600.0,
    dt_cap: float = 0.5,
    sample_times: tuple[int, ...] = (60, 120, 300, 600),
) -> dict[str, Any]:
    """Strict/refined or separately labelled bilinear geometry on frozen crop."""
    np, _, _, Boundary, Operator, Rate_operator, native_ext = _runtime()
    if geometry not in {"strict", "bilinear"}:
        raise ValueError("geometry must be strict or bilinear")
    if mesh_dx not in {15, 30}:
        raise ValueError("subdomain mesh spacing must be 15 or 30 metres")
    if any(t <= 0 or t > horizon for t in sample_times):
        raise ValueError("subdomain sample times must be positive and <= horizon")
    bed_path = STUDY_ROOT / "inputs/mountain-bed.f32"
    input_hash = hashlib.sha256(bed_path.read_bytes()).hexdigest()
    expected_hash = "02f1577d96f3ea4f65b75c7b21b6e1b2c83248cebb8b87a528fff6193435bc3f"
    if input_hash != expected_hash:
        raise RuntimeError(f"mountain input SHA256 mismatch: {input_hash}")
    full_grid = np.fromfile(bed_path, dtype="<f4").reshape((512, 512)).astype(np.float64)
    x0, y0, raster_width = 256, 192, 128
    source = full_grid[y0:y0 + raster_width, x0:x0 + raster_width]
    domain_m = raster_width * 30.0
    rectangles = int(domain_m / mesh_dx)
    domain = _make_domain(rectangles, rectangles, domain_m, domain_m)
    local_centroids = domain.centroid_coordinates
    raster_col = np.floor(local_centroids[:, 0] / 30.0).astype(np.int32)
    raster_row = np.floor(local_centroids[:, 1] / 30.0).astype(np.int32)
    if (np.any(raster_col < 0) or np.any(raster_col >= raster_width)
            or np.any(raster_row < 0) or np.any(raster_row >= raster_width)):
        raise AssertionError("subdomain triangle centroid outside frozen raster crop")

    def bilinear_dem(x, y):
        xf = np.asarray(x, dtype=np.float64) / 30.0 + x0 - 0.5
        yf = np.asarray(y, dtype=np.float64) / 30.0 + y0 - 0.5
        c0 = np.floor(xf).astype(np.int64)
        r0 = np.floor(yf).astype(np.int64)
        tx = xf - c0
        ty = yf - r0
        c0 = np.clip(c0, 0, 510)
        r0 = np.clip(r0, 0, 510)
        c1 = c0 + 1
        r1 = r0 + 1
        a = full_grid[r0, c0]
        b = full_grid[r0, c1]
        c = full_grid[r1, c0]
        d = full_grid[r1, c1]
        return ((1.0 - ty) * ((1.0 - tx) * a + tx * b)
                + ty * ((1.0 - tx) * c + tx * d))

    if geometry == "strict":
        triangle_bed = source[raster_row, raster_col]
        domain.set_quantity("elevation", triangle_bed, location="centroids")
    else:
        domain.set_quantity("elevation", function=bilinear_dem, location="vertices")
        triangle_bed = domain.quantities["elevation"].centroid_values.copy()

    domain.set_quantity("stage", triangle_bed, location="centroids")
    domain.set_quantity("xmomentum", 0.0, location="centroids")
    domain.set_quantity("ymomentum", 0.0, location="centroids")
    domain.set_quantity("friction", 0.0)
    domain.g = float(np.float32(9.81))

    class FaceBedDryBoundary(Boundary):
        def __init__(self, target_domain):
            super().__init__()
            self.domain = target_domain

        def evaluate(self, vol_id, edge_id):
            z = self.domain.quantities["elevation"].edge_values[vol_id, edge_id]
            return [float(z), 0.0, 0.0]

        def evaluate_segment(self, domain, segment_edges):
            ids = np.asarray(segment_edges, dtype=np.int64)
            if ids.size == 0:
                return
            cells = domain.boundary_cells[ids]
            edges = domain.boundary_edges[ids]
            z = domain.quantities["elevation"].edge_values[cells, edges]
            domain.quantities["stage"].boundary_values[ids] = z
            domain.quantities["xmomentum"].boundary_values[ids] = 0.0
            domain.quantities["ymomentum"].boundary_values[ids] = 0.0

    dry = FaceBedDryBoundary(domain)
    domain.set_boundary({tag: dry for tag in ("left", "right", "top", "bottom")})

    gamma = float(np.float32(0.15))

    class ExactLinearDrag(Operator):
        def __init__(self, target_domain):
            self.applied_time = 0.0
            super().__init__(target_domain, label="subdomain_exp_linear_drag")

        def __call__(self):
            dt = float(self.domain.get_timestep())
            factor = math.exp(-gamma * dt)
            self.xmom_c[:] *= factor
            self.ymom_c[:] *= factor
            self.applied_time += dt

    class FilmMomentumCutoff(Operator):
        def __init__(self, target_domain):
            self.threshold = 1e-4
            super().__init__(target_domain, label="subdomain_momentum_film_cutoff_1e-4m")

        def __call__(self):
            depth = (
                self.domain.quantities["stage"].centroid_values
                - self.domain.quantities["elevation"].centroid_values
            )
            mask = depth < self.threshold
            self.xmom_c[mask] = 0.0
            self.ymom_c[mask] = 0.0

    drag = ExactLinearDrag(domain)
    film = FilmMomentumCutoff(domain)
    rain_rate = float(np.float32(12.0) / np.float32(3600000.0))
    rain = Rate_operator(
        domain, rate=rain_rate, label="subdomain_rain_12mm_per_hour_float32_conversion",
        monitor=False,
    )
    domain.set_evolve_max_timestep(dt_cap)

    flat_cell = raster_row * raster_width + raster_col
    mesh_cell_counts = np.bincount(flat_cell, minlength=raster_width * raster_width)
    expected_per_cell = 4 if mesh_dx == 30 else 16
    if not np.all(mesh_cell_counts == expected_per_cell):
        raise AssertionError("mesh did not subdivide each original raster cell as declared")
    cell_area = 900.0
    projected_bed = np.bincount(
        flat_cell, weights=domain.areas * triangle_bed,
        minlength=raster_width * raster_width,
    ) / cell_area
    bed_delta = projected_bed.reshape(source.shape) - source
    bed_delta.astype("<f4").tofile(output / "bed-mean-delta.f32")
    global_xy = local_centroids.copy()
    global_xy[:, 0] += x0 * 30.0
    global_xy[:, 1] += y0 * 30.0
    np.savez(
        output / "native-mesh.npz",
        centroid_xy_local_m=local_centroids.copy(),
        centroid_xy_crop_m=global_xy,
        triangle_area_m2=domain.areas.copy(),
        elevation_centroid_m=triangle_bed.copy(),
        source_row=raster_row + y0,
        source_col=raster_col + x0,
    )

    initial_volume = float(domain.get_water_volume())
    yield_step = float(math.gcd(*sample_times)) if len(sample_times) > 1 else float(sample_times[0])
    yielded: list[float] = []
    samples: list[dict[str, Any]] = []
    minimum_depth = math.inf
    maximum_depth = -math.inf
    fields_finite = True
    start = time.perf_counter()
    for time_value in domain.evolve(yieldstep=yield_step, finaltime=horizon):
        t = float(time_value)
        yielded.append(t)
        nearest = int(round(t))
        if nearest not in set(sample_times):
            continue
        depth = (
            domain.quantities["stage"].centroid_values
            - domain.quantities["elevation"].centroid_values
        ).copy()
        xmom = domain.quantities["xmomentum"].centroid_values.copy()
        ymom = domain.quantities["ymomentum"].centroid_values.copy()
        minimum_depth = min(minimum_depth, float(np.min(depth)))
        maximum_depth = max(maximum_depth, float(np.max(depth)))
        fields_finite = fields_finite and bool(
            np.all(np.isfinite(depth)) and np.all(np.isfinite(xmom)) and np.all(np.isfinite(ymom))
        )
        np.savez(
            output / f"native-{nearest}.npz",
            depth_m=depth,
            xmomentum_m2_per_s=xmom,
            ymomentum_m2_per_s=ymom,
        )
        projected_depth = np.bincount(
            flat_cell, weights=domain.areas * depth,
            minlength=raster_width * raster_width,
        ) / cell_area
        projected_xmom = np.bincount(
            flat_cell, weights=domain.areas * xmom,
            minlength=raster_width * raster_width,
        ) / cell_area
        projected_ymom = np.bincount(
            flat_cell, weights=domain.areas * ymom,
            minlength=raster_width * raster_width,
        ) / cell_area
        ux = np.divide(
            projected_xmom, projected_depth, out=np.zeros_like(projected_depth),
            where=projected_depth >= 1e-4,
        )
        uy = np.divide(
            projected_ymom, projected_depth, out=np.zeros_like(projected_depth),
            where=projected_depth >= 1e-4,
        )
        projected_depth.reshape(source.shape).astype("<f4").tofile(output / f"{nearest}-depth.f32")
        ux.reshape(source.shape).astype("<f4").tofile(output / f"{nearest}-ux.f32")
        uy.reshape(source.shape).astype("<f4").tofile(output / f"{nearest}-uy.f32")
        samples.append({
            "time_seconds": nearest,
            "water_volume_m3": float(domain.get_water_volume()),
            "rain_cumulative_influx_m3": float(rain.cumulative_influx),
            "signed_boundary_flux_integral_m3": float(domain.get_boundary_flux_integral()),
            "raw_depth_min_m": float(np.min(depth)),
            "raw_depth_max_m": float(np.max(depth)),
            "native_fields_finite": bool(
                np.all(np.isfinite(depth)) and np.all(np.isfinite(xmom)) and np.all(np.isfinite(ymom))
            ),
        })
    wall = time.perf_counter() - start
    final_volume = float(domain.get_water_volume())
    source_volume = float(rain.cumulative_influx)
    boundary_flux = float(domain.get_boundary_flux_integral())
    residual = final_volume - initial_volume - source_volume - boundary_flux
    source_tol = max(1e-6, source_volume * 1e-8)
    return {
        "control": "bounded_subdomain_bilinear_geometry_sensitivity" if geometry == "bilinear"
        else "bounded_subdomain_strict_fixed_raster_sensitivity",
        "geometry_label": geometry,
        "algorithm": domain.get_flow_algorithm(),
        "source_path": str(bed_path),
        "source_sha256": input_hash,
        "source_crop_xzwh": [x0, y0, raster_width, raster_width],
        "source_crop_cell_area_m2": cell_area,
        "subdomain_physical_extent_m": [domain_m, domain_m],
        "mesh_spacing_m": mesh_dx,
        "mesh_rectangles": [rectangles, rectangles],
        "triangles": int(domain.number_of_elements),
        "triangles_per_original_source_cell": expected_per_cell,
        "source_cell_triangle_count_min": int(np.min(mesh_cell_counts)),
        "source_cell_triangle_count_max": int(np.max(mesh_cell_counts)),
        "bilinear_rule": "full crop center samples including neighbor columns/rows at boundaries; vertex elevations; native centroids derived from vertex values" if geometry == "bilinear" else None,
        "projected_bed_delta_min_m": float(np.min(bed_delta)),
        "projected_bed_delta_max_m": float(np.max(bed_delta)),
        "projected_bed_delta_p05_m": float(np.quantile(bed_delta, 0.05)),
        "projected_bed_delta_median_m": float(np.quantile(bed_delta, 0.5)),
        "projected_bed_delta_p95_m": float(np.quantile(bed_delta, 0.95)),
        "projected_bed_delta_rms_m": float(np.sqrt(np.mean(bed_delta ** 2))),
        "projected_bed_delta_mean_m": float(np.mean(bed_delta)),
        "projected_bed_delta_artifact": str(output / "bed-mean-delta.f32"),
        "gravity_m_per_second_squared_float32": float(domain.g),
        "rain_rate_m_per_second_float32_conversion": rain_rate,
        "drag_gamma_per_second_float32": gamma,
        "film_cutoff_m": film.threshold,
        "rain_operator_label": rain.label,
        "drag_operator_label": drag.label,
        "film_cutoff_operator_label": film.label,
        "boundary_label": "dry ghost stage from live elevation edge values; both ghost momenta zero",
        "horizon_seconds": horizon,
        "sample_times_seconds": list(sample_times),
        "yield_times_seconds": yielded,
        "dt_cap_seconds": dt_cap,
        "number_of_steps": domain.reference_step_observer.steps,
        "maximum_observed_timestep_seconds": domain.reference_step_observer.maximum_timestep,
        "applied_drag_time_seconds": drag.applied_time,
        "cpu_wall_seconds": wall,
        "cpu_wall_seconds_per_simulated_second": wall / horizon,
        "initial_volume_m3": initial_volume,
        "final_volume_m3": final_volume,
        "rain_operator_cumulative_influx_m3": source_volume,
        "signed_boundary_flux_integral_m3": boundary_flux,
        "native_water_balance_residual_m3": residual,
        "native_water_balance_tolerance_m3": source_tol,
        "native_water_balance_pass": abs(residual) <= source_tol,
        "min_raw_stage_minus_bed_m": minimum_depth,
        "max_raw_stage_minus_bed_m": maximum_depth,
        "all_native_fields_finite": fields_finite,
        "nonnegative_depth_health_pass": minimum_depth >= -1e-10,
        "native_mesh_file": str(output / "native-mesh.npz"),
        "native_field_pattern": "native-{time}.npz containing double depth and x/y momentum",
        "projected_field_pattern": "{time}-{depth,ux,uy}.f32, aligned 128x128 cell-area projections",
        "sample_ledgers": samples,
    }


def _run_probe(output: Path) -> dict[str, Any]:
    raw_path = output / "probe.raw.log"
    with raw_path.open("wb") as stream:
        result = subprocess.run(
            [sys.executable, str(SETUP_ROOT / "probe_anuga.py")],
            cwd=REPO_ROOT,
            env={**os.environ, "OMP_NUM_THREADS": "1"},
            stdout=stream,
            stderr=subprocess.STDOUT,
            check=False,
        )
    raw_text = raw_path.read_text(errors="replace")
    decoder = json.JSONDecoder()
    parsed = None
    for start in (i for i, char in enumerate(raw_text) if char == "{"):
        try:
            parsed, _ = decoder.raw_decode(raw_text[start:])
            break
        except json.JSONDecodeError:
            continue
    if result.returncode or parsed is None:
        raise RuntimeError(f"probe failed; inspect {raw_path} (exit {result.returncode})")
    return {
        "control": "api_and_native_de1_smoke",
        "probe_raw_log": str(raw_path),
        "probe": parsed,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--mode", required=True,
        choices=("probe", "controls", "paper-sheet", "lakes", "dam-break", "flat-rain", "incline-drag", "mountain-timing", "subdomain"),
    )
    parser.add_argument("--output", required=True, help="fresh single-level case leaf name")
    parser.add_argument("--horizon", type=_arg_horizon, default=None)
    parser.add_argument("--dt-cap", type=_arg_dt_cap, default=None)
    parser.add_argument("--times", default=None, help="comma-separated mountain sample seconds")
    parser.add_argument("--study-root", type=Path, default=None,
                        help="prepared study root; defaults to this V5 study")
    parser.add_argument("--geometry", choices=("strict", "bilinear"), default="strict")
    parser.add_argument("--mesh-dx", type=int, choices=(15, 30), default=30)
    args = parser.parse_args(argv)
    try:
        if args.mode in {"mountain-timing", "subdomain"}:
            default_horizon = 30.0 if args.mode == "mountain-timing" else 600.0
            selected_horizon = default_horizon if args.horizon is None else args.horizon
            times = _parse_sample_times(args.times, selected_horizon)
        else:
            if args.times is not None:
                parser.error("--times is only valid with --mode mountain-timing or subdomain")
            times = None
    except ValueError as exc:
        parser.error(str(exc))
    _configure_study_root(args.study_root)
    output = _new_output_leaf(args.output)
    provenance_path = _write_run_provenance(output, args.mode)
    _runtime()
    if args.mode == "probe":
        metrics = _run_probe(output)
    elif args.mode == "paper-sheet":
        metrics = _run_published_sheet(args.horizon or 3000.0, args.dt_cap)
    elif args.mode == "lakes":
        metrics = {
            "control": "lake_at_rest_pair",
            "fully_wet": _run_lake(partial=False, horizon=args.horizon or 100.0),
            "partially_wet": _run_lake(partial=True, horizon=args.horizon or 100.0),
        }
    elif args.mode == "dam-break":
        metrics = _run_dam_break(args.horizon or 5.0)
    elif args.mode == "flat-rain":
        metrics = _run_flat_rain(args.horizon or 600.0, args.dt_cap or 0.5)
    elif args.mode == "incline-drag":
        metrics = _run_damped_incline(args.horizon or 10.0, args.dt_cap or 0.5)
    elif args.mode == "mountain-timing":
        horizon = args.horizon or 30.0
        metrics = _run_mountain_timing(
            output, horizon,
            0.5 if args.dt_cap is None else args.dt_cap,
            times,
        )
    elif args.mode == "subdomain":
        horizon = args.horizon or 600.0
        metrics = _run_subdomain(
            output,
            geometry=args.geometry,
            mesh_dx=args.mesh_dx,
            horizon=horizon,
            dt_cap=0.5 if args.dt_cap is None else args.dt_cap,
            sample_times=times or tuple(
                t for t in (60, 120, 300, 600) if t <= horizon
            ) or (int(horizon),),
        )
    else:
        metrics = {
            "control": "published_and_analytical_controls",
            "published_sheet": _run_published_sheet(args.horizon or 3000.0, args.dt_cap),
            "lakes": {
                "fully_wet": _run_lake(partial=False),
                "partially_wet": _run_lake(partial=True),
            },
            "dam_break": _run_dam_break(),
            "damped_incline": _run_damped_incline(),
        }
    payload = {
        "study_root": str(STUDY_ROOT),
        "output_leaf": str(output),
        "run_provenance_file": str(provenance_path),
        "mode": args.mode,
        "metrics": metrics,
    }
    (output / "summary.json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
