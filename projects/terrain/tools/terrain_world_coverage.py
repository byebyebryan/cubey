"""Portable world/site coverage contract. No Torch, generation, or water dependency.

World coordinates retain the existing source's frame. A site is admitted only
when its largest requested backdrop plus camera/sampling margins fits the world.
Actual generated coverage must be reported separately from these requested boxes.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass
import hashlib
import json
import math

WORLD_SCHEMA = "cubey.terrain.world-recipe.v1"
SITE_SCHEMA = "cubey.terrain.site-coverage.v1"
WORLD_SPAN_M = 262144.0
LEVEL_SPANS_M = (16384.0, 32768.0, 65536.0, 131072.0)
CAMERA_MARGIN_M = 1000.0
SAMPLING_MARGIN_M = 480.0


@dataclass(frozen=True)
class Box:
    minimum: tuple[float, float]
    maximum: tuple[float, float]

    def __post_init__(self):
        if (len(self.minimum) != 2 or len(self.maximum) != 2
                or not all(math.isfinite(v) for v in (*self.minimum, *self.maximum))
                or any(a >= b for a, b in zip(self.minimum, self.maximum))):
            raise ValueError("invalid coverage box")

    def contains(self, other: Box) -> bool:
        return all(a <= c and d <= b for a, b, c, d in
                   zip(self.minimum, self.maximum, other.minimum, other.maximum))

    @classmethod
    def centered(cls, center, span, margin=0):
        if (len(center) != 2 or not math.isfinite(span) or span <= 0
                or not math.isfinite(margin) or margin < 0):
            raise ValueError("invalid coverage request")
        radius = span / 2 + margin
        return cls(tuple(v - radius for v in center), tuple(v + radius for v in center))


def world_recipe(source: dict, span_m=WORLD_SPAN_M) -> dict:
    """Bind a broad world to one existing source, without changing its coordinates."""
    g = source["grid"]
    spacing = g["sample_spacing_m"]
    native = source["provenance"]["model_native_origin"]
    if (source["schema"] != "cubey.terrain.heightfield.v1"
            or source["source"]["generator"] != "terrain-diffusion"
            or g["axis_mapping"] != {"world_x": "model_j", "world_z": "model_i"}
            or not math.isfinite(spacing) or spacing <= 0):
        raise ValueError("unsupported world anchor")
    origin = (g["sample_origin_x_m"], g["sample_origin_z_m"])
    center = tuple(a + (n - 1) * spacing / 2 for a, n in
                   zip(origin, (g["width"], g["height"])))
    recipe = {"schema": WORLD_SCHEMA, "seed": source["seed"],
              "producer": source["source"], "height_transform": source["height"],
              "coordinate_mapping": {"world_origin_xz_m": list(origin),
                                     "model_origin_ij": [native["i"], native["j"]],
                                     "native_spacing_m": spacing},
              "survey_bounds": asdict(Box.centered(center, span_m)),
              "survey_is_not_render_coverage": True}
    # Ignore host-specific paths in identity, but retain them in provenance.
    identity = json.loads(json.dumps(recipe))
    identity["producer"].pop("model_snapshot", None)
    identity["producer"].get("settings", {}).pop("device", None)
    recipe["world_id"] = hashlib.sha256(json.dumps(identity, sort_keys=True,
        separators=(",", ":"), allow_nan=False).encode()).hexdigest()
    return recipe


def native_point(recipe: dict, world_xz) -> tuple[float, float]:
    mapping = recipe["coordinate_mapping"]
    x, z = world_xz
    ox, oz = mapping["world_origin_xz_m"]
    i, j = mapping["model_origin_ij"]
    spacing = mapping["native_spacing_m"]
    if not all(math.isfinite(v) for v in (x, z)):
        raise ValueError("nonfinite world position")
    return i + (z - oz) / spacing, j + (x - ox) / spacing


def site_plan(recipe: dict, site_id: str, center, *, spans=LEVEL_SPANS_M,
              camera_margin=CAMERA_MARGIN_M, sampling_margin=SAMPLING_MARGIN_M) -> dict:
    if (not site_id or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789-" for c in site_id)
            or not spans or tuple(sorted(set(spans))) != tuple(spans)
            or not all(math.isfinite(v) and v > 0 for v in spans)
            or not all(math.isfinite(v) and v >= 0 for v in (camera_margin, sampling_margin))):
        raise ValueError("invalid site plan")
    world = Box(**{k: tuple(v) for k, v in recipe["survey_bounds"].items()})
    requested = Box.centered(center, spans[-1], camera_margin + sampling_margin)
    admitted = world.contains(requested)
    return {"schema": SITE_SCHEMA, "world_id": recipe["world_id"], "site_id": site_id,
            "center_xz_m": list(center), "levels": [{"span_m": span,
            "bounds": asdict(Box.centered(center, span))} for span in spans],
            "camera_margin_m": camera_margin, "sampling_margin_m": sampling_margin,
            "required_bounds": asdict(requested), "admitted": admitted,
            "rejection_reason": None if admitted else "outer-backdrop-and-margins-outside-world",
            "water_capture": None}
