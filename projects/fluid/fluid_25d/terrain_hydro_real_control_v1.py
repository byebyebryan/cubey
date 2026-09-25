#!/usr/bin/env -S uv run --python 3.12
# /// script
# requires-python = "==3.12.*"
# dependencies = [
#   "numpy==1.26.4",
#   "pillow==12.3.0",
#   "pyflwdir==0.5.12",
#   "pyproj==3.8.0",
#   "pyshp==3.1.6",
#   "rasterio==1.4.3",
#   "scipy==1.15.3",
# ]
# ///
"""Compare an offline D8 drainage reader with independent mapped NC streams."""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import time
from pathlib import Path

import numpy as np
import pyflwdir
import rasterio
from PIL import Image, ImageDraw
from pyproj import CRS
from rasterio.features import rasterize
from scipy import ndimage
import shapefile


ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / "outputs/fluid/terrain-hydro-read-real-control-20260924"
THRESHOLDS_KM2 = (0.25, 1.0, 4.0, 16.0)
SOURCE_PAGE = "https://grassbook.org/datasets/datasets-3rd-edition/"
ELEVATION_URL = "https://grass.osgeo.org/sampledata/north_carolina/nc_rast_geotiff.zip"
STREAM_URL = "https://grass.osgeo.org/sampledata/north_carolina/nc_shape.zip"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path, default=DATA)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    data_dir = args.data_dir.resolve()
    output = (args.output_dir or data_dir / "review").resolve()
    if output.exists():
        raise SystemExit(f"refusing to overwrite existing output: {output}")
    dem_path = data_dir / "inputs/elev_ned_30m.tif"
    stream_path = data_dir / "inputs/streams.shp"
    if not dem_path.is_file() or not stream_path.is_file():
        raise SystemExit("download the published NC GeoTIFF and SHAPE archives and extract the named control inputs first")
    start = time.monotonic()
    with rasterio.open(dem_path) as source:
        elevation = source.read(1).astype(np.float64)
        if not np.isfinite(elevation).all() or np.any(elevation == source.nodata):
            raise SystemExit("control DEM is not a finite full rectangular grid")
        transform = source.transform
        raster_crs = CRS.from_user_input(source.crs)
        sample_spacing_m = float(abs(transform.a))
        if sample_spacing_m != 30.0 or abs(transform.e) != sample_spacing_m:
            raise SystemExit("control DEM is not a square 30 m grid")
    vector_crs = CRS.from_wkt((data_dir / "inputs/streams.prj").read_text())
    if not vector_crs.equals(raster_crs):
        raise SystemExit("control stream and DEM coordinate systems differ")

    filled, directions = pyflwdir.dem.fill_depressions(elevation.copy(), outlets="edge", connectivity=8)
    flow = pyflwdir.from_array(directions, ftype="d8", transform=transform, latlon=False)
    contributing_cells = np.asarray(flow.upstream_area(unit="cell"), dtype=np.float64)
    contributing_km2 = contributing_cells * sample_spacing_m * sample_spacing_m / 1e6
    terminals = np.zeros(elevation.shape, dtype=bool)
    terminals.ravel()[flow.idxs_pit] = True
    boundary = np.zeros(elevation.shape, dtype=bool)
    boundary[[0, -1], :] = True
    boundary[:, [0, -1]] = True

    features = shapefile.Reader(str(stream_path))
    chosen = [item.shape.__geo_interface__ for item in features.iterShapeRecords() if item.record.as_dict().get("FTYPE") == "STREAM/RIVER"]
    if not chosen:
        raise SystemExit("no STREAM/RIVER features in published control shapefile")
    mapped = rasterize(((geometry, 1) for geometry in chosen), out_shape=elevation.shape, transform=transform, all_touched=True, dtype=np.uint8).astype(bool)
    near_mapped = ndimage.binary_dilation(mapped, iterations=2)

    comparisons = []
    for threshold in THRESHOLDS_KM2:
        derived = contributing_km2 >= threshold
        near_derived = ndimage.binary_dilation(derived, iterations=2)
        comparisons.append(
            {
                "threshold_km2": threshold,
                "derived_cells": int(np.count_nonzero(derived)),
                "mapped_stream_cells": int(np.count_nonzero(mapped)),
                "derived_near_mapped_fraction": float(np.count_nonzero(derived & near_mapped) / np.count_nonzero(derived)) if np.any(derived) else 0.0,
                "mapped_near_derived_fraction": float(np.count_nonzero(mapped & near_derived) / np.count_nonzero(mapped)) if np.any(mapped) else 0.0,
            }
        )

    slope_z, slope_x = np.gradient(elevation, sample_spacing_m)
    shade = np.clip((0.65 - 0.48 * slope_x + 0.38 * slope_z) / np.sqrt(1 + slope_x * slope_x + slope_z * slope_z), 0, 1)
    grey = (55 + 180 * shade).astype(np.uint8)
    rgb = np.stack((grey, grey, grey), axis=-1)
    derived = contributing_km2 >= 1.0
    rgb[mapped] = (244, 79, 107)
    rgb[derived] = (37, 193, 233)
    rgb[derived & near_mapped] = (250, 218, 79)
    scale = 3
    picture = Image.fromarray(rgb, mode="RGB").resize((rgb.shape[1] * scale, rgb.shape[0] * scale), Image.Resampling.NEAREST)
    sheet = Image.new("RGB", (picture.width, picture.height + 58), "#202830")
    sheet.paste(picture, (0, 58))
    draw = ImageDraw.Draw(sheet)
    draw.text((12, 8), "North Carolina real-DEM control | 30 m NED + published stream/river lines", fill="white")
    draw.text((12, 30), "Red: mapped line  |  Cyan: >=1 km2 contributing area  |  Yellow: derived line within 2 cells of mapped line", fill="white")

    output.mkdir(parents=True)
    sheet.save(output / "real-control-overlay.png")
    report = {
        "schema": "cubey.fluid25d.terrain_hydro_real_control.v1",
        "status": "independent mapped-stream sanity control, not field-flow validation",
        "source": {
            "page": SOURCE_PAGE,
            "elevation_archive_url": ELEVATION_URL,
            "streams_archive_url": STREAM_URL,
            "elevation_archive_sha256": sha256(data_dir / "nc_rast_geotiff.zip"),
            "streams_archive_sha256": sha256(data_dir / "nc_shape.zip"),
            "elevation_sha256": sha256(dem_path),
            "streams_sha256": sha256(stream_path),
            "grid_shape_zx": list(elevation.shape),
            "sample_spacing_m": sample_spacing_m,
            "crs": raster_crs.to_string(),
            "stream_filter": "FTYPE = STREAM/RIVER",
        },
        "method": {
            "routing": "pyflwdir 0.5.12 priority flood, edge outlets, 8-connected D8, unit runoff",
            "mapped_line_rasterization": "all_touched; 2-cell proximity allowance",
            "tool_versions": {name: importlib.metadata.version(name) for name in ("pyflwdir", "numpy", "rasterio", "pyproj", "pyshp", "scipy", "pillow")},
            "script_sha256": sha256(Path(__file__)),
        },
        "routing_checks": {
            "interior_terminal_count": int(np.count_nonzero(terminals & ~boundary)),
            "terminal_area_balance_relative": float((contributing_cells[terminals].sum() - elevation.size) / elevation.size),
            "fill_volume_m3": float(np.maximum(filled - elevation, 0).sum() * sample_spacing_m * sample_spacing_m),
        },
        "comparisons": comparisons,
        "limitations": [
            "Mapped stream lines are an independent positive control, but line alignment and headwater completeness are not exact at one-cell resolution.",
            "Artificial paths, connectors, canals and ditches were excluded from this control comparison.",
            "Contributing area is not discharge or a permanent water mask.",
        ],
        "elapsed_seconds": time.monotonic() - start,
        "overlay_sha256": sha256(output / "real-control-overlay.png"),
    }
    (output / "result.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"output": str(output), "routing_checks": report["routing_checks"], "comparisons": comparisons}, indent=2))


if __name__ == "__main__":
    main()
