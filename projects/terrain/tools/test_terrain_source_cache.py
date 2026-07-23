#!/usr/bin/env python3
"""Dependency-free tests for worktree terrain source-cache validation."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest


TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

import terrain_diffusion_bake as bake


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def pinned_source() -> dict[str, object]:
    return {
        "id": "terrain-diffusion-30m",
        "generator": "terrain-diffusion",
        "code_revision": bake.CODE_REVISION,
        "model_id": bake.MODEL_ID,
        "model_revision": bake.MODEL_REVISION,
    }


def grid() -> dict[str, object]:
    return {
        "width": 2,
        "height": 2,
        "sample_spacing_m": 30.0,
        "sample_origin_x_m": -15.0,
        "sample_origin_z_m": -15.0,
        "axis_mapping": {"world_x": "model_j", "world_z": "model_i"},
    }


def write_heightfield(root: Path, payload: bytes) -> str:
    payload_hash = sha256(payload)
    (root / "elevation.f32").write_bytes(payload)
    manifest = {
        "schema": "cubey.terrain.heightfield.v1",
        "source": pinned_source(),
        "seed": 0,
        "grid": grid(),
        "height": {"offset_m": 0.0, "scale": 1.0, "relief_scale_m": 1_000.0},
        "files": {
            "elevation": {
                "path": "elevation.f32",
                "dtype": "float32-le",
                "layout": "row-major-zx",
                "shape": [2, 2],
                "byte_count": len(payload),
                "sha256": payload_hash,
                "unit": "m",
            }
        },
    }
    (root / "heightfield.json").write_text(json.dumps(manifest))
    return payload_hash


def write_surface_fields(root: Path, payload: bytes, elevation_sha256: str) -> str:
    payload_hash = sha256(payload)
    (root / "climate.f32").write_bytes(payload)
    manifest = {
        "schema": bake.SURFACE_STUDY_SCHEMA,
        "source": pinned_source(),
        "seed": 0,
        "heightfield": {
            "schema": "cubey.terrain.heightfield.v1",
            "elevation_sha256": elevation_sha256,
        },
        "grid": grid(),
        "files": {
            "climate": {
                "path": "climate.f32",
                "dtype": "float32-le",
                "layout": "channel-major-zx",
                "shape": [4, 2, 2],
                "byte_count": len(payload),
                "sha256": payload_hash,
                "channels": [
                    {"name": name, "unit": unit} for name, unit in bake.CLIMATE_CHANNELS
                ],
            }
        },
    }
    (root / "surface-fields.json").write_text(json.dumps(manifest))
    return payload_hash


class TerrainSourceCacheTests(unittest.TestCase):
    def test_default_reuse_requires_the_complete_runtime_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            payload = struct.pack("<4f", 100.0, 200.0, 300.0, 400.0)
            payload_hash = write_heightfield(root, payload)
            original_hash = bake.DEFAULT_ASSET_ELEVATION_SHA256
            bake.DEFAULT_ASSET_ELEVATION_SHA256 = payload_hash
            try:
                self.assertTrue(bake.validate_existing_asset("default", root))

                manifest_path = root / "heightfield.json"
                manifest = json.loads(manifest_path.read_text())
                del manifest["grid"]
                manifest_path.write_text(json.dumps(manifest))
                self.assertFalse(bake.validate_existing_asset("default", root))

                write_heightfield(root, payload)
                (root / "elevation.f32").write_bytes(
                    struct.pack("<4f", 100.0, 200.0, 300.0, 401.0)
                )
                self.assertFalse(bake.validate_existing_asset("default", root))
            finally:
                bake.DEFAULT_ASSET_ELEVATION_SHA256 = original_hash

    def test_surface_reuse_validates_channels_and_physical_ranges(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            elevation_sha256 = "a" * 64
            channels = (
                [12.0, 13.0, 14.0, 15.0]
                + [4.0, 5.0, 6.0, 7.0]
                + [400.0, 500.0, 600.0, 700.0]
                + [0.2, 0.3, 0.4, 0.5]
            )
            payload = struct.pack("<16f", *channels)
            payload_hash = write_surface_fields(root, payload, elevation_sha256)
            original_elevation_hash = bake.DEFAULT_ASSET_ELEVATION_SHA256
            original_climate_hash = bake.DEFAULT_ASSET_CLIMATE_SHA256
            bake.DEFAULT_ASSET_ELEVATION_SHA256 = elevation_sha256
            bake.DEFAULT_ASSET_CLIMATE_SHA256 = payload_hash
            try:
                self.assertTrue(bake.validate_existing_asset("surface-study", root))

                manifest_path = root / "surface-fields.json"
                manifest = json.loads(manifest_path.read_text())
                manifest["files"]["climate"]["channels"][0]["unit"] = "native"
                manifest_path.write_text(json.dumps(manifest))
                self.assertFalse(bake.validate_existing_asset("surface-study", root))

                channels[-1] = 1.5
                invalid_payload = struct.pack("<16f", *channels)
                invalid_hash = write_surface_fields(root, invalid_payload, elevation_sha256)
                bake.DEFAULT_ASSET_CLIMATE_SHA256 = invalid_hash
                self.assertFalse(bake.validate_existing_asset("surface-study", root))
            finally:
                bake.DEFAULT_ASSET_ELEVATION_SHA256 = original_elevation_hash
                bake.DEFAULT_ASSET_CLIMATE_SHA256 = original_climate_hash


if __name__ == "__main__":
    unittest.main()
