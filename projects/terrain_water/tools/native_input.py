"""Maintained native input serialization and identity audit, extracted unchanged
from the retained runoff study. Calls released SynxFlow IO only when requested;
never dispatches a hydraulic solver.
"""
from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Any
import numpy as np

NODATA = -9999.0

class RunoffError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def read_ascii(path: Path) -> tuple[dict[str, float], np.ndarray, np.ndarray]:
    """Read a six-line ESRI ASCII raster without flipping its stored row order."""
    lines = path.read_text(encoding="ascii").splitlines()
    if len(lines) < 7:
        raise RunoffError(f"ASCII raster is truncated: {path}")
    header: dict[str, float] = {}
    for line in lines[:6]:
        pieces = line.split()
        if len(pieces) != 2:
            raise RunoffError(f"invalid ASCII header line in {path}: {line!r}")
        header[pieces[0].lower()] = float(pieces[1])
    required = {"ncols", "nrows", "xllcorner", "yllcorner", "cellsize", "nodata_value"}
    if set(header) != required:
        raise RunoffError(f"unexpected ASCII header keys in {path}: {sorted(header)}")
    rows = int(header["nrows"])
    cols = int(header["ncols"])
    values = np.fromstring(" ".join(lines[6:]), sep=" ", dtype=np.float64)
    if values.size != rows * cols:
        raise RunoffError(f"ASCII shape mismatch in {path}: expected {rows * cols}, got {values.size}")
    data = values.reshape((rows, cols))
    valid = data != header["nodata_value"]
    if not valid.any():
        raise RunoffError(f"ASCII raster has no valid cells: {path}")
    return header, data, valid


def write_ascii(path: Path, data: np.ndarray, cellsize_m: float, nodata: float = NODATA) -> None:
    array = np.asarray(data, dtype=np.float64)
    if array.ndim != 2 or not np.isfinite(array).all() or cellsize_m <= 0:
        raise ValueError("ASCII raster input must be a finite 2D array and positive cell size")
    rows, cols = array.shape
    with path.open("w", encoding="ascii", newline="\n") as stream:
        stream.write(f"ncols {cols}\n")
        stream.write(f"nrows {rows}\n")
        stream.write("xllcorner 0.000000\n")
        stream.write("yllcorner 0.000000\n")
        stream.write(f"cellsize {cellsize_m:.6f}\n")
        stream.write(f"NODATA_value {nodata:.6f}\n")
        for row in array:
            stream.write(" ".join(f"{value:.9f}" for value in row) + "\n")


def _input_hashes(input_dir: Path) -> dict[str, str]:
    return {
        path.relative_to(input_dir).as_posix(): sha256_file(path)
        for path in sorted(input_dir.rglob("*"))
        if path.is_file()
    }


def _element_records(path: Path) -> tuple[np.ndarray, np.ndarray]:
    """Read released cell-ID rows and all serialized scalar/vector columns."""
    lines = path.read_text(encoding="ascii").splitlines()
    marker = next((index for index, line in enumerate(lines) if line.startswith("$Element_id")), None)
    if marker is None:
        raise RunoffError(f"missing element-value marker: {path}")
    element_ids = []
    values = []
    for line in lines[marker + 1:]:
        if line.startswith("$"):
            break
        pieces = line.split()
        if len(pieces) >= 2:
            element_ids.append(int(pieces[0]))
            values.append([float(value) for value in pieces[1:]])
    if not values:
        raise RunoffError(f"empty element-value section: {path}")
    widths = {len(row) for row in values}
    if len(widths) != 1:
        raise RunoffError(f"inconsistent number of field values in {path}")
    return np.asarray(element_ids, dtype=np.int64), np.asarray(values, dtype=np.float64)


def _element_pairs(path: Path) -> tuple[np.ndarray, np.ndarray]:
    ids, values = _element_records(path)
    if values.shape[1] != 1:
        raise RunoffError(f"expected a single-value field in {path}, found {values.shape[1]}")
    return ids, values[:, 0]


def _element_values(path: Path) -> np.ndarray:
    """Return values from a released `Element_id Value` field file."""
    return _element_pairs(path)[1]


def _grid_from_native_ids(path: Path, rows: int, cols: int) -> tuple[np.ndarray, np.ndarray]:
    """Map native IDs back to ASC row order using released bottom-up IDs."""
    element_ids, serialized_values = _element_pairs(path)
    expected = rows * cols
    if element_ids.size != expected or not np.array_equal(np.sort(element_ids), np.arange(expected)):
        raise RunoffError(f"native field IDs do not cover the complete raster in {path}")
    serialized_grid = np.empty((rows, cols), dtype=np.float64)
    for element_id, value in zip(element_ids, serialized_values):
        row_from_bottom, col = divmod(int(element_id), cols)
        row = rows - 1 - row_from_bottom
        serialized_grid[row, col] = value
    return serialized_grid, serialized_grid.astype("<f4")


def _native_input_audit(spec: dict[str, Any], case_dir: Path) -> dict[str, Any]:
    native_input = case_dir / "native" / "input"
    field = native_input / "field"
    rows, cols = int(spec["rows"]), int(spec["cols"])
    _, source_bed, source_valid = read_ascii(case_dir / "DEM.asc")
    if source_bed.shape != (rows, cols) or not source_valid.all():
        raise RunoffError("native input audit found an unexpected DEM shape or NODATA")
    serialized_bed, native_float_bed = _grid_from_native_ids(field / "z.dat", rows, cols)
    difference = serialized_bed - source_bed
    float_difference = native_float_bed.astype(np.float64) - source_bed
    mesh_header, mesh_bed, mesh_valid = read_ascii(native_input / "mesh" / "DEM.txt")
    if mesh_bed.shape != (rows, cols) or not mesh_valid.all():
        raise RunoffError("native mesh DEM shape or validity differs from source raster")
    boundary_h = _serialized_boundary_codes(field / "h.dat")
    boundary_hu = _serialized_boundary_codes(field / "hU.dat")
    expected_boundary = (3, 0, 0) if spec["boundary"] == "fall" else (2, 0, 0)
    expected_hu = (3, 0, 0) if spec["boundary"] == "fall" else (2, 2, 0)
    if not boundary_h or not boundary_hu or any(code != expected_boundary for code in boundary_h) or any(code != expected_hu for code in boundary_hu):
        raise RunoffError(f"serialized perimeter boundary is not the declared {spec['boundary']} mapping")
    manning_values = _element_values(field / "manning.dat")
    if not np.allclose(manning_values, float(spec["manning_n"]), rtol=0.0, atol=1e-7):
        raise RunoffError("serialized native Manning field differs from the declared uniform value")
    sink_names = ("sewer_sink", "cumulative_depth", "hydraulic_conductivity", "capillary_head", "water_content_diff")
    sink_values = {name: _element_values(field / f"{name}.dat") for name in sink_names}
    if any(np.any(values != 0.0) for values in sink_values.values()):
        raise RunoffError("serialized native sink/source parameter is nonzero")
    rain_mask_values = _element_values(field / "precipitation_mask.dat")
    if np.any(rain_mask_values != 0.0):
        raise RunoffError("uniform-rain control unexpectedly serialized a masked-out precipitation cell")
    rain_source = np.loadtxt(field / "precipitation_source_all.dat", skiprows=1, ndmin=2, dtype=np.float64)
    expected_rain = float(spec["rain_rate_m_per_s"])
    rain_end = float(spec.get("rain_history_end_s", spec["duration_s"]))
    if (rain_source.shape != (2, 2)
            or not np.allclose(rain_source[:, 0], [0.0, rain_end], rtol=0.0, atol=1e-8)
            or not np.allclose(rain_source[:, 1], expected_rain, rtol=2e-6, atol=1e-14)):
        raise RunoffError("serialized continuous-rain schedule differs from the frozen case rate/horizon")
    h_ids, h_values = _element_records(field / "h.dat")
    hu_ids, hu_values = _element_records(field / "hU.dat")
    expected_initial = float(spec["initial_depth_m"])
    if (h_values.shape != (rows * cols, 1) or not np.all(h_values[:, 0] == expected_initial)
            or not np.array_equal(hu_ids, h_ids) or hu_values.shape != (rows * cols, 2)
            or not np.all(hu_values == 0.0)):
        raise RunoffError("serialized native initial h/hU differs from the declared zero-momentum state")
    runtime = (native_input / "times_setup.dat").read_text(encoding="ascii").split()
    if runtime != ["0", str(int(spec["duration_s"])), str(int(spec["output_interval_s"])), str(int(spec["duration_s"]))]:
        raise RunoffError(f"serialized native runtime/export schedule mismatch: {runtime}")
    return {
        "native_id_mapping": "released InputModel indep_functions._get_cell_id_array: IDs are bottom row first, each row left-to-right; map id//ncols to row=nrows-1-row_from_bottom",
        "z_field_path": str((field / "z.dat").resolve()),
        "z_field_sha256": sha256_file(field / "z.dat"),
        "z_element_count": int(serialized_bed.size),
        "serialized_z_matches_field_ids": True,
        "serialized_z_uses_six_significant_digit_%g": True,
        "serialized_z_max_abs_error_vs_9_decimal_input_dem_m": float(np.max(np.abs(difference))),
        "serialized_z_rms_error_vs_9_decimal_input_dem_m": float(np.sqrt(np.mean(difference * difference))),
        "native_scalar_dtype": "float32 (per official 1.0.1 sdist Scalar.h; wheel extension hash recorded separately)",
        "native_float32_z_max_abs_error_vs_9_decimal_input_dem_m": float(np.max(np.abs(float_difference))),
        "native_float32_z_rms_error_vs_9_decimal_input_dem_m": float(np.sqrt(np.mean(float_difference * float_difference))),
        "native_float32_bed_grid_row_order": "restored to input ASC order before ROI/pond analysis",
        "mesh_dem_header": mesh_header,
        "mesh_dem_max_abs_error_vs_source_m": float(np.max(np.abs(mesh_bed - source_bed))),
        "serialized_boundary_h_counts": {str(code): boundary_h.count(code) for code in set(boundary_h)},
        "serialized_boundary_hU_counts": {str(code): boundary_hu.count(code) for code in set(boundary_hu)},
        "declared_boundary": spec["boundary"],
        "serialized_manning_unique_values": np.unique(manning_values).tolist(),
        "serialized_sink_unique_values": {name: np.unique(values).tolist() for name, values in sink_values.items()},
        "serialized_rain_mask_unique_values": np.unique(rain_mask_values).tolist(),
        "serialized_continuous_rain_source_rows": rain_source.tolist(),
        "serialized_initial_h_unique_values": np.unique(h_values[:, 0]).tolist(),
        "serialized_initial_hU_max_abs_m2_per_s": float(np.max(np.abs(hu_values))),
        "serialized_runtime_values": runtime,
        "native_z_values_for_pond_bed_plus_depth": "field/z.dat values mapped by released Element_id ordering and cast to native float32; mesh DEM.txt is retained separately and is not treated as native z precision",
    }


def _serialized_boundary_codes(path: Path) -> list[tuple[int, int, int]]:
    lines = path.read_text(encoding="ascii").splitlines()
    try:
        marker = lines.index("$Boundary Numbers")
    except ValueError as error:
        raise RunoffError(f"missing serialized boundary section: {path}") from error
    count = int(lines[marker + 1])
    codes = []
    # The released writer emits a second `$Element_id Value` heading inside
    # the boundary section before the boundary records themselves.
    start = marker + 3
    for line in lines[start:start + count]:
        pieces = line.split()
        if len(pieces) != 4:
            raise RunoffError(f"malformed serialized boundary code in {path}: {line!r}")
        codes.append(tuple(int(value) for value in pieces[1:]))
    if len(codes) != count:
        raise RunoffError(f"serialized boundary count mismatch in {path}")
    return codes


def _make_model_inputs(spec: dict[str, Any], case_dir: Path, input_model_type: Any = None) -> dict[str, str]:
    """Use released InputModel APIs; this path does not launch the GPU."""
    if input_model_type is None:
        from synxflow.IO.InputModel import InputModel as input_model_type

    dem_path = case_dir / "DEM.asc"
    native_dir = case_dir / "native"
    if not dem_path.is_file():
        raise FileNotFoundError(f"case DEM is missing: {dem_path}")
    if native_dir.exists():
        raise FileExistsError(f"refusing to overwrite native case folder: {native_dir}")
    rows, cols = int(spec["rows"]), int(spec["cols"])
    model = input_model_type(dem_data=str(dem_path), case_folder=str(native_dir))
    model.set_initial_condition("h0", np.full((rows, cols), float(spec["initial_depth_m"]), dtype=np.float32))
    model.set_initial_condition("hU0x", np.zeros((rows, cols), dtype=np.float32))
    model.set_initial_condition("hU0y", np.zeros((rows, cols), dtype=np.float32))
    boundary = str(spec.get("boundary", "rigid"))
    model.set_boundary_condition([], outline_boundary=boundary)
    duration = float(spec["duration_s"])
    rain_end = float(spec.get("rain_history_end_s", duration))
    rate = float(spec["rain_rate_m_per_s"])
    model.set_rainfall(rain_mask=0, rain_source=np.asarray([[0.0, rate], [rain_end, rate]], dtype=np.float64))
    model.set_grid_parameter(
        manning=float(spec["manning_n"]),
        sewer_sink=0.0,
        cumulative_depth=0.0,
        hydraulic_conductivity=0.0,
        capillary_head=0.0,
        water_content_diff=0.0,
    )
    model.set_runtime([0.0, duration, float(spec["output_interval_s"]), duration])
    model.set_device_no([0])
    model.write_input_files()
    return _input_hashes(native_dir / "input")
