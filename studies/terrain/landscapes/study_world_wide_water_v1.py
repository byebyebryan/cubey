#!/usr/bin/env python3
"""Prepared-world wider captures, reusing the existing offline bake contract.

No generation, live-core splice, GUI, solver edits or automatic defaults.
The legacy wider study remains an independent retained recipe and product.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

import study_wider_water_v1 as bake

ROOT = bake.ROOT
sys.path[:0] = [str(bake.FLUID), str(ROOT / "projects/terrain/tools")]
from terrain_water_sites import WORLD_CATALOGUE, load_sites, validate_world_site
from study_world_water_v1 import run_controller

CANDIDATES = {"pilot-128m": (1024, 128), "wide-64m": (2048, 64),
              "fallback-32m": (2048, 32)}
EXPORT = ROOT / "build/release/projects/fluid/fluid_25d/cubey_project_fluid_25d_export_water_capture"


def owned_root(path):
    path = Path(path)
    resolved = path.resolve()
    if (resolved.parent != ROOT / "outputs/terrain" or path.is_symlink() or
            not resolved.name.startswith("wider-water-world-")):
        raise ValueError("owned outputs/terrain/wider-water-world-* root required")
    return resolved


def init(out):
    if out.exists():
        raise ValueError("fresh study root required")
    out.mkdir()
    receipts = []
    for sid, recipe in load_sites(WORLD_CATALOGUE).items():
        binding = validate_world_site(recipe)
        protocol = bake.freeze(out / sid, source=ROOT / recipe["manifest"], candidates=CANDIDATES,
            center=recipe["center_xz_m"], terrain_site=binding,
            milestones=(7200, 21600, 43200), wall_limit=1800)
        receipts.append({"site":sid, "binding":binding, "protocol_sha256":bake.digest(out / sid / "protocol.json"),
                         "grids":protocol["grids"]})
    bake.write(out / "study.json", {"schema":"cubey.terrain.world-wide-water-study.v1",
        "catalogue_sha256":bake.digest(WORLD_CATALOGUE), "sites":receipts,
        "scope":"Independent coherent saved states; no live splice, terrain edits or default promotion"})


def verify(out):
    doc = json.loads((out / "study.json").read_bytes())
    if doc["catalogue_sha256"] != bake.digest(WORLD_CATALOGUE):
        raise ValueError("world catalogue changed")
    for site in doc["sites"]:
        leaf = out / site["site"]
        if bake.digest(leaf / "protocol.json") != site["protocol_sha256"]:
            raise ValueError("frozen world-wide protocol changed")
        bake.check_frozen(leaf)
    return doc


def export(out, sid, candidate):
    leaf = out / sid
    result = json.loads((leaf / "runs" / candidate / "result.json").read_bytes())
    if result["status"] != "completed" or result["errors"]:
        raise ValueError("cannot accept a partial/failed wider bake")
    milestone = result["milestones"][-1]
    capture = leaf / "captures" / candidate
    command = ["rtk", "proxy", str(EXPORT), milestone["recording"],
               bake.check_frozen(leaf)["source"], str(capture)]
    with (leaf / f"export-{candidate}.log").open("x") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
    bake.write(capture / "water-binding.json", binding(out, sid, capture))


def binding(out, sid, capture):
    p = bake.check_frozen(out / sid)
    site_path = Path(p["terrain_site"]["manifest_path"])
    site = json.loads(site_path.read_bytes())
    return {"schema":"cubey.terrain.site-water-binding.v1", "site_id":site["site_id"],
            "world_id":site["world_id"], "site_manifest_sha256":bake.digest(site_path),
            "world_recipe_sha256":bake.digest(site_path.parent / "world-recipe.json"),
            "heightfield_manifest_sha256":p["source_sha256"],
            "capture":{"path":"water-capture.json", "sha256":bake.digest(capture / "water-capture.json")}}


def publish(out, sid, candidate):
    source = out / sid / "captures" / candidate
    identity = bake.digest(source / "water-binding.json")
    target = ROOT / "cache/terrain/water/v1/world-wide" / f"{sid}-{candidate}-{identity[:16]}"
    expected = {p.name:bake.digest(p) for p in source.iterdir() if p.is_file()}
    if target.exists():
        if target.is_symlink() or {p.name:bake.digest(p) for p in target.iterdir() if p.is_file()} != expected:
            raise ValueError("published world water changed; no overwrite")
    else:
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source, target)
    bake.write(out / sid / f"published-{candidate}.json", {"water_binding":str(target / "water-binding.json"),
        "terrain_site":bake.check_frozen(out / sid)["terrain_site"]["manifest_path"],
        "binding_sha256":identity, "opt_in":True})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("init", "geometry", "prepare", "run", "export", "publish"))
    parser.add_argument("out", type=Path)
    parser.add_argument("--site", choices=tuple(load_sites(WORLD_CATALOGUE)), default="mountain")
    parser.add_argument("--candidate", choices=tuple(CANDIDATES), default="pilot-128m")
    args = parser.parse_args()
    out = owned_root(args.out)
    if args.phase == "init":
        init(out)
        return
    verify(out)
    leaf = out / args.site
    if args.phase == "geometry":
        command = ["rtk", "proxy", str(ROOT / "build/release/tests/cubey/cubey_terrain_world_water_lod_preflight")]
        result = subprocess.run(command, capture_output=True, text=True, check=True, timeout=60)
        bake.write(out / "geometry.json", {"command":command,
            "rows":[json.loads(line) for line in result.stdout.splitlines()],
            "scope":"CPU topology feasibility only, not render performance or hydraulic fields"})
    elif args.phase == "prepare":
        bake.prepare(leaf, args.candidate)
    elif args.phase == "run":
        # Supervise Python directly; its finally block owns the independent worker.
        command = [str(bake.check_frozen(leaf)["python"]), "-u", "-c",
                   "import sys; from pathlib import Path; sys.path.insert(0, sys.argv[1]); "
                   "import study_wider_water_v1 as s; s.execute(Path(sys.argv[2]), sys.argv[3])",
                   str(Path(__file__).parent), str(leaf), args.candidate]
        run_controller(command, leaf / f"controller-{args.candidate}.log", timeout=2280)
    elif args.phase == "export":
        export(out, args.site, args.candidate)
    else:
        publish(out, args.site, args.candidate)
    verify(out)


if __name__ == "__main__":
    main()
