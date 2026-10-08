#!/usr/bin/env python3
"""Bounded 512/1024 mm/h dry-start storms against the frozen 480 reference.

Equal nominal rain input and recession windows on the unchanged mountain DEM.
Preserves all previous studies and numerical/render defaults.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import time
from pathlib import Path

import convert_synxflow_recording_v1 as converter
import numpy as np
import run_native_presentation_v1 as presentation
import run_native_rain_recession_v1 as recession
import run_natural_lakes_v1 as natural
import run_rain_acceleration_v1 as previous
import run_seeded_lake_v1 as native

ROOT = presentation.ROOT
SCHEMA = "cubey.fluid25d.rain_power2.v1"
PREFIX = "rain-power2-v1-"
REFERENCE_ROOT = ROOT / "outputs/fluid/rain-acceleration-v1-20261008-a1"
REFERENCE_CASE = REFERENCE_ROOT / "cases/rain480"
RATES = (480, 512, 1024)
CADENCE = 225
COMPARE_TIME = 2700


def encode_rain_history(history):
    """Native one-region text format, with round-trip-safe time/rate precision."""
    return "1\n" + "".join(f"{t:.17g} {rate:.17g}\n" for t, rate in history)


def child(case):
    """Unchanged released solver; serialize this study's fractional knots exactly.

    InputModel uses six significant timestamp digits. Only its freshly written
    precipitation table is re-encoded; no installed source or solver is patched.
    The remaining setup and reference-input checks follow the seeded helper.
    """
    case = case.resolve()
    if (
        case.parent.name != "cases"
        or case.parents[2] != (ROOT / "outputs/fluid").resolve()
        or not case.parents[1].name.startswith(PREFIX)
        or (case / "native").exists()
    ):
        raise ValueError("native execution requires a fresh prepared power2 case")
    protocol = json.loads((case / "case-protocol.json").read_text())
    if protocol["runner_sha256"] != presentation.sha256_file(
        Path(__file__)
    ) or protocol["native_helper_sha256"] != presentation.sha256_file(
        Path(native.__file__)
    ):
        raise ValueError("frozen runner or helper changed")
    if (
        presentation.sha256_file(case / "initial-depth.npy")
        != protocol["initial_depth_sha256"]
    ):
        raise ValueError("initial depth changed after preparation")
    initial = np.load(case / "initial-depth.npy", allow_pickle=False)
    if initial.shape != (512, 512) or np.any(initial != 0):
        raise ValueError("power2 cases require an exactly dry initial condition")
    source = recession.load()
    ref = recession.reference(source)
    s = protocol["case"]
    from synxflow import flood
    from synxflow.IO.InputModel import InputModel

    model = InputModel(dem_data=str(case / "DEM.asc"), case_folder=str(case / "native"))
    model.set_initial_condition("h0", initial)
    model.set_initial_condition("hU0x", np.zeros_like(initial))
    model.set_initial_condition("hU0y", np.zeros_like(initial))
    model.set_boundary_condition([], outline_boundary="fall")
    model.set_rainfall(rain_mask=0, rain_source=np.asarray(s["rainfall_history"]))
    model.set_grid_parameter(
        manning=0.05,
        sewer_sink=0.0,
        cumulative_depth=0.0,
        hydraulic_conductivity=0.0,
        capillary_head=0.0,
        water_content_diff=0.0,
    )
    model.set_runtime([0, s["duration_s"], s["output_interval_s"], s["duration_s"]])
    model.set_device_no([0])
    model.write_input_files()
    field = case / "native/input/field"
    rain_path = field / "precipitation_source_all.dat"
    exported = np.loadtxt(rain_path, skiprows=1)
    expected = np.asarray(s["rainfall_history"])
    if (
        exported.shape != expected.shape
        or not np.allclose(exported[:, 0], expected[:, 0], rtol=0, atol=0.005001)
        or not np.allclose(
            exported[:, 1],
            expected[:, 1],
            rtol=converter.RAIN_HISTORY_REL_TOL,
            atol=converter.RAIN_HISTORY_ABS_TOL,
        )
    ):
        raise ValueError("official rain exporter differs beyond its known precision")
    rain_path.write_text(encode_rain_history(s["rainfall_history"]), encoding="ascii")
    serialized_rain = np.loadtxt(rain_path, skiprows=1)
    if not np.array_equal(serialized_rain, expected):
        raise ValueError("rain schedule did not round-trip exactly")
    if presentation.sha256_file(field / "z.dat") != protocol["native_bed_sha256"]:
        raise ValueError("native terrain differs from the frozen numerical bed")
    serialized_h, _ = ref._grid_from_native_ids(field / "h.dat", 512, 512)
    if np.any(serialized_h != 0) or np.any(
        ref._element_records(field / "hU.dat")[1] != 0
    ):
        raise ValueError("serialized initial depth or momentum is not zero")
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
    baseline = source["baseline_native_input_sha256"]
    changed = sorted(k for k, v in hashes.items() if baseline.get(k) != v)
    if set(hashes) != set(baseline) or set(changed) != {
        "times_setup.dat",
        "field/precipitation_source_all.dat",
    }:
        raise ValueError(f"unexpected native input change: {changed}")
    native.write(
        case / "native-preflight.json",
        {
            "native_input_sha256": hashes,
            "changed_paths_vs_dry_start_rain_reference": changed,
            "z_field_sha256": presentation.sha256_file(field / "z.dat"),
            "z_element_count": int(initial.size),
            "serialized_rainfall_history": serialized_rain.tolist(),
            "rain_source_sha256": presentation.sha256_file(rain_path),
            "rain_table_encoding": "one uniform region; float64 round-trip-safe .17g timestamps and rates",
            "declared_initial_condition": s["initial_condition"],
            "serialized_initial_volume_m3": 0.0,
        },
    )
    started = time.monotonic()
    flood.run(str(case / "native"))
    elapsed = time.monotonic() - started
    if hashes != ref._input_hashes(case / "native/input"):
        raise ValueError("native inputs changed during execution")
    native.write(
        case / "native-postrun.json",
        {"native_call_wall_s": elapsed, "unchanged_inputs": True},
    )


def case_spec(rate):
    if rate == 480:
        return previous.case_spec(480)
    if rate not in (512, 1024):
        raise ValueError("only the frozen 480, 512 and 1024 mm/h cases are supported")
    storm_end = 960 * 3600 // rate
    end = storm_end + 14400
    flux = rate / 3_600_000
    # A triangular taper supplies the same extra 1 mm at every rate.
    taper_end = storm_end + 7200 / rate
    s = natural.spec()
    s.update(
        name=f"rain{rate}",
        family="power2-equal-total-rain",
        duration_s=end,
        output_interval_s=CADENCE,
        rain_rate_m_per_s=flux,
        rain_rate_mm_per_h=rate,
        rainfall_history=[[0, flux], [storm_end, flux], [taper_end, 0], [end, 0]],
        storm_end_s=storm_end,
        taper_end_s=taper_end,
        purpose="equal 961 mm scheduled total; four-hour recession window including taper",
    )
    return s


def reference_identity():
    return {
        str((REFERENCE_ROOT / rel).relative_to(ROOT)): presentation.sha256_file(
            REFERENCE_ROOT / rel
        )
        for rel in (
            "protocol.json",
            "comparison.json",
            "cases/rain480/case-protocol.json",
            "cases/rain480/case-result.json",
            "cases/rain480/recording/recording.json",
        )
    }


def helper_identity():
    return {
        str(
            Path(module.__file__).resolve().relative_to(ROOT)
        ): presentation.sha256_file(Path(module.__file__))
        for module in (previous, natural, native, converter)
    }


def prepare(out):
    if (
        out.parent != (ROOT / "outputs/fluid").resolve()
        or not out.name.startswith(PREFIX)
        or out.exists()
        or out.is_symlink()
    ):
        raise ValueError(
            "output must be a fresh rain-power2-v1-* leaf under outputs/fluid"
        )
    if shutil.disk_usage(out.parent).free < 8 * 1024**3:
        raise ValueError("less than 8 GiB available for this bounded study")
    protocol = recession.load()
    ref = recession.reference(protocol)
    _, mask_identity = ref.fixed_depression_masks()
    reference_protocol = json.loads((REFERENCE_CASE / "case-protocol.json").read_text())
    if (
        reference_protocol["case"] != case_spec(480)
        or reference_protocol["analytical_depression_identity"] != mask_identity
    ):
        raise ValueError("480 reference case or analytical masks changed")
    cases = [case_spec(rate) for rate in RATES]
    previous.assert_equal_total(cases)
    out.mkdir(exist_ok=False)
    presentation.write_text_exclusive(
        out / "runner-frozen.py", Path(__file__).read_text()
    )
    frozen = {
        "schema": SCHEMA,
        "cases": cases,
        "reference_identity": reference_identity(),
        "helper_identity": helper_identity(),
        "runner_sha256": presentation.sha256_file(Path(__file__)),
        "terrain_source_identity": protocol["terrain_source_identity"],
        "native_backend_identity": protocol["native_backend_identity"],
        "analytical_depression_identity": mask_identity,
        "nominal_total_rain_mm": 961,
        "formation_target": {"basin_label": 20, "min_depth_m": 1, "area_km2": 0.5},
        "comparison_time_s": COMPARE_TIME,
        "comparison_policy": "same physical time while all storms are on; same main-storm rain input; same four-hour recession window",
        "new_saved_state_cadence_s": CADENCE,
        "reference_saved_state_cadence_s": 300,
        "video_render_interval_s": CADENCE,
        "video_fps": 6,
        "rain_table_encoding": "one uniform region; float64 round-trip-safe .17g timestamps and rates",
        "native_timeout_s_per_case": 300,
        "frozen_before_native_execution": True,
        "human_visual_acceptance": "pending",
        "limits": "Extreme visual-demo stress forcing, not realistic weather or calibrated hydrology. No infiltration, evaporation, erosion or outside-crop catchment. Retained collections are not automatically calm permanent lakes. Different save cadences and single shared-GPU runs prevent strict performance comparisons.",
    }
    native.write(out / "protocol.json", frozen)
    source = Path(protocol["baseline_case"])
    for s in cases[1:]:
        case = out / "cases" / s["name"]
        case.mkdir(parents=True, exist_ok=False)
        shutil.copyfile(source / "DEM.asc", case / "DEM.asc")
        if (
            presentation.sha256_file(case / "DEM.asc")
            != protocol["baseline_dem_sha256"]
        ):
            raise ValueError("source DEM differs from the frozen reference")
        np.save(case / "initial-depth.npy", np.zeros((512, 512)), allow_pickle=False)
        native.write(case / "case-spec.json", s)
        native.write(
            case / "case-protocol.json",
            {
                "schema": SCHEMA,
                "case": s,
                "terrain_source_identity": frozen["terrain_source_identity"],
                "native_backend_identity": frozen["native_backend_identity"],
                "input_dem_ascii_sha256": protocol["baseline_dem_sha256"],
                "settings": {"rain_source_schedule": s["rainfall_history"]},
                "expected_saved_times_s": list(
                    range(0, s["duration_s"] + 1, s["output_interval_s"])
                ),
                "initial_depth_sha256": presentation.sha256_file(
                    case / "initial-depth.npy"
                ),
                "native_bed_sha256": presentation.sha256_file(
                    source / "native/input/field/z.dat"
                ),
                "runner_sha256": frozen["runner_sha256"],
                "native_helper_sha256": presentation.sha256_file(Path(native.__file__)),
                "analytical_depression_identity": mask_identity,
                "frozen_before_native_execution": True,
            },
        )
    return cases


def summarize(states, s, bed, labels):
    mask = labels == 20
    if not mask.any():
        raise ValueError("fixed B20 observation mask is missing")
    rows = []
    snapshots = {COMPARE_TIME, s["storm_end_s"], s["duration_s"]}
    for t, h, qx, qy in states:
        natural.validate_fields(h, qx, qy)
        if t == 0 and any(np.any(f != 0) for f in (h, qx, qy)):
            raise ValueError("time zero is not completely dry")
        _, depth, phase = recession.history_value(s["rainfall_history"], t)
        volume = float(h.sum()) * 900
        if volume > depth * h.size * 900 * 1.001 + h.size * 900 * 0.5e-6:
            raise ValueError(
                "storage exceeds scheduled rain plus serialization allowance"
            )
        row = {
            "time_s": t,
            "rain_phase": phase,
            "stored_m3": volume,
            "max_depth_m": float(h.max()),
            "material_wet_area_km2": int((h >= 0.1).sum()) * 0.0009,
            "b20_area_at_least_1m_km2": int(((h >= 1) & mask).sum()) * 0.0009,
            "b20_stored_m3": float(h[mask].sum()) * 900,
        }
        if t in snapshots:
            row.update(natural.field_metrics(bed, h, qx, qy, labels))
        rows.append(row)
    if [r["time_s"] for r in rows] != list(
        range(0, s["duration_s"] + 1, s["output_interval_s"])
    ):
        raise ValueError("missing or unexpected saved states")
    return {
        "all_saved_fields_healthy": True,
        "dry_start_verified_at_time_zero": True,
        "formation_target_first_crossing": previous.first_crossing(
            rows, "b20_area_at_least_1m_km2", 0.5
        ),
        "peak_material_wet_area_km2": max(r["material_wet_area_km2"] for r in rows),
        "observations": rows,
        "diagnostic_snapshots": [r for r in rows if r["time_s"] in snapshots],
    }


def run_case(case, s, ref, labels):
    print(
        f"running {s['rain_rate_mm_per_h']} mm/h × {s['storm_end_s'] / 60:g} min + four-hour recession",
        flush=True,
    )
    environment = {
        **os.environ,
        "PYTHONDONTWRITEBYTECODE": "1",
        "MPLCONFIGDIR": str(case / "cache/matplotlib"),
        "XDG_CACHE_HOME": str(case / "cache/xdg"),
    }
    with (
        (case / "native.stdout").open("x") as stdout,
        (case / "native.stderr").open("x") as stderr,
    ):
        process = subprocess.run(
            [
                "rtk",
                "proxy",
                str(recession.PYTHON),
                "-B",
                "-u",
                str(Path(__file__).resolve()),
                "--child",
                str(case),
            ],
            cwd=ROOT,
            env=environment,
            stdout=stdout,
            stderr=stderr,
            timeout=300,
            check=False,
        )
    if (
        process.returncode
        or "Simulation successfully finished!"
        not in (case / "native.stdout").read_text()
    ):
        raise RuntimeError(
            "native execution failed; inspect " + str(case / "native.stderr")
        )
    paths = ref.snapshot_paths(case / "native/output")
    expected = list(range(0, s["duration_s"] + 1, s["output_interval_s"]))
    if any(sorted(paths[f]) != expected for f in ("h", "hUx", "hUy")):
        raise ValueError("native run omitted expected saved states")
    _, bed = ref._grid_from_native_ids(case / "native/input/field/z.dat", 512, 512)
    states = (
        (t, *(ref.read_ascii(paths[f][t])[1] for f in ("h", "hUx", "hUy")))
        for t in expected
    )
    metrics = summarize(states, s, bed.astype(np.float64), labels)
    audit = json.loads((case / "native-preflight.json").read_text())
    if set(audit["changed_paths_vs_dry_start_rain_reference"]) != {
        "field/precipitation_source_all.dat",
        "times_setup.dat",
    }:
        raise ValueError("unexpected change to fixed native inputs")
    post = json.loads((case / "native-postrun.json").read_text())
    result = {
        "schema": SCHEMA,
        "case": s,
        "status": "healthy",
        "metrics": metrics,
        "native_call_wall_s": post["native_call_wall_s"],
        "input_provenance": {
            "case_dem_ascii_sha256": presentation.sha256_file(case / "DEM.asc"),
            "native_input_audit_before_solver": audit,
            "native_inputs_unchanged_during_run": post["unchanged_inputs"],
        },
    }
    native.write(case / "case-result.json", result)
    recording = case / "recording"
    converter.convert_case(case, recording)
    reference_manifest = json.loads(
        (REFERENCE_CASE / "recording/recording.json").read_text()
    )
    if (
        json.loads((recording / "recording.json").read_text())["bed"]["sha256"]
        != reference_manifest["bed"]["sha256"]
    ):
        raise ValueError("recorded numerical bed differs from the reference")
    print(
        json.dumps(
            {
                "rate": s["rain_rate_mm_per_h"],
                "formation": metrics["formation_target_first_crossing"],
                "native_call_s": post["native_call_wall_s"],
                "peak_wet_area_km2": metrics["peak_material_wet_area_km2"],
            }
        ),
        flush=True,
    )
    return {
        "case": s,
        "metrics": metrics,
        "native_call_wall_s": post["native_call_wall_s"],
        "recording": str(recording),
        "reused_reference": False,
    }


def require_fresh_presentation_outputs(out):
    occupied = [
        name
        for name in ("media", "capture-manifest.json", "index.html")
        if (out / name).exists() or (out / name).is_symlink()
    ]
    if occupied:
        raise FileExistsError(
            "retain/move existing presentation artifacts before recapture: "
            + ", ".join(occupied)
        )


def capture(out, entries):
    require_fresh_presentation_outputs(out)
    media = out / "media"
    media.mkdir(exist_ok=False)
    presentation.write_text_exclusive(
        media / "capture-runner-frozen.py", Path(__file__).read_text()
    )
    presentation.write_text_exclusive(
        media / "capture-helper-frozen.py", Path(presentation.__file__).read_text()
    )
    runtime = presentation.runtime_identity()
    assets = []
    for entry in entries:
        s = entry["case"]
        rate = s["rain_rate_mm_per_h"]
        requests = [
            ("collection", t) for t in (COMPARE_TIME, s["storm_end_s"], s["duration_s"])
        ] + [("overview", s["duration_s"])]
        items = []
        for camera, t in requests:
            name = f"rain{rate}-{camera}-{t:05d}s"
            asset = presentation.still_asset(s["name"], camera, t)
            asset.update(
                width=1280, height=720, saved_field_interval_s=s["output_interval_s"]
            )
            items.append((name, "png", asset))
        name = f"rain{rate}-collection-formation"
        count = s["duration_s"] // CADENCE + 1
        asset = presentation.video_asset(
            name, s["name"], "collection", 0, count, 6, CADENCE
        )
        asset.update(
            width=1280,
            height=720,
            saved_field_interval_s=s["output_interval_s"],
            timeline=presentation.timeline(0, count, CADENCE, s["output_interval_s"]),
        )
        items.append((name, "mp4", asset))
        for name, suffix, asset in items:
            path = media / f"{name}.{suffix}"
            command = presentation.app_command(asset, path, "scenic", False)
            command[command.index("--fluid25d-recording") + 1] = str(
                Path(entry["recording"]) / "recording.json"
            )
            command += ["--fluid25d-scenic-material", "macro"]
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
                    "path": str(path.relative_to(out)),
                    "sha256": presentation.sha256_file(path),
                    "receipt": receipt,
                }
            )
            print("captured " + name, flush=True)
    presentation.assert_same_runtime(
        runtime, presentation.runtime_identity(), "power-of-two rainfall captures"
    )
    native.write(
        out / "capture-manifest.json",
        {
            "runtime_identity": runtime,
            "assets": assets,
            "capture_runner_sha256": presentation.sha256_file(Path(__file__)),
            "capture_helper_sha256": presentation.sha256_file(
                Path(presentation.__file__)
            ),
            "human_visual_acceptance": "pending",
        },
    )


def maps(out, entries):
    os.environ["MPLCONFIGDIR"] = str(out / "cache/matplotlib")
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import LightSource, LogNorm

    figure, axes = plt.subplots(3, 3, figsize=(16, 14), layout="constrained")
    extent = (0, 15.36, 15.36, 0)
    max_depth = max(
        r["max_depth_m"] for e in entries for r in e["metrics"]["diagnostic_snapshots"]
    )
    artist = None
    for column, entry in enumerate(entries):
        s = entry["case"]
        bed, frames = previous.recording_states(Path(entry["recording"]))
        wanted = {COMPARE_TIME, s["storm_end_s"], s["duration_s"]}
        depths = {t: h for t, h, _, _ in frames if t in wanted}
        shade = LightSource(315, 45).hillshade(bed, dx=30, dy=30)
        for row, (t, label) in enumerate(
            (
                (COMPARE_TIME, "At 45 min: all storms still on"),
                (s["storm_end_s"], "Main storm end: 960 mm supplied"),
                (s["duration_s"], "After 4 h recession: 961 mm total"),
            )
        ):
            axis = axes[row, column]
            axis.imshow(shade, cmap="gray", vmin=0, vmax=1, extent=extent)
            artist = axis.imshow(
                np.ma.masked_less(depths[t], 0.1),
                cmap="Blues",
                norm=LogNorm(0.1, max_depth),
                extent=extent,
            )
            axis.annotate(
                "B20",
                (14.2, 3.2),
                xytext=(-40, -20),
                textcoords="offset points",
                color="#ffc933",
                weight="bold",
                bbox={"facecolor": "#18212a", "alpha": 0.8, "pad": 2},
                arrowprops={"arrowstyle": "->", "color": "#ffc933"},
            )
            axis.set_title(
                f"{s['rain_rate_mm_per_h']} mm/h · {t / 60:g} min elapsed\n{label}"
            )
            axis.set_xlabel("Crop X (km)")
            axis.set_ylabel("Crop Z (km)")
    figure.colorbar(
        artist,
        ax=axes,
        label="Saved water depth (m), log scale; films below 0.1 m hidden",
        shrink=0.65,
    )
    figure.suptitle("Power-of-two rainfall: same terrain, same total rain", fontsize=18)
    figure.savefig(out / "media/water-comparison.png", dpi=110)
    plt.close(figure)
    figure, axes = plt.subplots(
        3, 1, figsize=(11, 10), sharex=True, layout="constrained"
    )
    for entry in entries:
        s = entry["case"]
        rows = entry["metrics"]["observations"]
        for axis, key, scale in zip(
            axes,
            ("b20_area_at_least_1m_km2", "material_wet_area_km2", "stored_m3"),
            (1, 1, 1e-6),
        ):
            (line,) = axis.plot(
                [r["time_s"] / 3600 for r in rows],
                [r[key] * scale for r in rows],
                label=f"{s['rain_rate_mm_per_h']} mm/h · {s['storm_end_s'] / 60:g} min storm",
            )
            axis.axvline(
                s["storm_end_s"] / 3600,
                color=line.get_color(),
                linestyle=":",
                alpha=0.65,
            )
    axes[0].axhline(0.5, color="gray", linestyle="--", label="Fixed 0.5 km² target")
    axes[0].legend()
    for axis, label in zip(
        axes,
        (
            "B20 area ≥1 m deep (km²)",
            "Whole-map wet area ≥0.1 m (km²)",
            "Whole-map storage (million m³)",
        ),
    ):
        axis.set_ylabel(label)
        axis.grid(alpha=0.3)
    axes[-1].set_xlabel("Physical simulation hours; dotted lines mark each storm end")
    figure.savefig(out / "media/formation-history.png", dpi=130)
    plt.close(figure)


def report(out, entries):
    parts = [
        "<!doctype html><html lang='en'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>",
        "<title>Power-of-two rain: 512 and 1024 mm/h</title><style>body{font:16px system-ui;background:#18212a;color:#e2e8ed;max-width:1400px;margin:auto;padding:24px}a{color:#8acbff}img,video{max-width:100%;height:auto}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px}figure{margin:0}table{border-collapse:collapse}th,td{border:1px solid #52606d;padding:8px;text-align:right}section{margin:32px 0}@media(max-width:800px){.grid{grid-template-columns:1fr}table{font-size:13px}}</style>",
        "<h1>Power-of-two rainfall: faster collection or just more flooding?</h1><p>Two new dry-start native SynxFlow runs: 512 and 1024 mm/h, with the existing 480 mm/h run as reference. Same unmodified 15.36 × 15.36 km mountain crop, native numerical bed, Manning coefficient, fall boundary and Cubey render settings. Each main storm supplies 960 mm, a proportionally shortened taper adds exactly 1 mm, and the run ends four hours after its main storm ends. These are deliberate visual stress cases, not realistic weather or calibrated hydrology.</p>",
        "<p>The same frozen formation target is used: B20 has at least 0.5 km² covered by water at least 1 m deep. New states save every 225 s (3.75 min), chosen to hit both exact storm ends and recession ends. The unchanged 480 reference saves every 300 s. Formation times are observed crossing brackets, not exact onsets.</p>",
        "<table><tr><th>Rain</th><th>Main storm</th><th>Formation target</th><th>Peak wet area ≥0.1 m</th><th>Final storage</th><th>Native call</th></tr>",
    ]
    for e in entries:
        s, m = e["case"], e["metrics"]
        cross = m["formation_target_first_crossing"]
        onset = (
            "not reached"
            if cross is None
            else f"({cross['after_s'] / 60:g}, {cross['at_or_before_s'] / 60:g}] min"
        )
        parts.append(
            f"<tr><td>{s['rain_rate_mm_per_h']} mm/h</td><td>{s['storm_end_s'] / 60:g} min</td><td>{onset}</td><td>{m['peak_material_wet_area_km2']:.2f} km²</td><td>{m['observations'][-1]['stored_m3'] / 1e6:.2f} million m³</td><td>{e['native_call_wall_s']:.1f} s</td></tr>"
        )
    parts += [
        "</table><p>All saved states passed finite/nonnegative-depth and dry-momentum checks. Stored water remains below scheduled input plus a serialization allowance. New native input changes are restricted to rainfall and runtime/save cadence, and inputs remain unchanged during execution. This does not establish full water-budget closure, convergence, unlimited rate support or permanent calm lakes.</p>"
    ]
    for key, title, description in (
        (
            "early",
            "Same physical time: 45 minutes after a dry start",
            "All three storms are still supplying water here. From left to right: 480, 512, 1024 mm/h. Look at the width of the turquoise branched collection and the surrounding runoff channels, not just water brightness.",
        ),
        (
            "storm",
            "Same main-storm input: 960 mm delivered",
            "Different elapsed times, but the same rain amount. Higher-rate storms allow less time for drainage during supply.",
        ),
        (
            "final",
            "After the same four-hour recession window",
            "Compare the remaining broad collection after surrounding slopes and channels have drained. The main storm plus taper supplies 961 mm in every case.",
        ),
    ):
        parts.append(f"<section><h2>{title}</h2><p>{description}</p><div class='grid'>")
        for e in entries:
            s = e["case"]
            t = {
                "early": COMPARE_TIME,
                "storm": s["storm_end_s"],
                "final": s["duration_s"],
            }[key]
            path = f"media/rain{s['rain_rate_mm_per_h']}-collection-{t:05d}s.png"
            parts.append(
                f"<figure><a href='{path}'><img src='{path}' alt='{s['rain_rate_mm_per_h']} mm/h, {t / 60:g} physical minutes'></a><figcaption>{s['rain_rate_mm_per_h']} mm/h · {t / 60:g} min elapsed</figcaption></figure>"
            )
        parts.append("</div></section>")
    parts += [
        "<section><h2>Whole-map depth and histories</h2><p>Blue is saved water depth, on a shared log scale. Films below 0.1 m are hidden; broader coverage is flowing/flooded water, not necessarily more lakes. B20 is the upper-right branched collection used in the close views. In the graph, the top panel shows filling, the middle shows runoff coverage, and the bottom shows storage. The 0.886 km² plateau means all cells of the frozen B20 observation mask are at least 1 m deep, not a prescribed lake-size limit.</p><a href='media/water-comparison.png'><img src='media/water-comparison.png' alt='Matched time, input and recession comparisons'></a><img src='media/formation-history.png' alt='Collection buildup, wet area and storage histories'></section><section><h2>Formation and recession replays</h2><p>Same playback time scale: 225 physical seconds per frame at 6 fps (22.5 physical minutes per video second). Native fields are held between saves, not physically interpolated. The reference remains sampled at five-minute native knots; the new cases use 3.75-minute knots. Clips end at each experiment’s own recession end.</p><div class='grid'>"
    ]
    for e in entries:
        s = e["case"]
        rate, t = s["rain_rate_mm_per_h"], s["duration_s"]
        parts.append(
            f"<figure><video controls preload='metadata' poster='media/rain{rate}-collection-{t:05d}s.png' src='media/rain{rate}-collection-formation.mp4'></video><figcaption>{rate} mm/h · {t / 3600:g} physical hours</figcaption></figure>"
        )
    parts += [
        "</div></section><h2>Scope and evidence</h2><p>No solver, terrain, material, shoreline, UI preset or product-default changes. No infiltration, evaporation, erosion or outside-crop catchment. Rainfall is water input, not a simulation-clock multiplier. Native-call times include native I/O but exclude Python setup, analysis, conversion and rendering; these single shared-GPU runs with different save cadences are not strict performance benchmarks. Human visual acceptance remains pending.</p><p><a href='protocol.json'>Frozen protocol</a> · <a href='comparison.json'>All observations</a> · <a href='capture-manifest.json'>Capture provenance</a> · <a href='../rain-acceleration-v1-20261008-a1/index.html'>Unchanged 120/240/480 comparison</a></p></html>"
    ]
    presentation.write_text_exclusive(out / "index.html", "\n".join(parts))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--child", type=Path, help=argparse.SUPPRESS)
    parser.add_argument(
        "--presentation-only",
        action="store_true",
        help="Regenerate media from a completed sealed comparison; never run a solver",
    )
    args = parser.parse_args()
    if args.child:
        child(args.child)
        return
    if not args.out:
        parser.error("--out is required")
    out = args.out.resolve()
    if args.presentation_only:
        if out.parent != (ROOT / "outputs/fluid").resolve() or not out.name.startswith(
            PREFIX
        ):
            raise ValueError("presentation requires an existing power2 output leaf")
        require_fresh_presentation_outputs(out)
        frozen = json.loads((out / "protocol.json").read_text())
        if (
            frozen["schema"] != SCHEMA
            or presentation.sha256_file(out / "runner-frozen.py")
            != frozen["runner_sha256"]
            or reference_identity() != frozen["reference_identity"]
            or helper_identity() != frozen["helper_identity"]
        ):
            raise ValueError(
                "frozen numerical source, reference or dependencies changed"
            )
        comparison = json.loads((out / "comparison.json").read_text())
        entries = comparison["entries"]
        if comparison["schema"] != SCHEMA or [e["case"] for e in entries] != [
            case_spec(r) for r in RATES
        ]:
            raise ValueError("comparison does not match the frozen cases")
        for entry in entries:
            bed, states = previous.recording_states(Path(entry["recording"]))
            labels = np.load(
                recession.reference(recession.load()).DEPRESSION_LABELS,
                allow_pickle=False,
            )
            # Recheck all frame hashes/health before presenting existing results.
            metrics = summarize(states, entry["case"], bed, labels)
            if (
                metrics["formation_target_first_crossing"]
                != entry["metrics"]["formation_target_first_crossing"]
            ):
                raise ValueError(
                    "recording onset no longer agrees with numerical results"
                )
        capture(out, entries)
        maps(out, entries)
        report(out, entries)
        print("report " + str(out / "index.html"), flush=True)
        return
    cases = prepare(out)
    ref = recession.reference(recession.load())
    labels = np.load(ref.DEPRESSION_LABELS, allow_pickle=False)
    bed, frames = previous.recording_states(REFERENCE_CASE / "recording")
    reference_result = json.loads((REFERENCE_CASE / "case-result.json").read_text())
    entries = [
        {
            "case": cases[0],
            "metrics": summarize(frames, cases[0], bed, labels),
            "native_call_wall_s": reference_result["native_call_wall_s"],
            "recording": str(REFERENCE_CASE / "recording"),
            "reused_reference": True,
        }
    ]
    for s in cases[1:]:
        entries.append(run_case(out / "cases" / s["name"], s, ref, labels))
    frozen = json.loads((out / "protocol.json").read_text())
    if (
        reference_identity() != frozen["reference_identity"]
        or helper_identity() != frozen["helper_identity"]
        or presentation.sha256_file(Path(__file__)) != frozen["runner_sha256"]
    ):
        raise ValueError("reference or frozen helpers changed during the study")
    native.write(
        out / "comparison.json",
        {
            "schema": SCHEMA,
            "entries": entries,
            "reference_unchanged": True,
            "human_visual_acceptance": "pending",
        },
    )
    capture(out, entries)
    maps(out, entries)
    report(out, entries)
    print("report " + str(out / "index.html"), flush=True)


if __name__ == "__main__":
    main()
