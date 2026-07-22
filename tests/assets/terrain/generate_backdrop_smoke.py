#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


SCHEMA = "cubey.terrain.heightfield.v1"
SIZE = 257
SPACING_M = 256.0
ORIGIN_M = -32_768.0


def height_at(x_m: float) -> float:
    return min(max((x_m - 2_000.0) * 0.18, 0.0), 1_800.0)


def generate(output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    payload = bytearray()
    for _z in range(SIZE):
        for x in range(SIZE):
            payload.extend(struct.pack("<f", height_at(ORIGIN_M + x * SPACING_M)))

    elevation_path = output_dir / "elevation.f32"
    elevation_path.write_bytes(payload)
    manifest = {
        "schema": SCHEMA,
        "source": {
            "id": "cubey-backdrop-smoke-directional-rise",
            "generator": "tests/assets/terrain/generate_backdrop_smoke.py",
        },
        "provenance": {
            "purpose": "deterministic terrain and glTF backdrop integration smoke",
            "selection": "broad one-sided rise around the source bounds center",
        },
        "seed": 7,
        "grid": {
            "width": SIZE,
            "height": SIZE,
            "sample_spacing_m": SPACING_M,
            "sample_origin_x_m": ORIGIN_M,
            "sample_origin_z_m": ORIGIN_M,
        },
        "height": {
            "offset_m": 0.0,
            "scale": 1.0,
            "relief_scale_m": 1_800.0,
        },
        "files": {
            "elevation": {
                "path": elevation_path.name,
                "dtype": "float32-le",
                "layout": "row-major-zx",
                "shape": [SIZE, SIZE],
                "byte_count": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        },
    }
    (output_dir / "heightfield.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "output_dir",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parent / "backdrop-smoke",
    )
    generate(parser.parse_args().output_dir)


if __name__ == "__main__":
    main()
