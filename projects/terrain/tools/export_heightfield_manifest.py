#!/usr/bin/env python3
"""Export a runtime heightfield manifest from a Cubey raster-study field."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import tempfile


STUDY_SCHEMA = "cubey.terrain.raster-study.v1"
HEIGHTFIELD_SCHEMA = "cubey.terrain.heightfield.v1"


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def heightfield_manifest(study: dict[str, object]) -> dict[str, object]:
    _require(study.get("schema") == STUDY_SCHEMA, "unsupported terrain study manifest schema")
    source = study.get("source")
    grid = study.get("grid")
    comparison = study.get("comparison")
    files = study.get("files")
    _require(isinstance(source, dict) and bool(source.get("id")), "missing terrain source id")
    _require(isinstance(grid, dict), "missing terrain grid")
    _require(isinstance(comparison, dict), "missing terrain comparison calibration")
    _require(isinstance(files, dict) and isinstance(files.get("elevation"), dict),
             "missing terrain elevation file")
    return {
        "schema": HEIGHTFIELD_SCHEMA,
        "source": source,
        "seed": study["seed"],
        "grid": grid,
        "height": {
            "offset_m": comparison["height_offset_m"],
            "scale": comparison["height_scale"],
            "relief_scale_m": comparison["target_relief_m"],
        },
        "files": {"elevation": files["elevation"]},
        "provenance": {
            "source_manifest": "manifest.json",
            "source_schema": STUDY_SCHEMA,
        },
    }


def export_study_field(field: Path) -> Path:
    field = field.resolve()
    study_path = field / "manifest.json"
    study = json.loads(study_path.read_text())
    output = field / "heightfield.json"
    document = heightfield_manifest(study)
    with tempfile.NamedTemporaryFile("w", dir=field, prefix="heightfield.", suffix=".tmp",
                                     delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(json.dumps(document, indent=2, sort_keys=True, allow_nan=False) + "\n")
    os.replace(temporary, output)
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("field", nargs="+", type=Path, help="raster-study field directory")
    args = parser.parse_args()
    for field in args.field:
        print(export_study_field(field))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
