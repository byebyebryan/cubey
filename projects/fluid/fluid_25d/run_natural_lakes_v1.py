#!/usr/bin/env python3
"""Dry-start rainfall filling and recession on the unchanged mountain DEM.

Bounded visual-demo experiment, not calibrated hydrology or terrain erosion.
Reuses the native serializer and recording/viewer; never seeds a selected basin.
"""

from __future__ import annotations

import argparse
import html
import json
import os
import shutil
import subprocess
from pathlib import Path

import convert_synxflow_recording_v1 as converter
import numpy as np
import run_native_presentation_v1 as presentation
import run_native_rain_recession_v1 as recession
import run_seeded_lake_v1 as native
from scipy import ndimage

ROOT = presentation.ROOT
SCHEMA = "cubey.fluid25d.natural_lakes.v1"
STORM_END = 28800
TAPER_END = 28860
END = 43200
CADENCE = 300
RATE = 120.0 / 3_600_000
TIMES = (14400, STORM_END, 36000, END)


def spec():
    return {
        "name": "dry-start-rain-and-recession",
        "family": "natural-mountain-collections",
        "rows": 512,
        "cols": 512,
        "dx_m": 30.0,
        "duration_s": END,
        "output_interval_s": CADENCE,
        "rain_rate_m_per_s": RATE,
        "rainfall_history": [[0, RATE], [STORM_END, RATE], [TAPER_END, 0], [END, 0]],
        "boundary": "fall",
        "manning_n": 0.05,
        "crop_xzwh": [1152, 1408, 512, 512],
        "initial_condition": {
            "kind": "completely dry; all water comes from uniform rain",
            "initial_volume_m3": 0.0,
            "initial_momentum": "zero",
            "seeded_basins": [],
            "terrain_modified": False,
        },
        "purpose": "observe collections everywhere after an eight-hour storm and four-hour recession",
    }


def checked_output(path):
    resolved = path.resolve()
    if (
        resolved.parent != (ROOT / "outputs/fluid").resolve()
        or not resolved.name.startswith("natural-lakes-v1-")
        or path.is_symlink()
        or path.exists()
    ):
        raise ValueError(
            "--out must be a fresh natural-lakes-v1-* leaf directly under outputs/fluid"
        )
    return resolved


def case_path(out):
    return out / "cases" / spec()["name"]


def prepare(out):
    previous = recession.load()
    ref = recession.reference(previous)
    _, mask_identity = ref.fixed_depression_masks()
    source = Path(previous["baseline_case"])
    out.mkdir(exist_ok=False)
    case = case_path(out)
    case.mkdir(parents=True)
    shutil.copyfile(source / "DEM.asc", case / "DEM.asc")
    if presentation.sha256_file(case / "DEM.asc") != previous["baseline_dem_sha256"]:
        raise ValueError("source DEM differs from the established rain experiment")
    np.save(case / "initial-depth.npy", np.zeros((512, 512)), allow_pickle=False)
    s = spec()
    native.write(case / "case-spec.json", s)
    protocol = {
        "schema": SCHEMA,
        "case": s,
        "terrain_source_identity": previous["terrain_source_identity"],
        "native_backend_identity": previous["native_backend_identity"],
        "input_dem_ascii_sha256": previous["baseline_dem_sha256"],
        "settings": {"rain_source_schedule": s["rainfall_history"]},
        "expected_saved_times_s": list(range(0, END + 1, CADENCE)),
        "initial_depth_sha256": presentation.sha256_file(case / "initial-depth.npy"),
        "native_bed_sha256": presentation.sha256_file(
            source / "native/input/field/z.dat"
        ),
        "runner_sha256": presentation.sha256_file(Path(__file__)),
        "native_helper_sha256": presentation.sha256_file(Path(native.__file__)),
        "analytical_depression_identity": mask_identity,
        "observation_policy": "all frozen analytical depressions and all material-depth connected water patches, not just B20",
        "reporting_depth_threshold_m": 0.1,
        "retained_patch_depth_threshold_m": 0.25,
        "native_timeout_s": 300,
        "study_disk_budget_bytes": 6 * 1024**3,
        "frozen_before_native_execution": True,
        "human_visual_acceptance": "pending",
        "limits": "Eight-hour 120 mm/h storm is a deliberate stress/demo, not weather realism. No infiltration, evaporation, erosion or outside-crop catchment. Retained collections are not necessarily calm permanent lakes.",
    }
    native.write(case / "case-protocol.json", protocol)
    native.write(out / "protocol.json", protocol)
    return case


def validate_fields(h, qx, qy):
    if (
        not all(np.isfinite(f).all() for f in (h, qx, qy))
        or (h < 0).any()
        or ((h == 0) & ((qx != 0) | (qy != 0))).any()
    ):
        raise ValueError("nonfinite/negative water or dry nonzero momentum")


def field_metrics(bed, h, qx, qy, labels):
    """Describe observed water without automatically declaring every patch a lake."""
    validate_fields(h, qx, qy)
    speed = np.zeros_like(h)
    np.divide(np.hypot(qx, qy), h, out=speed, where=h > 0)
    basins = []
    for label in np.unique(labels):
        if label <= 0:
            continue
        mask = labels == label
        volume = float(h[mask].sum()) * 900
        if volume < 1:
            continue
        wet = mask & (h >= 0.1)
        surface = (bed + h)[wet]
        basins.append(
            {
                "label": int(label),
                "stored_m3": volume,
                "material_wet_area_km2": int(wet.sum()) * 0.0009,
                "max_depth_m": float(h[mask].max()),
                "water_weighted_speed_m_per_s": float(
                    np.hypot(qx[mask], qy[mask]).sum()
                )
                / float(h[mask].sum()),
                "surface_p90_minus_p10_m": float(
                    np.percentile(surface, 90) - np.percentile(surface, 10)
                )
                if surface.size
                else None,
            }
        )
    basins.sort(key=lambda b: b["stored_m3"], reverse=True)
    connected, count = ndimage.label(h >= 0.25, native.D4)
    patches = []
    for number, box in enumerate(ndimage.find_objects(connected, count), 1):
        if box is None:
            continue
        patch = connected[box] == number
        cells = int(patch.sum())
        if cells < 4:
            continue
        depths = h[box][patch]
        surface = (bed[box] + h[box])[patch]
        patches.append(
            {
                "area_km2": cells * 0.0009,
                "stored_m3": float(depths.sum()) * 900,
                "water_weighted_speed_m_per_s": float(
                    np.hypot(qx[box][patch], qy[box][patch]).sum()
                )
                / float(depths.sum()),
                "surface_p90_minus_p10_m": float(
                    np.percentile(surface, 90) - np.percentile(surface, 10)
                ),
                "bbox_xz": [
                    box[1].start,
                    box[0].start,
                    box[1].stop - 1,
                    box[0].stop - 1,
                ],
                "touches_domain_edge": box[0].start == 0
                or box[1].start == 0
                or box[0].stop == h.shape[0]
                or box[1].stop == h.shape[1],
            }
        )
    patches.sort(key=lambda b: b["stored_m3"], reverse=True)
    return {
        "stored_m3": float(h.sum()) * 900,
        "max_depth_m": float(h.max()),
        "material_wet_area_km2": int((h >= 0.1).sum()) * 0.0009,
        "water_weighted_speed_m_per_s": float(np.hypot(qx, qy).sum()) / float(h.sum())
        if h.sum()
        else 0,
        "slow_water_volume_fraction": float(h[speed <= 0.05].sum()) / float(h.sum())
        if h.sum()
        else 0,
        "basins_with_at_least_10000_m3": sum(b["stored_m3"] >= 10000 for b in basins),
        "analytical_depression_observations": basins,
        "observed_depth_connected_patches": patches,
    }


def analyze(case):
    ref = recession.reference(recession.load())
    _, bed = ref._grid_from_native_ids(case / "native/input/field/z.dat", 512, 512)
    labels = np.load(ref.DEPRESSION_LABELS, allow_pickle=False)
    paths = ref.snapshot_paths(case / "native/output")
    expected = list(range(0, END + 1, CADENCE))
    if any(sorted(paths[f]) != expected for f in ("h", "hUx", "hUy")):
        raise ValueError("missing or unexpected saved states")
    rows = []
    for t in expected:
        h, qx, qy = [ref.read_ascii(paths[f][t])[1] for f in ("h", "hUx", "hUy")]
        validate_fields(h, qx, qy)
        if t == 0 and any(np.any(f != 0) for f in (h, qx, qy)):
            raise ValueError("time zero is not completely dry")
        _, scheduled_depth, phase = recession.history_value(
            spec()["rainfall_history"], t
        )
        stored = float(h.sum()) * 900
        if stored > scheduled_depth * h.size * 900 * 1.001 + h.size * 900 * 0.5e-6:
            raise ValueError(
                "storage exceeds nominal rainfall plus precision allowance"
            )
        row = {
            "time_s": t,
            "rain_phase": phase,
            "stored_m3": stored,
            "max_depth_m": float(h.max()),
            "material_wet_area_km2": int((h >= 0.1).sum()) * 0.0009,
        }
        if t in TIMES:
            row.update(field_metrics(bed.astype(np.float64), h, qx, qy, labels))
        rows.append(row)
    return {
        "observations": rows,
        "all_saved_fields_healthy": True,
        "dry_start_verified_at_time_zero": True,
        "initial_water_volume_m3": 0,
        "diagnostic_snapshots": [r for r in rows if r["time_s"] in TIMES],
    }


def run_case(case):
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
        str(recession.PYTHON),
        "-B",
        "-u",
        str(Path(__file__).resolve()),
        "--child",
        str(case),
    ]
    print("running dry-start 8-hour storm + 4-hour recession", flush=True)
    with (
        (case / "native.stdout").open("x") as stdout,
        (case / "native.stderr").open("x") as stderr,
    ):
        result = subprocess.run(
            command,
            cwd=ROOT,
            env=environment,
            stdout=stdout,
            stderr=stderr,
            timeout=300,
            check=False,
        )
    if (
        result.returncode
        or "Simulation successfully finished!"
        not in (case / "native.stdout").read_text()
    ):
        raise RuntimeError("native run failed; inspect " + str(case / "native.stderr"))
    metrics = analyze(case)
    audit = json.loads((case / "native-preflight.json").read_text())
    post = json.loads((case / "native-postrun.json").read_text())
    native.write(
        case / "case-result.json",
        {
            "schema": SCHEMA,
            "case": spec(),
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
                "native_call_s": post["native_call_wall_s"],
                "snapshots": [
                    {
                        k: r[k]
                        for k in (
                            "time_s",
                            "stored_m3",
                            "material_wet_area_km2",
                            "basins_with_at_least_10000_m3",
                            "slow_water_volume_fraction",
                        )
                    }
                    for r in metrics["diagnostic_snapshots"]
                ],
            }
        ),
        flush=True,
    )
    converter.convert_case(case, case.parents[1] / "recording")
    print("converted recording", flush=True)


def capture(out):
    media = out / "media"
    media.mkdir(exist_ok=False)
    runtime = presentation.runtime_identity()
    assets = []
    for camera in ("overview", "collection"):
        for t in (0, 14400, STORM_END, END):
            name = f"{camera}-{t:05d}s"
            asset = presentation.still_asset("natural", camera, t)
            asset.update(width=1280, height=720)
            command = presentation.app_command(
                asset, media / f"{name}.png", "scenic", False
            )
            command[command.index("--fluid25d-recording") + 1] = str(
                out / "recording/recording.json"
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
                    "path": f"media/{name}.png",
                    "sha256": presentation.sha256_file(media / f"{name}.png"),
                    "receipt": receipt,
                }
            )
            print("captured " + name, flush=True)
    for camera in ("overview", "collection"):
        name = f"{camera}-formation"
        asset = presentation.video_asset(
            name, "natural", camera, 0, END // CADENCE + 1, 6, CADENCE
        )
        asset.update(
            width=1280,
            height=720,
            saved_field_interval_s=CADENCE,
            timeline=presentation.timeline(0, END // CADENCE + 1, CADENCE, CADENCE),
        )
        command = presentation.app_command(
            asset, media / f"{name}.mp4", "scenic", False
        )
        command[command.index("--fluid25d-recording") + 1] = str(
            out / "recording/recording.json"
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
                "path": f"media/{name}.mp4",
                "sha256": presentation.sha256_file(media / f"{name}.mp4"),
                "receipt": receipt,
            }
        )
        print("captured " + name, flush=True)
    presentation.assert_same_runtime(
        runtime, presentation.runtime_identity(), "natural collection captures"
    )
    native.write(
        out / "capture-manifest.json",
        {
            "runtime_identity": runtime,
            "assets": assets,
            "capture_runner_sha256": presentation.sha256_file(Path(__file__)),
            "human_visual_acceptance": "pending",
        },
    )


def maps(out):
    """Whole-map evidence, not an artistic recoloring of the Scenic render."""
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import LightSource, LogNorm

    ref = recession.reference(recession.load())
    case = case_path(out)
    _, bed = ref._grid_from_native_ids(case / "native/input/field/z.dat", 512, 512)
    labels = np.load(ref.DEPRESSION_LABELS, allow_pickle=False)
    result = json.loads((case / "case-result.json").read_text())["metrics"]
    snapshots = result["diagnostic_snapshots"]
    paths = ref.snapshot_paths(case / "native/output")
    figure, axes = plt.subplots(1, 3, figsize=(16, 6), layout="constrained")
    extent = (0, 15.36, 15.36, 0)
    shade = LightSource(315, 45).hillshade(bed, vert_exag=1, dx=30, dy=30)
    artist = None
    for axis, t in zip(axes, (14400, STORM_END, END)):
        depth = ref.read_ascii(paths["h"][t])[1]
        axis.imshow(shade, cmap="gray", vmin=0, vmax=1, extent=extent)
        artist = axis.imshow(
            np.ma.masked_less(depth, 0.1),
            cmap="Blues",
            norm=LogNorm(0.1, 70),
            extent=extent,
        )
        row = next(r for r in snapshots if r["time_s"] == t)
        for basin in row["analytical_depression_observations"][:3]:
            z, x = np.argwhere(labels == basin["label"]).mean(axis=0)
            axis.annotate(
                f"B{basin['label']}",
                ((x + 0.5) * 0.03, (z + 0.5) * 0.03),
                color="#ffc933",
                weight="bold",
                xytext=(5, 8),
                textcoords="offset points",
                bbox={"facecolor": "#18212a", "alpha": 0.8, "pad": 2},
            )
        axis.set_title(
            f"{t // 3600} h: {'rain' if t <= STORM_END else '4 h after rain'}\n{row['stored_m3'] / 1e6:.1f} million m³ stored"
        )
        axis.set_xlabel("Crop X (km)")
        axis.set_ylabel("Crop Z (km)")
    figure.colorbar(
        artist,
        ax=axes,
        label="Actual water depth (m), log scale; below 0.1 m hidden",
        shrink=0.65,
    )
    figure.suptitle(
        "Dry-start uniform rainfall: water chooses the collections, not a lake seed"
    )
    figure.savefig(out / "media/water-distribution.png", dpi=130)
    plt.close(figure)
    figure, axes = plt.subplots(
        2, 1, figsize=(10, 6), sharex=True, layout="constrained"
    )
    rows = result["observations"]
    hours = [r["time_s"] / 3600 for r in rows]
    axes[0].plot(hours, [r["stored_m3"] / 1e6 for r in rows])
    axes[0].set_ylabel("Stored water (million m³)")
    axes[1].plot(hours, [r["material_wet_area_km2"] for r in rows])
    axes[1].set_ylabel("Wet area ≥0.1 m (km²)")
    axes[1].set_xlabel("Physical simulation hours")
    for axis in axes:
        axis.axvline(
            8, color="orange", linestyle="--", label="Rain stops (60-second taper)"
        )
        axis.grid(alpha=0.3)
    axes[0].legend()
    figure.savefig(out / "media/storage-history.png", dpi=130)
    plt.close(figure)


def report(out):
    result = json.loads((case_path(out) / "case-result.json").read_text())
    snapshots = result["metrics"]["diagnostic_snapshots"]
    final = snapshots[-1]
    storm = next(r for r in snapshots if r["time_s"] == STORM_END)
    parts = [
        "<!doctype html><html lang='en'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>",
        "<title>Rain-formed collections across the mountain map</title><style>body{font:16px system-ui;background:#18212a;color:#e2e8ed;max-width:1300px;margin:auto;padding:24px}a{color:#8acbff}img,video{max-width:100%;height:auto}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px}table{border-collapse:collapse}th,td{border:1px solid #52606d;padding:8px;text-align:right}pre{white-space:pre-wrap;overflow-wrap:anywhere}section{margin:32px 0}</style>",
        "<h1>Where does rain leave water?</h1><p>Completely dry start. Uniform rainfall across the same unmodified 15.36 × 15.36 km Terrain Diffusion crop. No lake seed, painted source, artificial dam or maintained water level. Eight physical hours at the established heavy 120 mm/h rate, a 60-second taper, then four hours of recession. Same released SynxFlow solver and Cubey material/shoreline defaults.</p>",
        f"<p><b>Measured:</b> storage {storm['stored_m3'] / 1e6:.2f} → {final['stored_m3'] / 1e6:.2f} million m³ after rain stops; wet area at ≥0.1 m {storm['material_wet_area_km2']:.2f} → {final['material_wet_area_km2']:.2f} km². At the end, {final['basins_with_at_least_10000_m3']} analytical depressions each contain at least 10,000 m³. That is an explicit volume threshold, not a count of proven calm lakes.</p>",
        "<section><h2>Start with the whole map</h2><p>Blue is actual saved water depth, with shallow films below 0.1 m hidden for legibility. Yellow labels mark the three largest observed analytical-basin stores in each snapshot. Other water has not been removed from the simulation. Most slopes are drainage paths, not bowls that can hold lakes.</p><a href='media/water-distribution.png'><img src='media/water-distribution.png' alt='Water distribution at four, eight and twelve simulation hours'></a><img src='media/storage-history.png' alt='Stored volume and wet area during rain and recession'></section>",
        "<section><h2>Scenic overview, same camera</h2><div class='grid'>",
    ]
    for t, caption in (
        (0, "Dry start"),
        (14400, "Four hours of rain"),
        (STORM_END, "Eight hours of rain"),
        (END, "Four hours after rain stops"),
    ):
        parts.append(
            f"<figure><a href='media/overview-{t:05d}s.png'><img src='media/overview-{t:05d}s.png' alt='{caption}'></a><figcaption>{caption}</figcaption></figure>"
        )
    parts += [
        "</div><video controls preload='metadata' poster='media/overview-43200s.png' src='media/overview-formation.mp4'></video></section>",
        "<section><h2>Largest basin, now filled only by rain</h2><p>This close view is the same B20 region used for the seeded lake, but no initial lake was placed here. Streams deliver water from the surrounding terrain.</p><div class='grid'>",
    ]
    for t, caption in (
        (0, "Dry start"),
        (14400, "Four hours"),
        (STORM_END, "Eight hours"),
        (END, "After recession"),
    ):
        parts.append(
            f"<figure><a href='media/collection-{t:05d}s.png'><img src='media/collection-{t:05d}s.png' alt='{caption}'></a><figcaption>{caption}</figcaption></figure>"
        )
    parts += [
        "</div><video controls preload='metadata' poster='media/collection-43200s.png' src='media/collection-formation.mp4'></video></section>",
        "<section><h2>Largest retained analytical-basin stores</h2><p>Masks are terrain-derived and frozen before the run. They are observation regions, not sources or lake placement. Adjacent flowing water can contribute to these stores; the speed and level-spread columns help distinguish moving collection from calm lake-like water.</p><table><tr><th>Basin</th><th>Stored million m³</th><th>Wet area ≥0.1 m, km²</th><th>Weighted speed, m/s</th><th>Surface P90−P10, m</th></tr>",
    ]
    for b in final["analytical_depression_observations"][:10]:
        spread = (
            "—"
            if b["surface_p90_minus_p10_m"] is None
            else f"{b['surface_p90_minus_p10_m']:.2f}"
        )
        parts.append(
            f"<tr><td>B{b['label']}</td><td>{b['stored_m3'] / 1e6:.3f}</td><td>{b['material_wet_area_km2']:.3f}</td><td>{b['water_weighted_speed_m_per_s']:.3f}</td><td>{spread}</td></tr>"
        )
    parts += [
        "</table></section><h2>Reading and limits</h2><p>The videos replay twelve physical hours in about 24 seconds. Native states are saved every five minutes and held, not continuously simulated or physically interpolated in the browser. This is water collecting in existing terrain, not erosion or new river/lake-bed formation. The exceptional 960 mm storm total is a visual stress case, not realistic climate. Boundary outflow, absent outside-crop catchments, and no infiltration/evaporation constrain interpretation. Retained water is not automatically a permanent calm lake. Human visual acceptance remains pending.</p>",
        "<p><a href='protocol.json'>Frozen protocol</a> · <a href='cases/dry-start-rain-and-recession/case-result.json'>All numerical observations</a> · <a href='capture-manifest.json'>Capture provenance</a> · <a href='../seeded-lake-v1-20261008-a1/index.html'>Seeded reference</a></p>",
        "<p>Local GUI replay:</p><pre>rtk proxy build/dev/projects/fluid/fluid_25d/fluid_25d --fluid25d-recording "
        + html.escape(str((out / "recording/recording.json").relative_to(ROOT)))
        + " --fluid25d-native-presentation scenic --fluid25d-scenic-material macro --fluid25d-recording-camera overview --fluid25d-recording-speed 60</pre></html>",
    ]
    presentation.write_text_exclusive(out / "index.html", "\n".join(parts))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--child", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--capture-only", action="store_true")
    args = parser.parse_args()
    if args.child:
        # The shared native helper retains its strict fresh case layout.
        native.child(
            args.child, study_prefix="natural-lakes-v1-", launch_source=Path(__file__)
        )
        return
    if not args.out:
        parser.error("--out is required")
    if args.capture_only:
        out = args.out.resolve()
        if out.parent != (ROOT / "outputs/fluid").resolve() or not out.name.startswith(
            "natural-lakes-v1-"
        ):
            parser.error("invalid natural-lakes output leaf")
    else:
        out = checked_output(args.out)
        if shutil.disk_usage(out.parent).free < 8 * 1024**3:
            raise ValueError("less than 8 GiB free for the bounded experiment")
        case = prepare(out)
        run_case(case)
    capture(out)
    maps(out)
    report(out)
    print("report " + str(out / "index.html"), flush=True)


if __name__ == "__main__":
    main()
