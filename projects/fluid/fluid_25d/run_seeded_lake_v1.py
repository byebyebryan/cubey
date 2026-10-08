#!/usr/bin/env python3
"""Add a declared pre-existing lake to the unchanged native mountain/rain demo.

Uses the installed, provenance-checked SynxFlow reference and Cubey's existing
recording adapter/viewer. No terrain edits, solver port, or default changes.
Run with the existing native study's Python environment. Outputs are exclusive.
"""

from __future__ import annotations

import argparse
import html
import json
import math
import os
import shutil
import subprocess
import time
from pathlib import Path

import convert_synxflow_recording_v1 as converter
import numpy as np
import run_native_presentation_v1 as presentation
import run_native_rain_recession_v1 as recession
from scipy import ndimage

ROOT = presentation.ROOT
PYTHON = recession.PYTHON
D4 = ndimage.generate_binary_structure(2, 1)
SCHEMA = "cubey.fluid25d.seeded_lake.v1"


def write(path: Path, value) -> None:
    presentation.write_json_exclusive(path, value)


def lake_depth(bed, basin_mask, spill_m: float, fraction: float = 0.9):
    """Flood the floor-connected sublevel set, not a painted depth/mask patch.

    Compute depths against the bed actually serialized to the native solver.
    The analytical basin mask selects the floor only; connectivity uses the
    entire DEM, so no artificial vertical water walls are created at mask edges.
    """
    bed = np.asarray(bed, dtype=np.float64)
    basin_mask = np.asarray(basin_mask, dtype=bool)
    if (
        bed.ndim != 2
        or bed.shape != basin_mask.shape
        or min(bed.shape) < 3
        or not np.isfinite(bed).all()
        or not basin_mask.any()
        or not math.isfinite(spill_m)
        or not math.isfinite(fraction)
        or not 0 < fraction < 1
    ):
        raise ValueError("invalid finite bed, basin, spill, or below-spill fraction")
    floor_cell = np.unravel_index(
        np.argmin(np.where(basin_mask, bed, np.inf)), bed.shape
    )
    floor = float(bed[floor_cell])
    if spill_m <= floor:
        raise ValueError("spill must be above the basin floor")
    level = floor + fraction * (spill_m - floor)
    labels, _ = ndimage.label(bed < level, D4)
    wet = labels == labels[floor_cell]
    if any((wet[0].any(), wet[-1].any(), wet[:, 0].any(), wet[:, -1].any())):
        raise ValueError(
            "initial lake connects to the domain perimeter; refusing an uncontained lake"
        )
    depth = np.where(wet, level - bed, 0.0)
    return depth, {
        "kind": "pre-existing floor-connected lake; not formed by this rainfall",
        "water_surface_elevation_m": level,
        "analytical_spill_elevation_m": spill_m,
        "headroom_below_analytical_spill_m": spill_m - level,
        "fraction_of_floor_to_spill_height": fraction,
        "floor_elevation_m": floor,
        "floor_cell_xz": [int(floor_cell[1]), int(floor_cell[0])],
        "wet_cells": int(wet.sum()),
        "connected_by": "D4 on the entire native numerical DEM",
        "initial_momentum": "zero",
        "terrain_modified": False,
    }


def checked_output(path: Path) -> Path:
    root = ROOT / "outputs/fluid"
    resolved = path.resolve()
    if (
        resolved.parent != root.resolve()
        or not resolved.name.startswith("seeded-lake-v1-")
        or path.is_symlink()
        or path.exists()
    ):
        raise ValueError(
            "--out must be a fresh seeded-lake-v1-* leaf directly under outputs/fluid"
        )
    return resolved


def prepare(out: Path, fraction: float):
    previous = recession.load()
    ref = recession.reference(previous)
    source_identity = previous["terrain_source_identity"]
    baseline = Path(previous["baseline_case"])
    bed, _ = ref._grid_from_native_ids(baseline / "native/input/field/z.dat", 512, 512)
    masks, mask_identity = ref.fixed_depression_masks()
    summary = json.loads(Path(mask_identity["summary_path"]).read_text())
    basin = next(b for b in summary["basins"]["top_components"] if b["label"] == 20)
    spill_lo, spill_hi = basin["derived_spill_elevation_range_m"]
    if spill_lo != spill_hi:
        raise ValueError(
            "selected basin does not have a single analytical spill elevation"
        )
    depth, initial = lake_depth(bed, masks["label-20"], spill_lo, fraction)
    initial.update(
        basin_label=20,
        initial_volume_m3=float(depth.sum()) * 900,
        initial_surface_area_km2=initial["wet_cells"] * 0.0009,
        initial_max_depth_m=float(depth.max()),
    )
    out.mkdir(exist_ok=False)
    cases = []
    for name, duration, rain in (
        ("lake-hold", 1800, 0.0),
        ("lake-rain", 7200, 120.0 / 3_600_000),
    ):
        spec = {
            "name": name,
            "family": "seeded-mountain-lake",
            "rows": 512,
            "cols": 512,
            "dx_m": 30.0,
            "duration_s": duration,
            "output_interval_s": 60,
            "rain_rate_m_per_s": rain,
            "rainfall_history": [[0, rain], [duration, rain]],
            "boundary": "fall",
            "manning_n": 0.05,
            "crop_xzwh": [1152, 1408, 512, 512],
            "initial_condition": initial,
            "purpose": "no-rain lake retention check"
            if not rain
            else "established rainfall feeding a pre-existing lake",
        }
        case = out / "cases" / name
        case.mkdir(parents=True, exist_ok=False)
        shutil.copyfile(baseline / "DEM.asc", case / "DEM.asc")
        if (
            presentation.sha256_file(case / "DEM.asc")
            != previous["baseline_dem_sha256"]
        ):
            raise ValueError("source DEM differs from the established rain demo")
        np.save(case / "initial-depth.npy", depth, allow_pickle=False)
        write(case / "case-spec.json", spec)
        write(
            case / "case-protocol.json",
            {
                "schema": SCHEMA,
                "case": spec,
                "terrain_source_identity": source_identity,
                "input_dem_ascii_sha256": previous["baseline_dem_sha256"],
                "settings": {"rain_source_schedule": spec["rainfall_history"]},
                "expected_saved_times_s": list(range(0, duration + 1, 60)),
                "initial_depth_sha256": presentation.sha256_file(
                    case / "initial-depth.npy"
                ),
                "native_bed_sha256": presentation.sha256_file(
                    baseline / "native/input/field/z.dat"
                ),
                "runner_sha256": presentation.sha256_file(Path(__file__)),
                "frozen_before_native_execution": True,
            },
        )
        cases.append(spec)
    write(
        out / "protocol.json",
        {
            "schema": SCHEMA,
            "terrain_source_identity": source_identity,
            "native_backend_identity": previous["native_backend_identity"],
            "analytical_basin_identity": mask_identity,
            "initial_condition": initial,
            "cases": cases,
            "native_timeout_s_per_case": 240,
            "hold_gates": {
                "relative_storage_change_max": 0.001,
                "core_surface_p95_error_max_m": 0.05,
                "core_water_weighted_speed_max_m_per_s": 0.02,
            },
            "human_visual_acceptance": "pending",
            "limits": "Pre-existing lake, not rain-formed or calibrated hydrology. No infiltration, evaporation or erosion.",
        },
    )
    return cases


def child(case: Path, *, study_prefix="seeded-lake-v1-", launch_source=None):
    """Shared native serializer/runner; defaults preserve the seeded experiment."""
    launch_source = Path(__file__) if launch_source is None else Path(launch_source)
    case = case.resolve()
    if (
        case.parent.name != "cases"
        or case.parents[2] != (ROOT / "outputs/fluid").resolve()
        or not case.parents[1].name.startswith(study_prefix)
        or (case / "native").exists()
    ):
        raise ValueError("native execution requires a fresh prepared lake case")
    protocol = json.loads((case / "case-protocol.json").read_text())
    if protocol["runner_sha256"] != presentation.sha256_file(launch_source):
        raise ValueError("runner changed after preparing the case")
    if protocol.get(
        "native_helper_sha256", presentation.sha256_file(Path(__file__))
    ) != presentation.sha256_file(Path(__file__)):
        raise ValueError("native helper changed after preparing the case")
    if (
        presentation.sha256_file(case / "initial-depth.npy")
        != protocol["initial_depth_sha256"]
    ):
        raise ValueError("initial depth changed after preparing the case")
    ref = recession.reference(recession.load())
    spec = protocol["case"]
    from synxflow import flood
    from synxflow.IO.InputModel import InputModel

    model = InputModel(dem_data=str(case / "DEM.asc"), case_folder=str(case / "native"))
    initial = np.load(case / "initial-depth.npy", allow_pickle=False)
    model.set_initial_condition("h0", initial)
    model.set_initial_condition("hU0x", np.zeros_like(initial))
    model.set_initial_condition("hU0y", np.zeros_like(initial))
    model.set_boundary_condition([], outline_boundary="fall")
    model.set_rainfall(rain_mask=0, rain_source=np.asarray(spec["rainfall_history"]))
    model.set_grid_parameter(
        manning=0.05,
        sewer_sink=0.0,
        cumulative_depth=0.0,
        hydraulic_conductivity=0.0,
        capillary_head=0.0,
        water_content_diff=0.0,
    )
    model.set_runtime(
        [0, spec["duration_s"], spec["output_interval_s"], spec["duration_s"]]
    )
    model.set_device_no([0])
    model.write_input_files()
    field = case / "native/input/field"
    if presentation.sha256_file(field / "z.dat") != protocol["native_bed_sha256"]:
        raise ValueError("native numerical terrain differs from the established demo")
    serialized, _ = ref._grid_from_native_ids(field / "h.dat", 512, 512)
    if not np.allclose(serialized, initial, rtol=1e-5, atol=1e-7):
        raise ValueError("serialized initial lake differs from the declared seed")
    if np.any(ref._element_records(field / "hU.dat")[1] != 0):
        raise ValueError("initial momentum must be zero")
    for name in (
        "sewer_sink",
        "cumulative_depth",
        "hydraulic_conductivity",
        "capillary_head",
        "water_content_diff",
    ):
        if np.any(ref._element_values(field / f"{name}.dat") != 0):
            raise ValueError("unexpected interior source/sink")
    hashes = ref._input_hashes(case / "native/input")
    baseline = recession.load()["baseline_native_input_sha256"]
    changed = sorted(k for k, v in hashes.items() if baseline.get(k) != v)
    if set(hashes) != set(baseline) or not set(changed) <= {
        "field/h.dat",
        "times_setup.dat",
        "field/precipitation_source_all.dat",
    }:
        raise ValueError(f"unexpected change to native reference inputs: {changed}")
    audit = {
        "native_input_sha256": hashes,
        "changed_paths_vs_dry_start_rain_reference": changed,
        "z_field_sha256": presentation.sha256_file(field / "z.dat"),
        "z_element_count": int(initial.size),
        "serialized_rainfall_history": np.loadtxt(
            field / "precipitation_source_all.dat", skiprows=1
        ).tolist(),
        "rain_source_sha256": presentation.sha256_file(
            field / "precipitation_source_all.dat"
        ),
        "declared_initial_condition": spec["initial_condition"],
        "serialized_initial_volume_m3": float(serialized.sum()) * 900,
    }
    write(case / "native-preflight.json", audit)
    started = time.monotonic()
    flood.run(str(case / "native"))
    elapsed = time.monotonic() - started
    if hashes != ref._input_hashes(case / "native/input"):
        raise ValueError("native inputs mutated during execution")
    write(
        case / "native-postrun.json",
        {"native_call_wall_s": elapsed, "unchanged_inputs": True},
    )


def analyze(case: Path, ref):
    spec = json.loads((case / "case-spec.json").read_text())
    _, bed = ref._grid_from_native_ids(case / "native/input/field/z.dat", 512, 512)
    core = np.load(case / "initial-depth.npy", allow_pickle=False) >= 1.0
    paths = ref.snapshot_paths(case / "native/output")
    times = list(range(0, spec["duration_s"] + 1, 60))
    if any(sorted(paths[f]) != times for f in ("h", "hUx", "hUy")):
        raise ValueError("missing or unexpected saved native states")
    rows = []
    for t in times:
        h, qx, qy = [ref.read_ascii(paths[f][t])[1] for f in ("h", "hUx", "hUy")]
        if (
            not all(np.isfinite(f).all() for f in (h, qx, qy))
            or (h < 0).any()
            or ((h == 0) & ((qx != 0) | (qy != 0))).any()
        ):
            raise ValueError(f"unhealthy native fields at {t}s")
        volume = float(h.sum()) * 900
        nominal_input = (
            spec["initial_condition"]["initial_volume_m3"]
            + spec["rain_rate_m_per_s"] * t * h.size * 900
        )
        if volume > nominal_input * 1.001 + h.size * 900 * 0.5e-6:
            raise ValueError(
                "storage exceeds initial water plus nominal rainfall allowance"
            )
        denom = float(h[core].sum())
        rows.append(
            {
                "time_s": t,
                "stored_volume_m3": volume,
                "material_wet_area_km2": int((h >= 0.01).sum()) * 0.0009,
                "core_surface_p95_error_from_initial_level_m": float(
                    np.percentile(
                        np.abs(
                            bed[core].astype(np.float64)
                            + h[core]
                            - spec["initial_condition"]["water_surface_elevation_m"]
                        ),
                        95,
                    )
                ),
                "core_water_weighted_speed_m_per_s": float(
                    np.hypot(qx[core], qy[core]).sum()
                )
                / denom
                if denom
                else None,
                "max_depth_m": float(h.max()),
            }
        )
    result = {
        "observations": rows,
        "all_saved_fields_healthy": True,
        "initial_depth_verified_at_time_zero": bool(
            np.allclose(
                ref.read_ascii(paths["h"][0])[1],
                np.load(case / "initial-depth.npy", allow_pickle=False),
                rtol=1e-5,
                atol=1e-6,
            )
        ),
        "initial_volume_accounted_for": True,
    }
    if not result["initial_depth_verified_at_time_zero"]:
        raise ValueError(
            "saved initial depth differs from the declared pre-existing lake"
        )
    if spec["rain_rate_m_per_s"] == 0:
        gates = json.loads((case.parents[1] / "protocol.json").read_text())[
            "hold_gates"
        ]
        drift = max(
            abs(r["stored_volume_m3"] / rows[0]["stored_volume_m3"] - 1) for r in rows
        )
        result["hold_check"] = {
            "relative_storage_change_max": drift,
            "core_surface_p95_error_max_m": max(
                r["core_surface_p95_error_from_initial_level_m"] for r in rows
            ),
            "core_water_weighted_speed_max_m_per_s": max(
                r["core_water_weighted_speed_m_per_s"] for r in rows
            ),
        }
        result["hold_check"]["pass"] = all(
            result["hold_check"][k] <= limit for k, limit in gates.items()
        )
    return result


def run_case(case: Path):
    spec = json.loads((case / "case-spec.json").read_text())
    cache = case / "cache"
    environment = {
        **os.environ,
        "PYTHONDONTWRITEBYTECODE": "1",
        "MPLCONFIGDIR": str(cache / "matplotlib"),
        "XDG_CACHE_HOME": str(cache / "xdg"),
    }
    command = [
        "rtk",
        "proxy",
        str(PYTHON),
        "-B",
        "-u",
        str(Path(__file__).resolve()),
        "--child",
        str(case),
    ]
    print("running " + spec["name"], flush=True)
    with (
        (case / "native.stdout").open("x") as stdout,
        (case / "native.stderr").open("x") as stderr,
    ):
        process = subprocess.run(
            command,
            cwd=ROOT,
            env=environment,
            stdout=stdout,
            stderr=stderr,
            timeout=240,
            check=False,
        )
    if (
        process.returncode
        or "Simulation successfully finished!"
        not in (case / "native.stdout").read_text()
    ):
        raise RuntimeError(
            "native solver failed; inspect " + str(case / "native.stderr")
        )
    ref = recession.reference(recession.load())
    metrics = analyze(case, ref)
    audit = json.loads((case / "native-preflight.json").read_text())
    post = json.loads((case / "native-postrun.json").read_text())
    write(
        case / "case-result.json",
        {
            "schema": SCHEMA,
            "case": spec,
            "status": "healthy",
            "metrics": metrics,
            "native_call_wall_s": post["native_call_wall_s"],
            "input_provenance": {
                "case_dem_ascii_sha256": presentation.sha256_file(case / "DEM.asc"),
                "native_input_audit_before_solver": audit,
                "native_inputs_unchanged_during_run": post["unchanged_inputs"],
            },
        },
    )
    print(
        json.dumps(
            {
                "case": spec["name"],
                "native_call_s": post["native_call_wall_s"],
                "hold_check": metrics.get("hold_check"),
                "final": metrics["observations"][-1],
            }
        ),
        flush=True,
    )
    recording = converter.convert_case(
        case, case.parents[1] / "recordings" / spec["name"]
    )
    print("converted " + str(recording), flush=True)
    if spec["name"] == "lake-hold" and not metrics["hold_check"]["pass"]:
        raise RuntimeError(
            "lake failed the declared no-rain hold check; do not present it as stable"
        )


def capture(out: Path):
    media = out / "media"
    media.mkdir(exist_ok=False)
    runtime = presentation.runtime_identity()
    assets = []
    scenes = (
        ("lake-hold", 0, "collection", "initial-lake", -0.52),
        ("lake-hold", 1800, "collection", "held-lake", -0.52),
        ("lake-rain", 7200, "collection", "lake-with-rain", -0.52),
        ("lake-rain", 7200, "collection", "lake-reverse", 2.62),
        ("lake-rain", 7200, "overview", "rain-overview", -0.52),
    )
    for case, timestamp, camera, name, yaw in scenes:
        asset = presentation.still_asset(case, camera, timestamp)
        asset.update(width=1280, height=720)
        command = presentation.app_command(
            asset, media / f"{name}.png", "scenic", False
        )
        command[command.index("--fluid25d-recording") + 1] = str(
            out / "recordings" / case / "recording.json"
        )
        command += [
            "--fluid25d-scenic-material",
            "macro",
            "--fluid25d-native-camera-yaw",
            str(yaw),
        ]
        _, receipt = presentation.run_logged(
            command,
            media / "logs",
            name,
            expected_asset=asset,
            expected_presentation="scenic",
        )
        assets.append(
            {
                "name": name,
                "path": f"media/{name}.png",
                "sha256": presentation.sha256_file(media / f"{name}.png"),
                "receipt": receipt,
            }
        )
        print("captured " + name, flush=True)
    asset = presentation.diagnostic_asset("lake-hold", "depth", 1800)
    asset.update(width=1280, height=720)
    command = presentation.app_command(
        asset, media / "lake-depth.png", "original", False
    )
    command[command.index("--fluid25d-recording") + 1] = str(
        out / "recordings/lake-hold/recording.json"
    )
    _, receipt = presentation.run_logged(
        command,
        media / "logs",
        "lake-depth",
        expected_asset=asset,
        expected_presentation="original",
    )
    assets.append(
        {
            "name": "lake-depth",
            "path": "media/lake-depth.png",
            "sha256": presentation.sha256_file(media / "lake-depth.png"),
            "receipt": receipt,
        }
    )
    asset = presentation.video_asset(
        "rain-to-lake", "lake-rain", "collection", 0, 121, 10, 60
    )
    asset.update(width=1280, height=720)
    command = presentation.app_command(
        asset, media / "rain-to-lake.mp4", "scenic", False
    )
    command[command.index("--fluid25d-recording") + 1] = str(
        out / "recordings/lake-rain/recording.json"
    )
    command += ["--fluid25d-scenic-material", "macro"]
    _, receipt = presentation.run_logged(
        command,
        media / "logs",
        "rain-to-lake",
        expected_asset=asset,
        expected_presentation="scenic",
    )
    assets.append(
        {
            "name": "rain-to-lake",
            "path": "media/rain-to-lake.mp4",
            "sha256": presentation.sha256_file(media / "rain-to-lake.mp4"),
            "receipt": receipt,
        }
    )
    presentation.assert_same_runtime(
        runtime, presentation.runtime_identity(), "lake capture"
    )
    write(
        out / "capture-manifest.json",
        {
            "runtime_identity": runtime,
            "assets": assets,
            "capture_runner_sha256": presentation.sha256_file(Path(__file__)),
            "numerical_protocol_sha256": presentation.sha256_file(
                out / "protocol.json"
            ),
            "human_visual_acceptance": "pending",
        },
    )


def report(out: Path):
    protocol = json.loads((out / "protocol.json").read_text())
    hold, rain = [
        json.loads((out / "cases" / name / "case-result.json").read_text())
        for name in ("lake-hold", "lake-rain")
    ]
    initial = protocol["initial_condition"]
    parts = [
        "<!doctype html><html lang='en'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>",
        "<title>Pre-existing lake on the mountain terrain</title><style>body{font:16px system-ui;background:#18212a;color:#e2e8ed;max-width:1200px;margin:auto;padding:24px}a{color:#8acbff}img,video{max-width:100%;height:auto}section{margin:32px 0}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#101820;padding:16px}.pair{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px}</style>",
        "<h1>A lake already exists; rain feeds it</h1><p>This is the same unmodified Terrain Diffusion mountain crop and unchanged released SynxFlow solver. Water was explicitly initialized inside the existing basin, not formed by the rain shown here. Existing material, shoreline and solver defaults are unchanged.</p>",
        f"<p>Initial lake: {initial['initial_surface_area_km2']:.2f} km², {initial['initial_volume_m3'] / 1e6:.2f} million m³. Horizontal surface at {initial['water_surface_elevation_m']:.2f} m, {initial['headroom_below_analytical_spill_m']:.2f} m below the analytical spill elevation. Depth varies with the existing bed; there is no maintained level or artificial dam.</p>",
        "<section><h2>Does the lake hold?</h2><p>Matched camera, without rain or interior sources/drains: initial condition at left; 30 simulated minutes later at right.</p><div class='pair'>",
        "<a href='media/initial-lake.png'><img src='media/initial-lake.png' alt='Initialized lake'></a><a href='media/held-lake.png'><img src='media/held-lake.png' alt='Lake after thirty minutes without rain'></a></div>",
        "<pre>"
        + html.escape(json.dumps(hold["metrics"]["hold_check"], indent=2))
        + "</pre></section>",
        "<section><h2>Streams feeding the lake</h2><p>Uniform 120 mm/hour rainfall, the established heavy demonstration case, starts over the whole scene. The lake is already present at time zero. This recording shows two physical hours in 12.1 seconds; native states are saved every minute and held between updates. It is not a continuously running live simulation.</p><video controls preload='metadata' poster='media/lake-with-rain.png' src='media/rain-to-lake.mp4'></video><a href='media/lake-with-rain.png'><img src='media/lake-with-rain.png' alt='Existing lake and incoming runoff after two hours of rain'></a></section>",
        "<section><h2>Other views</h2><div class='pair'><figure><a href='media/lake-reverse.png'><img loading='lazy' src='media/lake-reverse.png' alt='Reverse lake view'></a><figcaption>Reverse view; same water state and material.</figcaption></figure><figure><a href='media/rain-overview.png'><img loading='lazy' src='media/rain-overview.png' alt='Whole terrain overview'></a><figcaption>Whole crop: rain-generated streams plus the pre-existing lake.</figcaption></figure></div><a href='media/lake-depth.png'><img loading='lazy' src='media/lake-depth.png' alt='Raw water depth diagnostic'></a><p>Raw depth diagnostic; the broad patch is actual initialized water, not a rendering-only plane.</p></section>",
        "<h2>Evidence and limits</h2><p>No terrain carving, closed perimeter, source, drain, shader changes, solver changes or default promotion. No infiltration, evaporation or erosion. This is a visual demonstration, not calibrated hydrology; human visual acceptance remains pending.</p>",
        "<p><a href='protocol.json'>Protocol / initial condition</a> · <a href='cases/lake-hold/case-result.json'>Hold results</a> · <a href='cases/lake-rain/case-result.json'>Rain results</a> · <a href='capture-manifest.json'>Capture receipts</a></p>",
        "<p>Final rain-case measurements:</p><pre>"
        + html.escape(json.dumps(rain["metrics"]["observations"][-1], indent=2))
        + "</pre>",
        "<p>Local GUI replay (recorded, not live):</p><pre>rtk proxy build/dev/projects/fluid/fluid_25d/fluid_25d --fluid25d-recording "
        + html.escape(
            str((out / "recordings/lake-rain/recording.json").relative_to(ROOT))
        )
        + " --fluid25d-native-presentation scenic --fluid25d-scenic-material macro --fluid25d-recording-camera collection --fluid25d-recording-speed 60</pre></html>",
    ]
    presentation.write_text_exclusive(out / "index.html", "\n".join(parts))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--level-fraction", type=float, default=0.9)
    parser.add_argument("--child", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--capture-only", action="store_true")
    args = parser.parse_args()
    if args.child:
        child(args.child)
        return
    if not args.out:
        parser.error("--out is required")
    if args.capture_only:
        out = args.out.resolve()
        if out.parent != (ROOT / "outputs/fluid").resolve() or not out.name.startswith(
            "seeded-lake-v1-"
        ):
            parser.error("invalid lake output leaf")
    else:
        out = checked_output(args.out)
        if shutil.disk_usage(out.parent).free < 5 * 1024**3:
            raise ValueError("less than 5 GiB free for the bounded lake experiment")
        specs = prepare(out, args.level_fraction)
        print("prepared " + str(out), flush=True)
        for spec in specs:
            run_case(out / "cases" / spec["name"])
    capture(out)
    report(out)
    print("report " + str(out / "index.html"), flush=True)


if __name__ == "__main__":
    main()
