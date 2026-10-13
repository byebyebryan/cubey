"""Read-only prepared-site admission shared by offline consumers.

Mirrors TerrainSite's producer/placement/coverage contract; raster payloads are
validated by the existing source loaders. No generator or hydraulic dependency.
"""
import hashlib
import json
import math
from pathlib import Path

from terrain_world_coverage import Box, site_plan


def read_json(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 65536:
        raise ValueError("unsafe/missing/oversized terrain site metadata")
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate terrain site metadata key")
            result[key] = value
        return result
    return json.loads(path.read_bytes(), object_pairs_hook=pairs)


def child(root, name):
    p = Path(name)
    if p.is_absolute() or not p.parts or ".." in p.parts:
        raise ValueError("terrain site reference escapes package")
    resolved = (root / p).resolve(strict=True)
    if not resolved.is_relative_to(root.resolve(strict=True)):
        raise ValueError("terrain site reference escapes package")
    return resolved


def load_site(path):
    path = Path(path)
    doc = read_json(path)
    world = read_json(child(path.parent, "world-recipe.json"))
    identity = json.loads(json.dumps(world))
    world_id = identity.pop("world_id")
    identity["producer"].pop("model_snapshot", None)
    identity["producer"].get("settings", {}).pop("device", None)
    expected = hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(",", ":"),
                                        allow_nan=False).encode()).hexdigest()
    if (world["schema"] != "cubey.terrain.world-recipe.v1" or expected != world_id or
            doc["schema"] != "cubey.terrain.site-coverage.v1" or doc["world_id"] != world_id or
            doc["admitted"] is not True or doc["rejection_reason"] is not None):
        raise ValueError("terrain site world/admission mismatch")
    center = doc["center_xz_m"]
    if (len(center) != 2 or any(isinstance(v, bool) or not isinstance(v, (int, float)) or
                              not math.isfinite(v) for v in center) or len(doc["levels"]) != 4):
        raise ValueError("invalid terrain site geometry")
    spans = tuple(level["span_m"] for level in doc["levels"])
    camera, sampling = doc["camera_margin_m"], doc["sampling_margin_m"]
    if camera > 10000 or sampling > 10000 or spans[-1] > 262144:
        raise ValueError("terrain site coverage budget exceeded")
    plan = site_plan(world, doc["site_id"], center, spans=spans,
                     camera_margin=camera, sampling_margin=sampling)
    if not plan["admitted"] or json.loads(json.dumps(plan["levels"])) != doc["levels"]:
        raise ValueError("invalid nested terrain site coverage")
    heightfield = child(path.parent, doc["heightfield"])
    climate = child(path.parent, doc["surface_fields"])
    field = read_json(heightfield)
    mapping, grid, native = world["coordinate_mapping"], field["grid"], field["provenance"]["model_native_origin"]
    spacing = mapping["native_spacing_m"]
    origin = [mapping["world_origin_xz_m"][0] + (native["j"] - mapping["model_origin_ij"][1]) * spacing,
              mapping["world_origin_xz_m"][1] + (native["i"] - mapping["model_origin_ij"][0]) * spacing]
    if (field["schema"] != "cubey.terrain.heightfield.v1" or field["seed"] != world["seed"] or
            field["source"] != world["producer"] or field["height"] != world["height_transform"] or
            field["provenance"]["world_id"] != world_id or spacing != grid["sample_spacing_m"] or
            grid["axis_mapping"] != {"world_x": "model_j", "world_z": "model_i"} or
            any(abs(a - b) > .001 for a, b in zip(origin, (grid["sample_origin_x_m"], grid["sample_origin_z_m"]))) or
            min(grid["width"], grid["height"]) < 2 or grid["width"] * grid["height"] > 25000000):
        raise ValueError("terrain site producer/coordinate binding mismatch")
    actual = Box(tuple(origin), tuple(o + (n - 1) * spacing for o, n in zip(origin, (grid["width"], grid["height"]))))
    if not actual.contains(Box.centered(center, spans[-1], sampling)):
        raise ValueError("prepared terrain site coverage incomplete")
    read_json(climate)
    return {"site": doc, "world": world, "heightfield": heightfield, "surface_fields": climate}
