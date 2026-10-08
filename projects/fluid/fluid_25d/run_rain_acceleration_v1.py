#!/usr/bin/env python3
"""Compare faster, equal-total dry-start storms on the unchanged mountain DEM.

Two native SynxFlow runs, plus the existing 120 mm/h recording. No solver,
terrain, renderer or product-default changes. Not calibrated hydrology.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

import convert_synxflow_recording_v1 as converter
import numpy as np
import run_native_presentation_v1 as presentation
import run_native_rain_recession_v1 as recession
import run_natural_lakes_v1 as natural
import run_seeded_lake_v1 as native

ROOT = presentation.ROOT
SCHEMA = "cubey.fluid25d.rain_acceleration.v1"
PREFIX = "rain-acceleration-v1-"
BASELINE = ROOT / "outputs/fluid/natural-lakes-v1-20261008-a1"
CADENCE = 300
RATES = (120, 240, 480)
AREA_TARGET_KM2 = 0.5
BASIN = 20


def case_spec(rate):
    if rate not in RATES:
        raise ValueError("only the frozen 120, 240 and 480 mm/h cases are supported")
    multiplier = rate // 120
    storm_end = 28800 // multiplier
    taper_end = storm_end + 60 // multiplier
    end = storm_end + 14400
    flux = rate / 3_600_000
    s = natural.spec()
    s.update(
        name=f"rain{rate}",
        family="equal-total-rain-acceleration",
        duration_s=end,
        rain_rate_m_per_s=flux,
        rainfall_history=[[0, flux], [storm_end, flux], [taper_end, 0], [end, 0]],
        storm_end_s=storm_end,
        taper_end_s=taper_end,
        rain_rate_mm_per_h=rate,
        purpose="equal 961 mm scheduled total; four-hour recession window including taper",
    )
    return s


def first_crossing(rows, key, target):
    """First observed crossing, bracketed by saved states, not exact onset."""
    previous = None
    for row in rows:
        if row[key] >= target:
            return {"after_s": previous, "at_or_before_s": row["time_s"]}
        previous = row["time_s"]
    return None


def assert_equal_total(cases):
    totals = [
        recession.history_value(s["rainfall_history"], s["duration_s"])[1]
        for s in cases
    ]
    if not all(abs(total - 0.961) < 1e-12 for total in totals):
        raise ValueError("storms must have exactly 961 mm of nominal scheduled rain")


def baseline_identity():
    return {
        str((BASELINE / rel).relative_to(ROOT)): presentation.sha256_file(
            BASELINE / rel
        )
        for rel in (
            "protocol.json",
            "cases/dry-start-rain-and-recession/case-result.json",
            "recording/recording.json",
        )
    }


def prepare(out):
    if (
        out.parent != (ROOT / "outputs/fluid").resolve()
        or not out.name.startswith(PREFIX)
        or out.exists()
        or out.is_symlink()
    ):
        raise ValueError("output must be a fresh rain-acceleration-v1-* leaf")
    if shutil.disk_usage(out.parent).free < 8 * 1024**3:
        raise ValueError("less than 8 GiB available for this bounded sweep")
    previous = recession.load()
    ref = recession.reference(previous)
    _, mask_identity = ref.fixed_depression_masks()
    baseline_protocol = json.loads((BASELINE / "protocol.json").read_text())
    if baseline_protocol["case"] != natural.spec():
        raise ValueError(
            "the existing dry-start baseline is not the expected experiment"
        )
    if baseline_protocol["analytical_depression_identity"] != mask_identity:
        raise ValueError("analytical masks differ from the existing dry-start baseline")
    cases = [case_spec(r) for r in RATES]
    assert_equal_total(cases)
    out.mkdir(exist_ok=False)
    source = Path(previous["baseline_case"])
    frozen = {
        "schema": SCHEMA,
        "cases": cases,
        "baseline_identity": baseline_identity(),
        "terrain_source_identity": previous["terrain_source_identity"],
        "native_backend_identity": previous["native_backend_identity"],
        "analytical_depression_identity": mask_identity,
        "runner_sha256": presentation.sha256_file(Path(__file__)),
        "analysis_helper_sha256": presentation.sha256_file(Path(natural.__file__)),
        "nominal_total_rain_mm": 961,
        "formation_target": {
            "basin_label": BASIN,
            "min_depth_m": 1,
            "area_km2": AREA_TARGET_KM2,
        },
        "comparison_policy": "same physical time at 2 h; same total rain at storm end; same 4 h recession window",
        "saved_state_cadence_s": CADENCE,
        "native_timeout_s_per_case": 300,
        "frozen_before_native_execution": True,
        "human_visual_acceptance": "pending",
        "limits": "Extreme visual-demo stress rainfall, not realistic climate. No infiltration, evaporation, erosion or outside-crop catchment. Higher rain is not faster simulation time. Retained water is not automatically a calm permanent lake.",
    }
    native.write(out / "protocol.json", frozen)
    for s in cases[1:]:
        case = out / "cases" / s["name"]
        case.mkdir(parents=True, exist_ok=False)
        shutil.copyfile(source / "DEM.asc", case / "DEM.asc")
        if (
            presentation.sha256_file(case / "DEM.asc")
            != previous["baseline_dem_sha256"]
        ):
            raise ValueError("DEM differs from the frozen reference")
        np.save(case / "initial-depth.npy", np.zeros((512, 512)), allow_pickle=False)
        native.write(case / "case-spec.json", s)
        native.write(
            case / "case-protocol.json",
            {
                "schema": SCHEMA,
                "case": s,
                "terrain_source_identity": frozen["terrain_source_identity"],
                "native_backend_identity": frozen["native_backend_identity"],
                "input_dem_ascii_sha256": previous["baseline_dem_sha256"],
                "settings": {"rain_source_schedule": s["rainfall_history"]},
                "expected_saved_times_s": list(range(0, s["duration_s"] + 1, CADENCE)),
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
    mask = labels == BASIN
    if not mask.any():
        raise ValueError("fixed B20 observation region is missing")
    rows = []
    snapshot_times = {7200, s["storm_end_s"], s["duration_s"]}
    for t, h, qx, qy in states:
        natural.validate_fields(h, qx, qy)
        if t == 0 and any(np.any(f != 0) for f in (h, qx, qy)):
            raise ValueError("time zero is not completely dry")
        _, scheduled_depth, phase = recession.history_value(s["rainfall_history"], t)
        stored = float(h.sum()) * 900
        if stored > scheduled_depth * h.size * 900 * 1.001 + h.size * 900 * 0.5e-6:
            raise ValueError(
                "storage exceeds scheduled rain plus serialization allowance"
            )
        row = {
            "time_s": t,
            "rain_phase": phase,
            "stored_m3": stored,
            "max_depth_m": float(h.max()),
            "material_wet_area_km2": int((h >= 0.1).sum()) * 0.0009,
            "b20_area_at_least_1m_km2": int(((h >= 1) & mask).sum()) * 0.0009,
            "b20_stored_m3": float(h[mask].sum()) * 900,
        }
        if t in snapshot_times:
            row.update(natural.field_metrics(bed, h, qx, qy, labels))
        rows.append(row)
    expected = list(range(0, s["duration_s"] + 1, CADENCE))
    if [r["time_s"] for r in rows] != expected:
        raise ValueError("missing or unexpected saved states")
    return {
        "all_saved_fields_healthy": True,
        "dry_start_verified_at_time_zero": True,
        "formation_target_first_crossing": first_crossing(
            rows, "b20_area_at_least_1m_km2", AREA_TARGET_KM2
        ),
        "peak_material_wet_area_km2": max(r["material_wet_area_km2"] for r in rows),
        "observations": rows,
        "diagnostic_snapshots": [r for r in rows if r["time_s"] in snapshot_times],
    }


def run_case(case, s, ref, labels):
    print(
        f"running {s['rain_rate_mm_per_h']} mm/h for {s['storm_end_s'] // 3600} h + 4 h recession",
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
        raise RuntimeError("native run failed; inspect " + str(case / "native.stderr"))
    _, bed = ref._grid_from_native_ids(case / "native/input/field/z.dat", 512, 512)
    paths = ref.snapshot_paths(case / "native/output")
    expected = list(range(0, s["duration_s"] + 1, CADENCE))
    if any(sorted(paths[f]) != expected for f in ("h", "hUx", "hUy")):
        raise ValueError("native run omitted expected saved states")
    states = (
        (t, *(ref.read_ascii(paths[f][t])[1] for f in ("h", "hUx", "hUy")))
        for t in expected
    )
    metrics = summarize(states, s, bed.astype(np.float64), labels)
    audit = json.loads((case / "native-preflight.json").read_text())
    if "field/h.dat" in audit["changed_paths_vs_dry_start_rain_reference"]:
        raise ValueError("dry-start initial water differs from the reference")
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
    print(
        json.dumps(
            {
                "rate": s["rain_rate_mm_per_h"],
                "native_call_s": post["native_call_wall_s"],
                "formation": metrics["formation_target_first_crossing"],
                "peak_wet_area_km2": metrics["peak_material_wet_area_km2"],
            }
        ),
        flush=True,
    )
    return result, recording


def recording_states(root):
    manifest = json.loads((root / "recording.json").read_text())
    bed_raw = (root / manifest["bed"]["path"]).read_bytes()
    if hashlib.sha256(bed_raw).hexdigest() != manifest["bed"]["sha256"]:
        raise ValueError("recording bed hash mismatch")
    bed = np.frombuffer(bed_raw, dtype="<f4").reshape(512, 512).astype(np.float64)

    def frames():
        for frame in manifest["frames"]:
            raw = (root / frame["path"]).read_bytes()
            if hashlib.sha256(raw).hexdigest() != frame["sha256"]:
                raise ValueError("recording frame hash mismatch")
            fields = (
                np.frombuffer(raw, dtype="<f4").reshape(3, 512, 512).astype(np.float64)
            )
            # Cubey qz has reversed native Y sign; these metrics only use its magnitude.
            yield int(frame["time_s"]), *fields

    return bed, frames()


def capture(out, entries):
    media = out / "media"
    media.mkdir(exist_ok=False)
    runtime = presentation.runtime_identity()
    assets = []
    for entry in entries:
        s = entry["case"]
        rate = s["rain_rate_mm_per_h"]
        requests = [
            ("collection", t) for t in sorted({7200, s["storm_end_s"], s["duration_s"]})
        ]
        requests.append(("overview", s["duration_s"]))
        for camera, t in requests:
            name = f"rain{rate}-{camera}-{t:05d}s"
            asset = presentation.still_asset(s["name"], camera, t)
            asset.update(width=1280, height=720)
            command = presentation.app_command(
                asset, media / f"{name}.png", "scenic", False
            )
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
                    "path": f"media/{name}.png",
                    "sha256": presentation.sha256_file(media / f"{name}.png"),
                    "receipt": receipt,
                }
            )
            print("captured " + name, flush=True)
        name = f"rain{rate}-collection-formation"
        count = s["duration_s"] // CADENCE + 1
        asset = presentation.video_asset(
            name, s["name"], "collection", 0, count, 6, CADENCE
        )
        asset.update(
            width=1280,
            height=720,
            saved_field_interval_s=CADENCE,
            timeline=presentation.timeline(0, count, CADENCE, CADENCE),
        )
        command = presentation.app_command(
            asset, media / f"{name}.mp4", "scenic", False
        )
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
                "path": f"media/{name}.mp4",
                "sha256": presentation.sha256_file(media / f"{name}.mp4"),
                "receipt": receipt,
            }
        )
        print("captured " + name, flush=True)
    presentation.assert_same_runtime(
        runtime, presentation.runtime_identity(), "equal-total rainfall captures"
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


def maps(out, entries):
    os.environ["MPLCONFIGDIR"] = str(out / "cache/matplotlib")
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import LightSource, LogNorm

    figure, axes = plt.subplots(3, 3, figsize=(16, 14), layout="constrained")
    extent = (0, 15.36, 15.36, 0)
    artist = None
    max_depth = max(
        r["max_depth_m"] for e in entries for r in e["metrics"]["diagnostic_snapshots"]
    )
    for column, entry in enumerate(entries):
        s = entry["case"]
        bed, frames = recording_states(Path(entry["recording"]))
        wanted = {7200, s["storm_end_s"], s["duration_s"]}
        depths = {t: h for t, h, _, _ in frames if t in wanted}
        shade = LightSource(315, 45).hillshade(bed, dx=30, dy=30)
        for row, (t, caption) in enumerate(
            (
                (7200, "At 2 physical hours"),
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
            axis.set_title(
                f"{s['rain_rate_mm_per_h']} mm/h · {t / 3600:g} h elapsed\n{caption}"
            )
            axis.set_xlabel("Crop X (km)")
            axis.set_ylabel("Crop Z (km)")
    figure.colorbar(
        artist,
        ax=axes,
        label="Saved water depth (m), log scale; films below 0.1 m hidden",
        shrink=0.65,
    )
    figure.suptitle(
        "Same terrain and total rainfall; different delivery rates", fontsize=18
    )
    figure.savefig(out / "media/water-comparison.png", dpi=110)
    plt.close(figure)
    figure, axes = plt.subplots(
        3, 1, figsize=(11, 10), sharex=True, layout="constrained"
    )
    for entry in entries:
        s = entry["case"]
        rows = entry["metrics"]["observations"]
        hours = [r["time_s"] / 3600 for r in rows]
        for axis, key, scale in zip(
            axes,
            ("b20_area_at_least_1m_km2", "material_wet_area_km2", "stored_m3"),
            (1, 1, 1e-6),
        ):
            (line,) = axis.plot(
                hours,
                [r[key] * scale for r in rows],
                label=f"{s['rain_rate_mm_per_h']} mm/h · {s['storm_end_s'] // 3600} h storm",
            )
            axis.axvline(
                s["storm_end_s"] / 3600,
                color=line.get_color(),
                linestyle=":",
                alpha=0.65,
            )
    axes[0].axhline(
        AREA_TARGET_KM2,
        color="gray",
        linestyle="--",
        label="Fixed 0.5 km² formation target",
    )
    for axis, ylabel in zip(
        axes,
        (
            "B20 area ≥1 m deep (km²)",
            "Whole-map wet area ≥0.1 m (km²)",
            "Whole-map storage (million m³)",
        ),
    ):
        axis.set_ylabel(ylabel)
        axis.grid(alpha=0.3)
    axes[0].legend()
    axes[-1].set_xlabel("Physical simulation hours; dotted lines mark each storm end")
    figure.savefig(out / "media/formation-history.png", dpi=130)
    plt.close(figure)


def report(out, entries):
    parts = [
        "<!doctype html><html lang='en'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>",
        "<title>Faster rainfall: equal-total lake formation comparison</title><style>body{font:16px system-ui;background:#18212a;color:#e2e8ed;max-width:1400px;margin:auto;padding:24px}a{color:#8acbff}img,video{max-width:100%;height:auto}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px}figure{margin:0}table{border-collapse:collapse}th,td{border:1px solid #52606d;padding:8px;text-align:right}section{margin:32px 0}@media(max-width:800px){.grid{grid-template-columns:1fr}table{font-size:13px}}</style>",
        "<h1>Does heavier rain build lakes faster?</h1><p>Same unmodified mountain map, completely dry start, released SynxFlow solver, and Cubey render settings. Baseline 120 mm/h × 8 h, compared with 240 × 4 h and 480 × 2 h. Each supplies 960 mm during the main storm plus exactly 1 mm during a proportionally shortened taper, then finishes at four hours after its main storm ends. This is a deliberate demo stress case, not realistic weather.</p>",
        "<p>The formation target was frozen before execution: at least <b>0.5 km² of B20 at ≥1 m water depth</b>. This measures a substantial collection, not the first visible puddle or proof of a calm permanent lake. Crossings are bracketed by five-minute saves.</p>",
        "<table><tr><th>Rain</th><th>Storm</th><th>Formation target</th><th>Peak wet area ≥0.1 m</th><th>Final storage</th><th>Final B20 area ≥1 m</th><th>Native call time</th></tr>",
    ]
    for e in entries:
        s, m = e["case"], e["metrics"]
        cross = m["formation_target_first_crossing"]
        onset = (
            "not reached"
            if cross is None
            else f"({cross['after_s'] / 60:g}, {cross['at_or_before_s'] / 60:g}] min"
        )
        final = m["observations"][-1]
        parts.append(
            f"<tr><td>{s['rain_rate_mm_per_h']} mm/h</td><td>{s['storm_end_s'] // 3600} h</td><td>{onset}</td><td>{m['peak_material_wet_area_km2']:.2f} km²</td><td>{final['stored_m3'] / 1e6:.2f} million m³</td><td>{final['b20_area_at_least_1m_km2']:.3f} km²</td><td>{e['native_call_wall_s']:.1f} s</td></tr>"
        )
    parts += [
        "</table><p>All saved states passed finite/nonnegative-depth and dry-momentum checks. DEM and numerical bed hashes match the reference; native input changes are restricted to rainfall and runtime. Stored water stays below scheduled input plus the serialization allowance. This is not a complete calibrated water-budget or convergence validation.</p>",
        "<section><h2>Same physical time: two hours after a dry start</h2><p>Read left to right: 120, 240, 480 mm/h. By this time they have received 240, 480 and 960 mm respectively. This tests faster buildup; the final-state comparison below instead matches total input.</p><div class='grid'>",
    ]
    for e in entries:
        rate = e["case"]["rain_rate_mm_per_h"]
        path = f"media/rain{rate}-collection-07200s.png"
        parts.append(
            f"<figure><a href='{path}'><img src='{path}' alt='{rate} mm/h at two physical hours'></a><figcaption>{rate} mm/h · 2 hours</figcaption></figure>"
        )
    parts += [
        "</div></section><section><h2>Whole-map truth and formation histories</h2><p>Blue is actual saved water depth, using the same scale in all nine panels. Thin films below 0.1 m are hidden. Top: same physical time. Middle: same main-storm input. Bottom: same total input and recession duration. Greater blue coverage is flood spread, not necessarily more lakes.</p><a href='media/water-comparison.png'><img src='media/water-comparison.png' alt='Three rain rates at two hours, storm end and recession end'></a><img src='media/formation-history.png' alt='Collection growth, flood spread and stored volume over physical time'></section><section><h2>After the same four-hour recession window</h2><div class='grid'>"
    ]
    for e in entries:
        s = e["case"]
        rate, t = s["rain_rate_mm_per_h"], s["duration_s"]
        path = f"media/rain{rate}-collection-{t:05d}s.png"
        parts.append(
            f"<figure><a href='{path}'><img src='{path}' alt='{rate} mm/h after recession'></a><figcaption>{rate} mm/h · {t // 3600} h elapsed</figcaption></figure>"
        )
    parts += [
        "</div></section><section><h2>Full formation and recession replays</h2><p>All clips use the same time scale: five physical minutes per saved frame, six frames/s (30 physical minutes per video second). Different clip lengths reflect different experiment durations. These replay saved fields, not a live solver or physically interpolated water between samples.</p><div class='grid'>"
    ]
    for e in entries:
        s = e["case"]
        rate, t = s["rain_rate_mm_per_h"], s["duration_s"]
        parts.append(
            f"<figure><video controls preload='metadata' poster='media/rain{rate}-collection-{t:05d}s.png' src='media/rain{rate}-collection-formation.mp4'></video><figcaption>{rate} mm/h · {t // 3600} physical hours</figcaption></figure>"
        )
    parts += [
        "</div></section><h2>Limits and evidence</h2><p>Higher rain changes the physics; it is not a simulation-clock multiplier. Shorter storms allow less time for drainage during supply, so equal input need not yield identical retained water. The open crop boundary, absent outside catchment, no infiltration/evaporation/erosion, and 30 m cells remain unchanged. Large retained stores may still be flowing. Native-call timings include native solver I/O but exclude Python setup, analysis, conversion and rendering; they are single runs on a shared GPU, not a performance benchmark. No default has been promoted. Human visual acceptance remains pending.</p><p><a href='protocol.json'>Frozen protocol</a> · <a href='comparison.json'>Complete comparison and per-basin observations</a> · <a href='capture-manifest.json'>Capture provenance</a> · <a href='../natural-lakes-v1-20261008-a1/index.html'>Original 120 mm/h report</a></p></html>"
    ]
    presentation.write_text_exclusive(out / "index.html", "\n".join(parts))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--child", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.child:
        native.child(args.child, study_prefix=PREFIX, launch_source=Path(__file__))
        return
    if not args.out:
        parser.error("--out is required")
    out = args.out.resolve()
    cases = prepare(out)
    ref = recession.reference(recession.load())
    labels = np.load(ref.DEPRESSION_LABELS, allow_pickle=False)
    bed, frames = recording_states(BASELINE / "recording")
    base_result = json.loads(
        (BASELINE / "cases/dry-start-rain-and-recession/case-result.json").read_text()
    )
    entries = [
        {
            "case": cases[0],
            "metrics": summarize(frames, cases[0], bed, labels),
            "native_call_wall_s": base_result["native_call_wall_s"],
            "recording": str(BASELINE / "recording"),
            "reused_baseline": True,
        }
    ]
    for s in cases[1:]:
        result, recording = run_case(out / "cases" / s["name"], s, ref, labels)
        entries.append(
            {
                "case": s,
                "metrics": result["metrics"],
                "native_call_wall_s": result["native_call_wall_s"],
                "recording": str(recording),
                "reused_baseline": False,
            }
        )
    frozen = json.loads((out / "protocol.json").read_text())
    if frozen["baseline_identity"] != baseline_identity():
        raise ValueError("existing baseline changed during the sweep")
    native.write(
        out / "comparison.json",
        {
            "schema": SCHEMA,
            "entries": entries,
            "baseline_unchanged": True,
            "human_visual_acceptance": "pending",
        },
    )
    capture(out, entries)
    maps(out, entries)
    report(out, entries)
    print("report " + str(out / "index.html"), flush=True)


if __name__ == "__main__":
    main()
