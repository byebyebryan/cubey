"""Small, CPU-only crop catalogue; elevations use shared Terrain sampling.

Recipes select existing immutable terrain, never generate or modify it. Missing
assets remain a preparation/admission error rather than triggering a fallback.
"""
from __future__ import annotations

import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_CATALOGUE = Path(__file__).with_name("terrain_water_sites.json")
SCHEMA = "cubey.fluid25d.terrain-sites.v1"
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
    if set(doc) != {"schema", "default_site", "sites"} or doc["schema"] != SCHEMA:
        raise ValueError("incompatible terrain site catalogue")
    if not isinstance(doc["sites"], list) or not 1 <= len(doc["sites"]) <= 6:
        raise ValueError("site catalogue must contain 1..6 sites (five presets each)")
    result = {}
    for site in doc["sites"]:
        if not isinstance(site, dict) or set(site) != {"id", "label", "manifest", "elevation_sha256", "center_xz_m"}:
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
                not relative.as_posix().startswith("cache/terrain/sources/v1/") or
                relative.name != "heightfield.json"):
            raise ValueError("site must reference the existing Terrain source cache")
        if not isinstance(site["elevation_sha256"], str) or not SHA.fullmatch(site["elevation_sha256"]):
            raise ValueError("invalid source elevation identity")
        center = site["center_xz_m"]
        if (not isinstance(center, list) or len(center) != 2 or
                any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) for v in center)):
            raise ValueError("invalid crop center")
        result[sid] = site
    if not isinstance(doc["default_site"], str) or doc["default_site"] not in result:
        raise ValueError("default site is not in the catalogue")
    return result
