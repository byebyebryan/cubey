#!/usr/bin/env python3
"""Prepare opt-in power-of-two native cases using existing shared terrain sources.

Run in the already installed optional SynxFlow IO environment. This script
prepares inputs only: no CUDA execution, installs, terrain generation or fallback.
"""
from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path
import sys
import subprocess
import time

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "projects/terrain/tools"))
from terrain_grid_sampling import grid_geometry, load_source, sample_grid, sha256

from terrain_water_sites import DEFAULT_CATALOGUE, load_sites, validate_world_site

SITES = load_sites()
CONFIGS = {"patch-512-32m": (512, 32), "patch-1024-16m": (1024, 16),
           "patch-2048-8m": (2048, 8), "small-512-16m": (512, 16),
           "large-2048-16m": (2048, 16)}
HISTORICAL_CONFIG = "historical-512-30m"


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n")


def prepare(site: str, config: str, out: Path, rainfall: float = 512, sites=None) -> dict:
    sites = SITES if sites is None else sites
    if out.exists() or out.is_symlink() or not out.parent.is_dir():
        raise ValueError("case output must be a fresh leaf")
    if site not in sites or config not in (*CONFIGS, HISTORICAL_CONFIG) or rainfall not in (512, 1024):
        raise ValueError("unknown approved site/configuration/rainfall")
    if config == HISTORICAL_CONFIG and site != "mountain":
        raise ValueError("historical control exists only for the original mountain site")
    recipe = sites[site]
    site_identity = validate_world_site(recipe)
    source = ROOT / recipe["manifest"]
    manifest, raw = load_source(source)
    if manifest["files"]["elevation"]["sha256"] != recipe["elevation_sha256"]:
        raise ValueError("catalogued source elevation identity changed")
    g = manifest["grid"]
    cg = json.loads((source.parent / "surface-fields.json").read_text())["grid"]
    bounds = (max(g["sample_origin_x_m"], cg["sample_origin_x_m"]),
              max(g["sample_origin_z_m"], cg["sample_origin_z_m"]),
              min(g["sample_origin_x_m"] + (g["width"] - 1) * g["sample_spacing_m"],
                  cg["sample_origin_x_m"] + (cg["width"] - 1) * cg["sample_spacing_m"]),
              min(g["sample_origin_z_m"] + (g["height"] - 1) * g["sample_spacing_m"],
                  cg["sample_origin_z_m"] + (cg["height"] - 1) * cg["sample_spacing_m"]))
    center = tuple(recipe["center_xz_m"])
    n, spacing = (512, 30) if config == HISTORICAL_CONFIG else CONFIGS[config]
    grid = grid_geometry(n, n, spacing, center, bounds)
    if site_identity and grid["center_xz_m"] != list(center):
        raise ValueError("prepared world site crop cannot be shifted")
    bed = sample_grid(manifest, raw, grid)
    out.mkdir()
    bed.tofile(out / "source-bed.f32")
    source_identity = {"schema": "cubey.terrain.sampled-source.v1",
                       "manifest_path": str(source.resolve()), "manifest_sha256": sha256(source),
                       "elevation_sha256": manifest["files"]["elevation"]["sha256"],
                       "source_grid": g, "target_grid": grid,
                       "source_bed_path": "source-bed.f32", "source_bed_sha256": sha256(out / "source-bed.f32"),
                       "interpolated": True, "terrain_modified": False}
    if site_identity:
        source_identity["terrain_site"] = site_identity
    write_json(out / "surface-grid.json", source_identity)
    spec = {"name": f"{site}-{config}-rain{int(rainfall)}", "family": "live-perf-terrain-v1",
            "rows": n, "cols": n, "dx_m": spacing, "initial_depth_m": 0., "boundary": "fall",
            "manning_n": 0.05, "duration_s": 72000, "rain_history_end_s": 72000,
            "output_interval_s": 7200, "rain_rate_m_per_s": rainfall / 3600000,
            "rain_rate_mm_per_h": rainfall, "site": site, "configuration": config,
            "terrain_source_identity": source_identity}
    write_json(out / "case-spec.json", spec)
    # Reuse the established optional InputModel preparation API, not solver math.
    import run_native_runoff_reuse_v1 as native
    native.write_ascii(out / "DEM.asc", bed, spacing)
    if config == HISTORICAL_CONFIG:
        import run_mountain_rain_demo as demo
        original = demo.CASE / "native/input"
        if demo.input_tree(original) != demo.CASE_INPUT_SHA:
            raise ValueError("historical reference input changed")
        shutil.copytree(original, out / "native/input")
        (out / "native/output").mkdir()
        (out / "native/input/times_setup.dat").write_text("0\n72000\n7200\n72000\n")
        rate = rainfall / 3600000
        (out / "native/input/field/precipitation_source_all.dat").write_text(f"1\n0 {rate:.17g}\n72000 {rate:.17g}\n")
        inputs = native._input_hashes(out / "native/input")
    else:
        inputs = native._make_model_inputs(spec, out)
    audit = native._native_input_audit(spec, out)
    _, solver_bed = native._grid_from_native_ids(out / "native/input/field/z.dat", n, n)
    solver_bed.astype("<f4").tofile(out / "solver-bed.f32")
    source_identity["solver_bed_sha256"] = sha256(out / "solver-bed.f32")
    write_json(out / "surface-grid.json", source_identity)
    write_json(out / "case-spec.json", spec)
    protocol = {"schema": "cubey.fluid25d.live-perf-case.v1", "frozen_before_native_execution": True,
                "case": spec, "terrain_source_identity": source_identity,
                "input_hashes": inputs, "input_audit": audit,
                "water_volume_for_one_hour_rain_m3": n * n * spacing ** 2 * rainfall / 1000,
                "source_inputs_unchanged": sha256(source) == source_identity["manifest_sha256"]}
    if config == HISTORICAL_CONFIG:
        import run_mountain_rain_demo as demo
        original = demo.CASE / "native/input"
        if demo.input_tree(original) != demo.CASE_INPUT_SHA:
            raise ValueError("historical reference input changed")
        # Preserve the old numerical bed and all non-forcing field values exactly.
        historical_fields = {str(path.relative_to(original)): sha256(path)
                             for path in original.rglob("*") if path.is_file()}
        allowed = {"times_setup.dat", "field/precipitation_source_all.dat"}
        changed = [key for key, value in historical_fields.items() if inputs.get(key) != value]
        if any(key not in allowed for key in changed):
            raise ValueError("historical control changed a non-rainfall/runtime input: " + str(changed))
        protocol["historical_control"] = {"reference_input_sha256": demo.CASE_INPUT_SHA,
            "changed_native_paths": changed, "native_bed_and_nonforcing_fields_exact": True}
    write_json(out / "case-protocol.json", protocol)
    return protocol


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--site", required=True)
    p.add_argument("--site-catalogue", type=Path, default=DEFAULT_CATALOGUE)
    p.add_argument("--configuration", required=True, choices=(*CONFIGS, HISTORICAL_CONFIG, "all"))
    p.add_argument("--rain", type=float, default=512, choices=(512, 1024))
    p.add_argument("--out", required=True, type=Path)
    a = p.parse_args()
    sites = load_sites(a.site_catalogue)
    if a.site not in (*sites, "all"):
        p.error("site not in the selected catalogue")
    if a.site == "all" or a.configuration == "all":
        out = a.out.absolute()
        if out.exists() or out.is_symlink() or not out.parent.is_dir():
            raise ValueError("batch output must be a fresh leaf")
        out.mkdir()
        selected_sites = tuple(sites) if a.site == "all" else (a.site,)
        configs = tuple(CONFIGS) if a.configuration == "all" else (a.configuration,)
        # Screen-size preparations first; never choose crops from simulation outcomes.
        configs = sorted(configs, key=lambda key: (key != "patch-1024-16m", key))
        results = []
        for config in configs:
            for site in selected_sites:
                label = f"{site}-{config}"
                command = ["rtk", "proxy", sys.executable, "-u", str(Path(__file__).resolve()),
                           "--site", site, "--configuration", config, "--rain", str(a.rain),
                           "--site-catalogue", str(a.site_catalogue.resolve()),
                           "--out", str(out / label)]
                started = time.monotonic()
                with (out / f"{label}.prepare.log").open("w") as log:
                    try:
                        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
                        status = "prepared" if process.wait(timeout=1200) == 0 else "failed"
                    except subprocess.TimeoutExpired:
                        import run_mountain_rain_demo as demo
                        demo.stop_owned(process)
                        status = "timeout"
                row = {"site": site, "configuration": config, "case": label, "status": status,
                       "preparation_wall_s": time.monotonic() - started, "command": command}
                results.append(row)
                write_json(out / "preparation.json", {"cases": results})
                print(json.dumps(row), flush=True)
        return
    result = prepare(a.site, a.configuration, a.out.absolute(), a.rain, sites)
    print(json.dumps({"case": result["case"]["name"], "grid": result["case"]["terrain_source_identity"]["target_grid"],
                      "audit": result["input_audit"]}, allow_nan=False), flush=True)


if __name__ == "__main__":
    main()
