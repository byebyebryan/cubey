#!/usr/bin/env python3
"""Explicit, dependency-free installation of pinned saved landscapes; never runs producers.

Copy exact retained study packages into a selected cache. A fresh hydraulic bake
is not assumed to reproduce recording/provenance metadata: new assets require
an explicitly reviewed catalog revision. Existing valid packages are reused;
existing incomplete or changed packages are never overwritten.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[3]
CATALOG = ROOT / "projects/terrain/terrain_landscape_presets.json"
WATER_LOCATIONS = {
    "mountain-local": ("mountain", "fallback-32m"),
    "mountain-wide": ("mountain", "wide-64m"),
    "mountain-interior-wide": ("mountain-interior", "wide-64m"),
    "valley-interior-wide": ("valley-interior", "wide-64m"),
}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate metadata key")
        result[key] = value
    return result


def read(path):
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 1048576:
        raise ValueError(f"invalid metadata: {path}")
    return json.loads(path.read_bytes(), object_pairs_hook=unique)


def child(root, name):
    relative = Path(name)
    if not relative.parts or relative.is_absolute() or ".." in relative.parts:
        raise ValueError("package path must be relative and contained")
    path = root / relative
    if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
        raise ValueError("package symlink escapes root")
    return path


def pinned(root, ref):
    path = child(root, ref["path"])
    if not path.is_file() or digest(path) != ref["sha256"]:
        raise ValueError(f"pinned metadata mismatch: {path}")
    return path


def fields(root, records):
    for record in records:
        path = child(root, record["path"])
        if not path.is_file() or path.stat().st_size != record["byte_count"] or digest(path) != record["sha256"]:
            raise ValueError(f"payload mismatch: {path}")


def site_package(root, ref, site_id):
    manifest = pinned(root, {"path": "site.json", "sha256": ref["sha256"]})
    site = read(manifest)
    if site["site_id"] != site_id or not site["admitted"]:
        raise ValueError("site identity/admission mismatch")
    world = read(child(root, "world-recipe.json"))
    if site["world_id"] != world["world_id"]:
        raise ValueError("site/world mismatch")
    for key in ("heightfield", "surface_fields"):
        path = child(root, site[key])
        fields(path.parent, read(path)["files"].values())
    fields(root, site["tier_fields"])
    return site


def water_package(root, ref, terrain):
    binding = read(pinned(root, {"path": "water-binding.json", "sha256": ref["sha256"]}))
    for key, filename in (("site_manifest_sha256", "site.json"),
                          ("world_recipe_sha256", "world-recipe.json"),
                          ("heightfield_manifest_sha256", "heightfield.json")):
        if binding[key] != digest(child(terrain, filename)):
            raise ValueError("water/source identity mismatch")
    site = read(child(terrain, "site.json"))
    if binding["site_id"] != site["site_id"] or binding["world_id"] != site["world_id"]:
        raise ValueError("water/site identity mismatch")
    capture = pinned(root, binding["capture"])
    fields(root, read(capture)["fields"].values())


def copy_once(source, destination, validate):
    if destination.exists():
        validate(destination)
        return "reused"
    validate(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".landscape-install-", dir=destination.parent))
    try:
        for path in source.rglob("*"):
            if path.is_symlink():
                raise ValueError("refuse source package symlinks")
        shutil.copytree(source, staging, dirs_exist_ok=True)
        validate(staging)
        # Never merge into a changed/incomplete package or replace an existing
        # nonempty directory. Sources and caller caches remain untouched on failure.
        if destination.exists():
            raise ValueError("destination appeared during installation")
        staging.rename(destination)
        return "installed"
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def prepare(catalog_path, cache, terrain_study=None, water_study=None, ids=()):
    catalog = read(catalog_path)
    if catalog["schema"] != "cubey.terrain.landscape-catalog.v1":
        raise ValueError("unsupported catalog")
    presets = {p["id"]: p for p in catalog["presets"]}
    if len(presets) != len(catalog["presets"]):
        raise ValueError("duplicate preset")
    result = []
    for id in ids or presets:
        preset = presets[id]
        target = child(cache, preset["site"]["path"]).parent
        validator = lambda folder: site_package(folder, preset["site"], preset["site_id"])
        if terrain_study:
            status = copy_once(child(terrain_study / "sites", preset["site_id"]), target, validator)
        else:
            validator(target)
            status = "verified"
        water_status = "not requested"
        if preset["water"]:
            water_target = child(cache, preset["water"]["path"]).parent
            water_validator = lambda folder: water_package(folder, preset["water"], target)
            if water_study:
                sid, candidate = WATER_LOCATIONS[id]
                source = child(water_study, f"{sid}/captures/{candidate}")
                water_status = copy_once(source, water_target, water_validator)
            else:
                water_validator(water_target)
                water_status = "verified"
        result.append({"id": id, "terrain": status, "water": water_status})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=CATALOG)
    parser.add_argument("--cache-root", type=Path, default=ROOT / "cache/terrain")
    parser.add_argument("--from-terrain-study", type=Path)
    parser.add_argument("--from-water-study", type=Path)
    parser.add_argument("--preset", action="append", default=[])
    args = parser.parse_args()
    print(json.dumps(prepare(args.catalog, args.cache_root, args.from_terrain_study,
                             args.from_water_study, args.preset), indent=2))


if __name__ == "__main__":
    main()
