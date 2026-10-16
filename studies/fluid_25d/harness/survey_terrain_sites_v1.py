#!/usr/bin/env python3
"""Bounded cached-terrain audition: locator scores, actual rain, Vulkan receipts.

Screen uses the existing optional hydrology environment; run uses the installed
SynxFlow environment. No installs, generation, terrain edits or per-site tuning.
"""
from __future__ import annotations

import argparse
import html
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

import numpy as np
from scipy import ndimage

import run_terrain_rain_demo as terrain
import run_mountain_rain_demo as demo
from terrain_water_sites import ROOT, SCHEMA, load_sites

sys.path.insert(0, str(ROOT / "projects/terrain/tools"))
from terrain_grid_sampling import grid_geometry, load_source, sample_grid, sha256


def write(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n")


def run_capture(command, log_path, env, timeout=180):
    """Bound the whole owned renderer group, including the RTK wrapper's child."""
    with log_path.open("wb") as log:
        process = subprocess.Popen(command, env=env, stdout=log,
            stderr=subprocess.STDOUT, start_new_session=True)
        try:
            returncode = process.wait(timeout=timeout)
            if returncode:
                raise subprocess.CalledProcessError(returncode, command)
        finally:
            cleanup = demo.stop_owned(process, grace_s=5)
    return cleanup


def bounds_for(manifest, climate):
    grids = (manifest["grid"], climate["grid"])
    return (max(g["sample_origin_x_m"] for g in grids), max(g["sample_origin_z_m"] for g in grids),
            min(g["sample_origin_x_m"] + (g["width"] - 1) * g["sample_spacing_m"] for g in grids),
            min(g["sample_origin_z_m"] + (g["height"] - 1) * g["sample_spacing_m"] for g in grids))


def screen(out):
    import pyflwdir
    seen, rows = set(), []
    for source in sorted((ROOT / "cache/terrain/sources/v1").rglob("heightfield.json")):
        if not (source.parent / "surface-fields.json").is_file():
            continue
        manifest, raw = load_source(source)
        identity = manifest["files"]["elevation"]["sha256"]
        if identity in seen:
            continue
        if len(seen) >= 12:
            raise ValueError("cached corpus grew beyond the frozen 12-source screen cap")
        seen.add(identity)
        bounds = bounds_for(manifest, json.loads((source.parent / "surface-fields.json").read_bytes()))
        for cx in (-12000, 0, 12000):
            for cz in (-12000, 0, 12000):
                grid = grid_geometry(256, 256, 64, (cx, cz), bounds)
                # Require the large-area preset to retain the same logical center.
                large = grid_geometry(2048, 2048, 16, (cx, cz), bounds)
                if large["center_xz_m"] != [cx, cz]:
                    continue
                bed = sample_grid(manifest, raw, grid)
                filled, directions = pyflwdir.dem.fill_depressions(bed.copy(), outlets="edge", connectivity=8)
                routing = pyflwdir.from_array(directions, ftype="d8", check_ftype=True)
                accumulation = routing.upstream_area(unit="cell")
                dz, dx = np.gradient(bed, 64)
                slope = np.hypot(dx, dz)
                labels, count = ndimage.label(filled - bed > .5)
                areas = np.bincount(labels.ravel(), minlength=count + 1).astype(float) * 64**2
                volumes = np.bincount(labels.ravel(), weights=(filled-bed).ravel(), minlength=count+1) * 64**2
                areas[0], volumes[0] = 0, 0
                basin = int(np.argmax(volumes))
                robust = float(np.percentile(bed, 95) - np.percentile(bed, 5))
                row = {"manifest": str(source.relative_to(ROOT)), "elevation_sha256": identity,
                       "center_xz_m": [cx, cz], "source": source.parent.name,
                       "relief_p95_p05_m": robust, "slope_p90": float(np.percentile(slope, 90)),
                       "analytical_basin_area_km2": float(areas[basin] / 1e6),
                       "analytical_basin_capacity_m3": float(volumes[basin]),
                       "routed_channel_cells": int(np.count_nonzero(accumulation > 200)),
                       "max_contributing_cells": int(accumulation.max()),
                       "sample_bed_sha256": __import__("hashlib").sha256(bed.tobytes()).hexdigest()}
                rows.append(row)
        print(json.dumps({"screened_source": source.parent.name, "crops": len(rows)}), flush=True)
    chosen = []
    scores = {
        "basin": lambda r: r["analytical_basin_area_km2"] * np.log1p(r["analytical_basin_capacity_m3"]),
        "branch": lambda r: r["routed_channel_cells"] * min(r["relief_p95_p05_m"] / 250, 1),
        "steep": lambda r: r["slope_p90"] * np.sqrt(r["relief_p95_p05_m"]),
    }
    # Two candidates per target, no near-duplicate crops or duplicate elevations
    # within a target. Scores are analytical locators, not acceptance gates.
    for rank in (1, 2):
        for target, score in scores.items():
            eligible = [r for r in rows if not any(
                r["elevation_sha256"] == c["elevation_sha256"] and
                (c["target"] == target or np.linalg.norm(np.array(r["center_xz_m"])-c["center_xz_m"]) < 8192)
                for c in chosen)]
            best = max(eligible, key=score)
            chosen.append(dict(best, id=f"{target}-{rank}", label=f"{target.title()} candidate {rank}", target=target))
    recipes = [{key: r[key] for key in ("id", "label", "manifest", "elevation_sha256", "center_xz_m")} for r in chosen]
    write(out / "candidate-sites.json", {"schema": SCHEMA, "default_site": recipes[0]["id"], "sites": recipes})
    write(out / "screen.json", {"unique_elevations": len(seen), "crops": rows, "shortlist": chosen,
        "policy": "256x256 at 64m analytical locators only; raw elevations unchanged; same 16.384km coverage; six hydraulic pilots frozen before execution"})
    print(json.dumps({"unique_elevations": len(seen), "crops": len(rows), "shortlist": chosen}), flush=True)


def water_metrics(planes, shape, spacing):
    from run_live_perf_terrain_v1 import field_stats
    base = field_stats(planes, spacing)
    h, qx, qz = np.frombuffer(planes, dtype="<f4").reshape(3, *shape)
    speed = np.hypot(qx, qz) / np.maximum(h, 1e-30)
    labels, count = ndimage.label((h >= 1) & (speed < .5))
    sizes = np.bincount(labels.ravel(), minlength=count + 1)
    sizes[0] = 0
    base.update(material_wet_km2=float(np.count_nonzero(h >= .1) * spacing**2 / 1e6),
        flowing_km2=float(np.count_nonzero((h >= .1) & (speed >= .5)) * spacing**2 / 1e6),
        largest_slow_deep_patch_km2=float(sizes.max() * spacing**2 / 1e6),
        wet_speed_p95_m_per_s=float(np.percentile(speed[h >= .1], 95)) if (h >= .1).any() else 0)
    return base


def run(out, catalog, site, configuration, horizon, wall):
    from run_live_perf_terrain_v1 import snapshot, command, digest
    from prepare_live_perf_terrain_v1 import prepare
    recipes = load_sites(catalog)
    selected = list(recipes) if site == "all" else [site]
    if any(s not in recipes for s in selected):
        raise ValueError("unknown survey site")
    (out / "cases").mkdir(exist_ok=True)
    (out / "runs").mkdir(exist_ok=True)
    for sid in selected:
        case = out / "cases" / f"{sid}-{configuration}"
        if not case.exists():
            prepare(sid, configuration, case, 512, recipes)
        before = terrain.case_identity(case)
        leaf = out / "runs" / case.name
        leaf.mkdir()
        recording = leaf / "recording"
        recording.mkdir()
        session = leaf / "session"
        started = time.monotonic()
        frames, captures = [], []
        worker = terrain.worker_command(case, session, 300)
        with (leaf / "worker.log").open("wb") as log:
            process = subprocess.Popen(worker, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                while not (session / "state.json").exists():
                    if process.poll() is not None or time.monotonic() - started > 90:
                        raise RuntimeError("survey worker failed startup")
                    time.sleep(.1)
                initial = snapshot(session)
                next_time = 0
                resumed = False
                while time.monotonic() - started <= wall:
                    state, planes, capture = snapshot(session)
                    physical = state["frame"]["physical_time_s"]
                    if physical >= next_time:
                        path = recording / f"frame-{len(frames):04d}.f32"
                        path.write_bytes(planes)
                        metrics = water_metrics(planes, (before["spec"]["rows"], before["spec"]["cols"]), before["spec"]["dx_m"])
                        frames.append({"time_s": physical, "path": path.name, "sha256": digest(planes), **metrics})
                        captures.append({k: v for k, v in capture.items() if k != "_state_raw"})
                        print(json.dumps({"site": sid, "physical_s": physical, "wall_s": time.monotonic()-started, **metrics}), flush=True)
                        next_time = physical + 300
                    if physical >= horizon:
                        break
                    if not resumed:
                        command(session, "resume")
                        resumed = True
                    if process.poll() is not None:
                        raise RuntimeError("survey worker exited before target")
                    time.sleep(.05)
                command(session, "pause")
                final = snapshot(session)
                # Retain the exact paused endpoint even if it missed the cadence.
                if final[0]["frame"]["physical_time_s"] > frames[-1]["time_s"]:
                    planes = final[1]
                    path = recording / f"frame-{len(frames):04d}.f32"
                    path.write_bytes(planes)
                    frames.append({"time_s": final[0]["frame"]["physical_time_s"], "path": path.name,
                        "sha256": digest(planes), **water_metrics(planes, (before["spec"]["rows"], before["spec"]["cols"]), before["spec"]["dx_m"])})
                    captures.append({k: v for k, v in final[2].items() if k != "_state_raw"})
            finally:
                cleanup = demo.stop_owned(process)
        if terrain.case_identity(case) != before:
            raise RuntimeError("survey altered immutable case")
        for name, source in (("bed.f32", session / "bed.f32"), ("source-bed.f32", case / "source-bed.f32")):
            shutil.copy2(source, recording / name)
        duration = frames[-1]["time_s"]
        spec = before["spec"]
        doc = {"schema": "cubey.fluid25d.recording.v1", "encoding": "float32-little-endian", "field_layout": "planar",
            "fields": ["h_m", "qx_m2_per_s", "qz_m2_per_s"],
            "grid": {"width": spec["cols"], "height": spec["rows"], "cell_size_m": spec["dx_m"],
                "storage_order": "row-major", "row_direction": "world-z-positive", "sample_location": "cell-center"},
            "bed": {"path": "bed.f32", "sha256": sha256(recording / "bed.f32")},
            "source_bed": {"path": "source-bed.f32", "sha256": sha256(recording / "source-bed.f32")},
            "frames": frames, "protocol": dict(spec, duration_s=duration, output_interval_s=300,
                rainfall_history=[[0, spec["rain_rate_m_per_s"]], [duration, spec["rain_rate_m_per_s"]]]),
            "provenance": {"field_source": "validated external service binary snapshots", "captures": captures,
                "case_protocol_sha256": before["protocol_sha256"], "extension_sha256": demo.EXTENSION_SHA}}
        write(recording / "recording.json", doc)
        write(leaf / "result.json", {"site": sid, "configuration": configuration, "target_physical_s": horizon,
            "reached_target": duration >= horizon, "last_physical_s": duration, "wall_s": time.monotonic()-started,
            "rain_mm_per_hour": 512, "frames": len(frames), "final": frames[-1], "command": worker,
            "cleanup": cleanup, "immutable_case_unchanged": True})
        print(json.dumps({"completed": sid, "reached_target": duration >= horizon, "frames": len(frames)}), flush=True)


def capture(out, configuration, sites):
    from run_offscreen_perf_v2 import environment
    for sid in sites:
        case = out / "cases" / f"{sid}-{configuration}"
        leaf = out / "runs" / case.name
        recording = leaf / "recording/recording.json"
        if (leaf / "captures.json").is_file():
            for receipt in json.loads((leaf / "captures.json").read_bytes()):
                if sha256(leaf / receipt["artifact"]) != receipt["sha256"]:
                    raise ValueError("sealed visual receipt changed")
            continue
        seal_recording(case, leaf)
        result = json.loads((leaf / "result.json").read_bytes())
        end = result["last_physical_s"]
        receipts = []
        for view in ("overview", "flow"):
            base = terrain.viewer_command(case, recording, "Site survey", live=False, start=end,
                app=terrain.RELEASE_APP, camera="overview", width=1280, height=720)
            # The old collection camera is a mountain-specific authored point,
            # not a locator for these crops. Use a matched full-map diagnostic.
            if view == "flow":
                base += ["--fluid25d-catchment-view", "flow-inspection"]
            for kind in ("png", "mp4"):
                target = leaf / f"{view}.{kind}"
                cmd = [*base, "--headless", "--fluid25d-recording-gpu-validation", "--output", str(target)]
                if kind == "mp4":
                    cmd[cmd.index("--frames") + 1] = "240"
                    cmd[cmd.index("--fluid25d-recording-time-seconds") + 1] = "0"
                    cmd += ["--capture", "video", "--fps", "24", "--fluid25d-recording-frame-interval-seconds", str(end/239)]
                cleanup = run_capture(cmd, leaf / f"{view}-{kind}.log", environment("offscreen"))
                raw = (leaf / f"{view}-{kind}.log").read_text(errors="replace")
                if (not target.is_file() or "vulkan validation error" in raw.lower() or
                        "NVIDIA GeForce RTX 5070 Ti" not in raw or "fluid_25d_recording_upload: PASS" not in raw):
                    raise RuntimeError("invalid headless visual receipt")
                receipts.append({"artifact": target.name, "sha256": sha256(target), "command": cmd,
                    "cleanup": cleanup,
                    "scope": "Vulkan offscreen reconstruction of sampled native states; accelerated replay, not live timing"})
        write(leaf / "captures.json", receipts)


def seal_recording(case, leaf):
    """Seal retained exact native snapshots, never invent uniform timestamps."""
    path = leaf / "recording/recording.json"
    doc = json.loads(path.read_bytes())
    if doc["provenance"].get("capture_schema") == "cubey.fluid25d.external-binary-capture.v1":
        return  # Previously sealed recording is immutable.
    identity = terrain.case_identity(case)
    session = leaf / "session"
    frames, captures = doc["frames"], doc["provenance"]["captures"]
    if len(frames) != len(captures):
        raise ValueError("capture count mismatch")
    for frame, capture in zip(frames, captures):
        if frame["time_s"] != capture["physical_time_s"] or sha256(path.parent / frame["path"]) != frame["sha256"]:
            raise ValueError("retained native snapshot changed")
    doc["provenance"].update(capture_schema="cubey.fluid25d.external-binary-capture.v1",
        source_sha256={"case_spec": sha256(case / "case-spec.json"),
            "case_protocol": identity["protocol_sha256"], "metadata": sha256(session / "metadata.json"),
            "native_numerical_bed": sha256(session / "bed.f32")})
    path.rename(path.with_name("raw-timeline.json"))
    write(path, doc)


def capture_motion(out, catalog):
    """Short late-flow view, distinct from the six-hour formation timelapse."""
    from run_offscreen_perf_v2 import environment
    for sid in load_sites(catalog):
        case = out / "cases" / f"{sid}-patch-1024-16m"
        leaf = out / "runs" / case.name
        recording = leaf / "recording/recording.json"
        seal_recording(case, leaf)
        end = json.loads((leaf / "result.json").read_bytes())["last_physical_s"]
        # Headless captures must begin on an actual saved state. Keep that
        # contract rather than weakening it for a decorative clip.
        times = [frame["time_s"] for frame in json.loads(recording.read_bytes())["frames"]]
        start = max(t for t in times if t <= end-119.5)
        target = leaf / "late-flow.mp4"
        receipt = leaf / "late-flow-receipt.json"
        if receipt.is_file():
            if sha256(target) != json.loads(receipt.read_bytes())["sha256"]:
                raise ValueError("late-flow receipt changed")
            continue
        command = terrain.viewer_command(case, recording, "Late flow cues", live=False,
            start=start, app=terrain.RELEASE_APP, width=1280, height=720)
        command[command.index("--frames") + 1] = "240"
        command += ["--headless", "--capture", "video", "--fps", "24",
            "--fluid25d-recording-frame-interval-seconds", ".5", "--fluid25d-recording-gpu-validation",
            "--output", str(target)]
        cleanup = run_capture(command, leaf / "late-flow.log", environment("offscreen"))
        raw = (leaf / "late-flow.log").read_text(errors="replace")
        if ("fluid_25d_recording_upload: PASS" not in raw or "vulkan validation error" in raw.lower()
                or "NVIDIA GeForce RTX 5070 Ti" not in raw):
            raise RuntimeError("invalid late-motion GPU receipt")
        write(receipt, {"sha256": sha256(target), "command": command, "physical_start_s": start,
            "physical_end_s": start+119.5, "speedup": 12, "cleanup": cleanup,
            "scope": "12x playback of interpolated native fields; dots/trails and foam illustrate native velocity, not native particles or solver-performance evidence"})


def prepare_presets(out, catalog, baseline):
    from prepare_live_perf_terrain_v1 import prepare, CONFIGS
    recipes = load_sites(catalog)
    # Stage the expensive remaining presets only after every default-grid
    # confirmation finishes. Besides avoiding needless work on rejected sites,
    # this keeps preparation and the hydraulic collector from sharing a writer.
    for sid in recipes:
        result = out / "runs" / f"{sid}-patch-1024-16m/result.json"
        if not result.is_file() or not json.loads(result.read_bytes())["reached_target"]:
            raise ValueError("finish default-grid confirmation before preparing all presets: " + sid)
    rows = []
    for sid in recipes:
        for config in CONFIGS:
            case = out / "cases" / f"{sid}-{config}"
            started = time.monotonic()
            if not case.exists():
                prepare(sid, config, case, 512, recipes)
            identity = terrain.case_identity(case)
            row = {"id": case.name, "protocol_sha256": identity["protocol_sha256"],
                   "input_sha256": identity["input_sha256"], "wall_s": time.monotonic()-started}
            rows.append(row)
            write(out / "preset-preparation.json", {"cases": rows})
            print(json.dumps(row), flush=True)
    originals = []
    if baseline:
        for entry in terrain.catalogue(baseline.resolve(strict=True)):
            if not entry["available"]:
                continue
            source = Path(entry["case_dir"])
            target = out / "cases" / source.name
            before = terrain.case_identity(source)
            if not target.exists():
                shutil.copytree(source, target)
            after = terrain.case_identity(target)
            if {k:v for k,v in before.items() if k != "case"} != {k:v for k,v in after.items() if k != "case"}:
                raise ValueError("baseline prepared case did not transfer byte-identically")
            originals.append({"id": entry["id"], "protocol_sha256": before["protocol_sha256"], "exact": True})
    write(out / "preset-preparation.json", {"cases": rows, "baseline_cases": originals})


def audit(out):
    rows = []
    for path in sorted((out / "runs").glob("*/recording/recording.json")):
        doc = json.loads(path.read_bytes())
        previous = -1
        for frame in doc["frames"]:
            if frame["time_s"] <= previous or sha256(path.parent / frame["path"]) != frame["sha256"]:
                raise ValueError("snapshot identity/timeline changed")
            previous = frame["time_s"]
            stats = water_metrics((path.parent / frame["path"]).read_bytes(),
                (doc["grid"]["height"], doc["grid"]["width"]), doc["grid"]["cell_size_m"])
            supplied = (doc["grid"]["width"] * doc["grid"]["height"] * doc["grid"]["cell_size_m"]**2 *
                        doc["protocol"]["rain_rate_m_per_s"] * frame["time_s"])
            if stats["water_volume_m3"] > supplied * 1.02 + 1:
                raise ValueError("retained volume exceeds rain supply (2% sanity tolerance)")
        result = json.loads((path.parents[1] / "result.json").read_bytes())
        if not result["reached_target"] or result["cleanup"]["active_group_remaining"]:
            raise ValueError("incomplete horizon or owned process leak")
        rows.append({"case": path.parents[1].name, "frames": len(doc["frames"]),
            "finite_nonnegative_dry_momentum_clean": True, "volume_not_above_supply_2pct": True,
            "target_reached": True, "owned_group_gone": True})
    write(out / "field-audit.json", {"cases": rows,
        "scope": "Field/hash/timeline/supply sanity checks, not a native flux-conservation or hydrology-accuracy proof"})


def report(out):
    cards, pilots = [], []
    for path in sorted((out / "runs").glob("*/result.json")):
        r = json.loads(path.read_bytes())
        leaf = path.parent.relative_to(out).as_posix()
        f = r["final"]
        card = f'<article id="{html.escape(r["site"])}"><h2>{html.escape(r["site"])} · {html.escape(r["configuration"])}</h2><p>{r["last_physical_s"]/3600:.2f} simulated hours; {r["wall_s"]:.1f}s observed wall time; target reached: {r["reached_target"]}. Wet ≥0.1m: {f["material_wet_km2"]:.2f}km²; largest slow ≥1m patch: {f["largest_slow_deep_patch_km2"]:.2f}km²; wet speed P95: {f["wet_speed_p95_m_per_s"]:.2f}m/s.</p><a href="{leaf}/overview.png"><img loading="lazy" src="{leaf}/overview.png"></a><video controls preload="metadata" poster="{leaf}/overview.png" src="{leaf}/overview.mp4"></video><details><summary>Full-map flow diagnostic and receipts</summary><img loading="lazy" src="{leaf}/flow.png"><video controls preload="none" src="{leaf}/flow.mp4"></video><a href="{leaf}/result.json">Result</a> · <a href="{leaf}/captures.json">GPU receipts</a></details></article>'
        if (path.parent / "late-flow-receipt.json").is_file():
            card = card.replace('</article>', f'<p>Late flow cues: two physical minutes near the end at 12×, not the six-hour formation timelapse. Dots/foam are render-only velocity illustrations.</p><video controls preload="none" poster="{leaf}/overview.png" src="{leaf}/late-flow.mp4"></video></article>')
        (cards if r["configuration"] == "patch-1024-16m" else pilots).append(card)
    decision = json.loads((out / "selection.json").read_bytes()) if (out / "selection.json").exists() else {}
    introduction = '<p>' + html.escape(decision.get("stage", "Survey in progress")) + '</p><ul>'
    for row in decision.get("selected", []):
        introduction += f'<li><a href="#{html.escape(row["site"])}">{html.escape(row["site"])}</a>: {html.escape(row["reason"])}</li>'
    introduction += '</ul><details><summary>Why the other candidates were not added</summary><ul>'
    for row in decision.get("not_added", []):
        introduction += f'<li>{html.escape(row["candidate"])}: {html.escape(row["reason"])}</li>'
    introduction += '</ul></details>'
    if (out / "review-receipt.json").is_file():
        introduction += '<p><a href="review-receipt.json">Final reviewed evidence receipt</a></p>'
    lifecycle = ''
    validation_path = out / "switch-validation/validation.json"
    if validation_path.is_file():
        validation = json.loads(validation_path.read_bytes())
        applied = sum(e["event"] == "action-applied" for e in validation["events"])
        installed = [e for e in validation["events"] if e["event"] == "installed"]
        actions = json.loads((out / "switch-matrix.json").read_bytes())["actions"]
        expected = {a["case_id"] for a in actions if a["kind"] == "switch"}
        valid = (not validation["errors"] and validation["returncode"] == 0 and applied == len(actions)
            and {e["case_id"] for e in installed} == expected
            and all(e["physical_s"] == 0 and e["hydraulic_dispatches"] == 0 for e in installed))
        lifecycle = f'<h2>Prepared selector and restart validation</h2><p>Status: {"PASS" if valid else "FAIL"}; {applied} acknowledged script actions, {len(installed)} replacements, every installation dry. All 15 new site/grid combinations were briefly run, followed by return to Mountain valley. This is offscreen lifecycle/upload validation, not native GUI acceptance or a steady-state FPS benchmark.</p><p><a href="switch-validation/validation.json">Validation and cleanup receipt</a> · <a href="switch-matrix.json">Exact script</a></p>'
    (out / "index.html").write_text('<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Terrain Water · site expansion</title><style>body{font:16px system-ui;background:#18212a;color:#e2e8ed;max-width:1200px;margin:auto;padding:24px}a{color:#8acbff}img,video{width:100%;height:auto}article{margin:36px 0}p,li{line-height:1.5}summary{cursor:pointer}</style><h1>Terrain Water · three new behaviours</h1>' + introduction + '<h2>How to read this</h2><p>Dry start, uniform continuous 512mm/h rain, open/fall boundaries, Manning 0.05. No source/drain, lake seeds, infiltration, erosion, terrain edits or per-site forcing. Same 16.384km footprint. Look for tributaries joining, collections expanding, and faster drainage along steeper terrain. The perimeter lets water leave; there is no drain marker. Scenic blue/green is optical and lighting color, not a depth/speed palette.</p><p>Videos compress six simulated hours into ten seconds from ~300s sampled states. Interpolation, rain streaks and dots/trails are presentation, not a realtime-performance measurement. Observed audition wall times include startup/teardown and concurrent capture/preparation work; they are not an isolated benchmark. Generated Terrain Diffusion terrain is not surveyed real terrain. A slow deep connected patch is a diagnostic, not proof of a lake or overflow.</p><p>The default remains Mountain valley, live 1k/16m, dry/paused, 60Hz rendering and requested 60× solver pace. New sites are selection + restart options; water is not transferred between terrain grids.</p><p><a href="screen.json">99-crop screen</a> · <a href="candidate-sites.json">Frozen pilots</a> · <a href="selection.json">Selection reasoning</a> · <a href="field-audit.json">Field audit</a> · <a href="preset-preparation.json">Prepared grids</a></p>' + lifecycle + ''.join(cards) + '<details><summary>Six coarse hydraulic pilots · includes reserves and rejected presentation</summary>' + ''.join(pilots) + '</details></html>')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=("screen", "run", "capture", "motion", "prepare-presets", "audit", "report"))
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--catalogue", type=Path)
    p.add_argument("--baseline-study", type=Path)
    p.add_argument("--site", default="all")
    p.add_argument("--configuration", default="patch-512-32m", choices=terrain.CONFIGS)
    p.add_argument("--horizon", type=float, default=21600)
    p.add_argument("--wall", type=float, default=240)
    a = p.parse_args()
    out = a.out.resolve(strict=True)
    if (out.parent != demo.ref.OUTPUT_ROOT.resolve() or
            not out.name.startswith("site-expansion-") or a.out.is_symlink()):
        p.error("survey root must be an existing owned leaf below outputs/fluid")
    if not 300 <= a.horizon <= 72000 or not 30 <= a.wall <= 1200:
        p.error("bounded horizon/wall required")
    catalog = (a.catalogue or out / "candidate-sites.json").resolve()
    if a.mode == "screen":
        if (out / "screen.json").exists():
            p.error("screen receipt already exists")
        screen(out)
    elif a.mode == "run":
        if sha256(demo.EXTENSION) != demo.EXTENSION_SHA:
            raise ValueError("optional native runtime changed")
        run(out, catalog, a.site, a.configuration, a.horizon, a.wall)
    elif a.mode == "capture":
        capture(out, a.configuration, list(load_sites(catalog)) if a.site == "all" else [a.site])
    elif a.mode == "prepare-presets":
        prepare_presets(out, catalog, a.baseline_study)
    elif a.mode == "motion":
        capture_motion(out, catalog)
    elif a.mode == "audit":
        audit(out)
    else:
        report(out)


if __name__ == "__main__":
    main()
