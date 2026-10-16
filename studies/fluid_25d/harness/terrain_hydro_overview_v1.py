#!/usr/bin/env -S uv run --python 3.12
# /// script
# requires-python = "==3.12.*"
# dependencies = ["pillow==12.3.0"]
# ///
"""Build a labeled corpus review sheet from frozen per-map hydro captures."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_CORPUS = ROOT / "outputs/fluid/terrain-hydro-read-v1-corpus-visible-20260924"
TILE_WIDTH = 760
TILE_HEIGHT = 415
PANEL_SIZE = 368


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus-dir", type=Path, default=DEFAULT_CORPUS)
    args = parser.parse_args()
    corpus = args.corpus_dir.resolve()
    summary = json.loads((corpus / "summary.json").read_text())
    if summary.get("schema") != "cubey.fluid25d.terrain_hydro_corpus.v1" or summary.get("case_count") != 12:
        raise SystemExit("expected the pinned 12-map corpus summary")
    output = corpus / "corpus-review-overview.png"
    if output.exists():
        raise SystemExit(f"refusing to overwrite existing overview: {output}")
    cases = summary["cases"]
    sheet = Image.new("RGB", (TILE_WIDTH * 3, TILE_HEIGHT * 4 + 60), "#202830")
    draw = ImageDraw.Draw(sheet)
    draw.text((12, 10), "Pinned 30 m Terrain Diffusion corpus | connected D8 drainage (left) vs derived basin fill (right)", fill="white")
    draw.text((12, 32), "Blue is contributing area, not water; orange/red is analytical fill, not a known lake. Shared display scales on every map.", fill="#cddde6")
    for index, case in enumerate(cases):
        x = (index % 3) * TILE_WIDTH
        y = (index // 3) * TILE_HEIGHT + 60
        case_dir = corpus / case["case"]
        for column, filename in enumerate(("connected-d8-contributing-area.png", "derived-fill-depth-basin-proxy.png")):
            with Image.open(case_dir / filename) as source:
                picture = source.convert("RGB").resize((PANEL_SIZE, PANEL_SIZE), Image.Resampling.LANCZOS)
            sheet.paste(picture, (x + 8 + column * (PANEL_SIZE + 8), y + 26))
        label = case["variant"] or "default"
        draw.text((x + 10, y + 5), f"{index + 1:02d}  {label}  seed {case['seed']}  [{case['elevation_sha256'][:8]}]", fill="white")
        draw.text((x + 10, y + 395), f"Max drainage {case['d8_max_contributing_area_km2']:.0f} km2  |  fill {case['fill_fraction']:.1%}  |  interior sinks {case['d8_interior_terminal_count']}", fill="#cddde6")
    sheet.save(output)
    print(output)


if __name__ == "__main__":
    main()
