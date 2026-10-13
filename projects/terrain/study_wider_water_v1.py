#!/usr/bin/env python3
"""Explicit offline wider-water study; no GUI, installs or numerical edits.

Preparation reuses the existing Terrain LOD0 sampler and SynxFlow InputModel.
Execution owns one external worker and retains sparse binary rain-on snapshots.
The old capture/source and launch defaults are never changed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
FLUID = ROOT / "projects/fluid/fluid_25d"
SOURCE = ROOT / "cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json"
OLD_CAPTURE = ROOT / "cache/terrain/water/v1/mountain-rain-fed-1024-16m/water-capture.json"
CANDIDATES = {"pilot-64m": (957, 64), "wide-32m": (1913, 32)}
MILESTONES = (7200, 21600, 38152.7734375, 72000)


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write(path, value):
    raw = json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n"
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(raw)
    temporary.replace(path)


def geometry(candidate, candidates=None, center=(0, 0)):
    n, spacing = (CANDIDATES if candidates is None else candidates)[candidate]
    half = (n - 1) * spacing / 2
    return {"width": n, "height": n, "spacing_m": spacing,
            "sample_origin_xz_m": [v-half for v in center], "center_xz_m": list(center),
            "drawn_span_m": 2 * half, "cells": n * n}


def owned_root(out):
    out = out.absolute()
    if (out.parent.resolve() != ROOT / "outputs/terrain" or
            not out.name.startswith("wider-water-") or out.is_symlink()):
        raise ValueError("fresh wider-water-* leaf under outputs/terrain required")
    return out


def old_identity():
    return {p.name: digest(p) for p in OLD_CAPTURE.parent.iterdir() if p.is_file()}


def freeze(out, *, source=SOURCE, candidates=None, center=(0, 0), terrain_site=None,
           milestones=MILESTONES, wall_limit=1800):
    if out.exists():
        raise ValueError("study root already exists")
    sys.path.insert(0, str(FLUID))
    import run_mountain_rain_demo as demo
    if digest(demo.EXTENSION) != demo.EXTENSION_SHA:
        raise ValueError("optional solver extension differs from its accepted pin")
    source = Path(source).resolve(strict=True)
    manifest = json.loads(source.read_text())
    elevation = source.parent / manifest["files"]["elevation"]["path"]
    if digest(elevation) != manifest["files"]["elevation"]["sha256"]:
        raise ValueError("terrain payload differs from manifest")
    out.mkdir()
    for folder in ("cases", "runs", "captures"):
        (out / folder).mkdir()
    protocol = {"schema": "cubey.terrain.wider-water-study.v1",
                "source": str(source), "source_sha256": digest(source),
                "source_elevation_sha256": digest(elevation),
                "climate_manifest_sha256": digest(source.parent / "surface-fields.json"),
                "old_capture_identity": old_identity(),
                "native_extension": str(demo.EXTENSION), "native_extension_sha256": demo.EXTENSION_SHA,
                "python": str(demo.LIVE_PYTHON), "runner_sha256": digest(__file__),
                "worker_sha256": digest(FLUID / "external_synxflow/session_worker.py"),
                "grids": {name: geometry(name, candidates, center)
                          for name in (CANDIDATES if candidates is None else candidates)},
                "terrain_site": terrain_site,
                "rain_mm_per_hour": 512, "manning_n": .05, "boundary": "fall",
                "initial_depth_m": 0, "milestones_s": list(milestones),
                "wall_limit_s_per_run": wall_limit,
                "camera_envelope": {"focus_world_xz_m": list(center) if terrain_site else [11520, 19200],
                    "orbit_yaws_rad": [i * 3.141592653589793 / 4 for i in range(8)],
                    "landscape_span_m": [11457.6, 8192],
                    "target_offsets_xz_m": [[0, 0], [-1000, 0], [1000, 0], [0, -1000], [0, 1000]],
                    "finite_source_boundary_stress_is_not_supported_view": True},
                "scope": "new optional wider capture; no splice, solver math, new terrain, default promotion or push"}
    write(out / "protocol.json", protocol)
    return protocol


def check_frozen(out):
    p = json.loads((out / "protocol.json").read_text())
    source = Path(p["source"])
    if (digest(__file__) != p["runner_sha256"] or digest(source) != p["source_sha256"] or
            old_identity() != p["old_capture_identity"] or
            digest(Path(p["native_extension"])) != p["native_extension_sha256"] or
            digest(FLUID / "external_synxflow/session_worker.py") != p["worker_sha256"] or
            digest(source.parent / "surface-fields.json") != p["climate_manifest_sha256"]):
        raise ValueError("frozen study input or runner changed")
    elevation = json.loads(source.read_text())["files"]["elevation"]["path"]
    if digest(source.parent / elevation) != p["source_elevation_sha256"]:
        raise ValueError("source elevation changed")
    if p.get("terrain_site"):
        sys.path.insert(0, str(ROOT / "projects/terrain/tools"))
        from terrain_site_package import load_site
        binding = p["terrain_site"]
        path = Path(binding["manifest_path"])
        if digest(path) != binding["manifest_sha256"]:
            raise ValueError("pinned site changed")
        package = load_site(path)
        if (package["heightfield"] != source or package["site"]["world_id"] != binding["world_id"] or
                any(g["center_xz_m"] != package["site"]["center_xz_m"] for g in p["grids"].values())):
            raise ValueError("site/source/world placement changed")
    return p


def prepare(out, candidate):
    p = check_frozen(out)
    sys.path[:0] = [str(FLUID), str(ROOT / "projects/terrain/tools")]
    import terrain_grid_sampling as sampling
    import run_native_runoff_reuse_v1 as native
    import numpy as np
    case = out / "cases" / candidate
    if case.exists():
        raise ValueError("refusing to overwrite prepared case")
    source = Path(p["source"])
    manifest, raw = sampling.load_source(source)
    g = manifest["grid"]
    cg = json.loads((source.parent / "surface-fields.json").read_text())["grid"]
    bounds = (max(g["sample_origin_x_m"], cg["sample_origin_x_m"]),
              max(g["sample_origin_z_m"], cg["sample_origin_z_m"]),
              min(g["sample_origin_x_m"] + (g["width"]-1)*g["sample_spacing_m"],
                  cg["sample_origin_x_m"] + (cg["width"]-1)*cg["sample_spacing_m"]),
              min(g["sample_origin_z_m"] + (g["height"]-1)*g["sample_spacing_m"],
                  cg["sample_origin_z_m"] + (cg["height"]-1)*cg["sample_spacing_m"]))
    planned = p["grids"][candidate]
    n, spacing = planned["width"], planned["spacing_m"]
    target = sampling.grid_geometry(n, n, spacing, planned["center_xz_m"], bounds)
    if (target["center_xz_m"] != planned["center_xz_m"] or
            target["sample_origin_xz_m"] != planned["sample_origin_xz_m"]):
        raise ValueError("frozen common extent cannot be admitted without shifting")
    case.mkdir()
    bed = sampling.sample_grid(manifest, raw, target)
    bed.tofile(case / "source-bed.f32")
    identity = {"schema": "cubey.terrain.sampled-source.v1", "manifest_path": str(source),
                "manifest_sha256": p["source_sha256"], "elevation_sha256": p["source_elevation_sha256"],
                "source_grid": g, "target_grid": target, "source_bed_path": "source-bed.f32",
                "source_bed_sha256": digest(case / "source-bed.f32"), "interpolated": True,
                "terrain_modified": False}
    if p.get("terrain_site"):
        identity["terrain_site"] = p["terrain_site"]
    site_id = (Path(p["terrain_site"]["manifest_path"]).parent.name
               if p.get("terrain_site") else "mountain")
    spec = {"name": candidate, "family": "live-perf-terrain-v1", "site": site_id,
            "configuration": candidate, "rows": n, "cols": n, "dx_m": spacing,
            "initial_depth_m": 0, "boundary": "fall", "manning_n": .05,
            "duration_s": 72000, "rain_history_end_s": 72000, "output_interval_s": 7200,
            "rain_rate_m_per_s": 512 / 3600000, "rain_rate_mm_per_h": 512,
            "terrain_source_identity": identity}
    native.write_ascii(case / "DEM.asc", bed, spacing)
    inputs = native._make_model_inputs(spec, case)
    audit = native._native_input_audit(spec, case)
    _, solver_bed = native._grid_from_native_ids(case / "native/input/field/z.dat", n, n)
    solver_bed.astype("<f4").tofile(case / "solver-bed.f32")
    identity["solver_bed_sha256"] = digest(case / "solver-bed.f32")
    write(case / "surface-grid.json", identity)
    write(case / "case-spec.json", spec)
    write(case / "case-protocol.json", {"schema": "cubey.fluid25d.live-perf-case.v1",
          "frozen_before_native_execution": True, "case": spec, "terrain_source_identity": identity,
          "input_hashes": inputs, "input_audit": audit})
    if np.any(solver_bed < bed - .01) or np.any(solver_bed > bed + .01):
        raise ValueError("native bed differs beyond existing text quantization tolerance")
    print(json.dumps({"prepared": candidate, "geometry": target, "audit": audit}), flush=True)


def execute(out, candidate):
    p = check_frozen(out)
    milestones = p["milestones_s"]
    sys.path.insert(0, str(FLUID))
    import run_live_perf_terrain_v1 as helpers
    import run_terrain_rain_demo as terrain
    import run_mountain_rain_demo as demo
    case = out / "cases" / candidate
    identity = terrain.case_identity(case)
    run = out / "runs" / candidate
    run.mkdir()
    session = run / "session"
    argv = ["rtk", "proxy", p["python"], "-u", str(FLUID / "external_synxflow/session_worker.py"),
            "--extension", p["native_extension"], "--case", str(case), "--out", str(session),
            "--speed", "300", "--publish-interval", "1", "--paused", "--telemetry"]
    result = {"command": argv, "status": "failed", "milestones": [], "errors": [],
              "input_identity": identity, "wall_limit_s": p["wall_limit_s_per_run"]}
    started = time.monotonic()
    with (run / "worker.log").open("xb") as log:
        worker = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            demo.wait_ready(worker, session, 180)
            initial = helpers.snapshot(session)
            result["resume"] = helpers.command(session, "resume")
            compute_started = time.monotonic()
            next_milestone = 0
            while True:
                state = helpers.read_state(session)
                physical = state["frame"]["physical_time_s"]
                elapsed = time.monotonic() - compute_started
                write(run / "progress.json", {"physical_time_s": physical, "compute_wall_s": elapsed,
                    "physical_wall_ratio": physical / max(elapsed, .001), "lifecycle": state["frame"]["lifecycle"]})
                terminal = physical >= milestones[-1] or elapsed >= p["wall_limit_s_per_run"]
                if (next_milestone < len(milestones) and physical >= milestones[next_milestone]) or terminal:
                    final = helpers.snapshot(session)
                    actual = final[0]["frame"]["physical_time_s"]
                    recording = helpers.save_recording(case, session, initial, final,
                        run / f"snapshot-{len(result['milestones'])}")
                    row = {"target_physical_s": milestones[min(next_milestone, len(milestones)-1)],
                           "physical_s": actual, "wall_s": elapsed, "recording": str(recording),
                           "stats": helpers.field_stats(final[1], identity["spec"]["dx_m"])}
                    result["milestones"].append(row)
                    next_milestone += 1
                    write(run / "result.json", result)
                    print(json.dumps(row), flush=True)
                if terminal:
                    result["status"] = "completed" if physical >= milestones[-1] else "bounded-partial"
                    break
                if worker.poll() is not None or state["frame"]["lifecycle"] == "failed":
                    raise RuntimeError("external worker failed before a complete milestone")
                time.sleep(.5)
        except Exception as error:
            result["errors"].append(str(error))
        finally:
            if worker.poll() is None:
                try:
                    result["stop"] = helpers.command(session, "stop")
                    worker.wait(timeout=30)
                except Exception as error:
                    result["errors"].append("worker stop: " + str(error))
            result["cleanup"] = demo.stop_owned(worker)
    result["total_wall_s"] = time.monotonic() - started
    completion = session / "completion.json"
    result["completion"] = json.loads(completion.read_text()) if completion.exists() else None
    if (not result["completion"] or not result["completion"].get("inputs_unchanged") or
            not result["completion"].get("reference_inputs_unchanged")):
        result["errors"].append("missing immutable native completion receipt")
    if terrain.case_identity(case) != identity:
        result["errors"].append("case/source identity changed during execution")
    check_frozen(out)
    if result["errors"]:
        result["status"] = "failed"
    write(run / "result.json", result)
    print(json.dumps({k: result[k] for k in ("status", "total_wall_s", "errors")}), flush=True)
    if result["status"] == "failed":
        raise RuntimeError("wider water execution failed")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("phase", choices=("freeze", "prepare", "run"))
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--candidate", choices=tuple(CANDIDATES), default="pilot-64m")
    a = p.parse_args()
    out = owned_root(a.out)
    if a.phase == "freeze":
        freeze(out)
    elif a.phase == "prepare":
        prepare(out, a.candidate)
    else:
        execute(out, a.candidate)


if __name__ == "__main__":
    main()
