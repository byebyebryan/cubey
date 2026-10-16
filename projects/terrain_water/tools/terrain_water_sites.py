"""Small, CPU-only crop catalogue; elevations use shared Terrain sampling.

Recipes select existing immutable terrain, never generate or modify it. Missing
assets remain a preparation/admission error rather than triggering a fallback.
"""
from __future__ import annotations

import json
import math
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_CATALOGUE = Path(__file__).with_name("terrain_water_sites.json")
SCHEMA = "cubey.fluid25d.terrain-sites.v1"
WORLD_SCHEMA = "cubey.fluid25d.world-sites.v1"
WORLD_CATALOGUE = Path(__file__).with_name("terrain_water_world_sites.json")
ID = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
SHA = re.compile(r"^[0-9a-f]{64}$")


def load_sites(path=DEFAULT_CATALOGUE):
    path = Path(path)
    if path.is_symlink() or path.stat().st_size > 65536:
        raise ValueError("unsafe/oversized terrain site catalogue")
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate site catalogue key")
            result[key] = value
        return result
    doc = json.loads(path.read_bytes(), object_pairs_hook=pairs)
    if set(doc) != {"schema", "default_site", "sites"} or doc["schema"] not in (SCHEMA, WORLD_SCHEMA):
        raise ValueError("incompatible terrain site catalogue")
    if not isinstance(doc["sites"], list) or not 1 <= len(doc["sites"]) <= 6:
        raise ValueError("site catalogue must contain 1..6 sites (five presets each)")
    result = {}
    for site in doc["sites"]:
        keys = {"id", "label", "manifest", "elevation_sha256", "center_xz_m"}
        world = doc["schema"] == WORLD_SCHEMA
        if world:
            keys |= {"terrain_site", "terrain_site_sha256", "world_id"}
        if not isinstance(site, dict) or set(site) != keys:
            raise ValueError("incompatible site recipe")
        sid, label = site["id"], site["label"]
        if not isinstance(sid, str) or len(sid) > 64 or not ID.fullmatch(sid) or sid in result:
            raise ValueError("invalid/duplicate site ID")
        if not isinstance(label, str) or not label or len(label) > 128:
            raise ValueError("invalid site label")
        if not isinstance(site["manifest"], str):
            raise ValueError("invalid source path")
        relative = Path(site["manifest"])
        if (relative.is_absolute() or ".." in relative.parts or
                not relative.as_posix().startswith("cache/terrain/worlds/v1/" if world else "cache/terrain/sources/v1/") or
                relative.name != "heightfield.json"):
            raise ValueError("site must reference the existing Terrain source cache")
        if not isinstance(site["elevation_sha256"], str) or not SHA.fullmatch(site["elevation_sha256"]):
            raise ValueError("invalid source elevation identity")
        if world:
            p = Path(site["terrain_site"])
            if (p.is_absolute() or ".." in p.parts or p.parent != relative.parent or p.name != "site.json" or
                    any(not isinstance(site[k], str) or not SHA.fullmatch(site[k])
                        for k in ("terrain_site_sha256", "world_id"))):
                raise ValueError("invalid prepared world site binding")
        center = site["center_xz_m"]
        if (not isinstance(center, list) or len(center) != 2 or
                any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) for v in center)):
            raise ValueError("invalid crop center")
        result[sid] = site
    if not isinstance(doc["default_site"], str) or doc["default_site"] not in result:
        raise ValueError("default site is not in the catalogue")
    return result


def validate_world_site(recipe):
    """Missing packages disable a case at admission, not the whole catalogue."""
    if "terrain_site" not in recipe:
        return None
    sys.path.insert(0, str(ROOT / "projects/terrain/tools"))
    from terrain_site_package import load_site
    path = ROOT / recipe["terrain_site"]
    import hashlib
    if hashlib.sha256(path.read_bytes()).hexdigest() != recipe["terrain_site_sha256"]:
        raise ValueError("prepared terrain site changed")
    package = load_site(path)
    if (package["heightfield"] != (ROOT / recipe["manifest"]).resolve(strict=True) or
            package["site"]["center_xz_m"] != recipe["center_xz_m"] or
            package["site"]["world_id"] != recipe["world_id"]):
        raise ValueError("prepared terrain site recipe/coordinates changed")
    return {"manifest_path": str(path.resolve(strict=True)), "manifest_sha256": recipe["terrain_site_sha256"],
            "world_id": recipe["world_id"]}
