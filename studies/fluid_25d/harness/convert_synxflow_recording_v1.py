#!/usr/bin/env python3
"""Convert one frozen SynxFlow native case into a portable Cubey recording.

This adapter only reads saved ASC states and native input files. It does not
import SynxFlow, NumPy, CUDA, or the case runner.
"""

from __future__ import annotations

import argparse
from array import array
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys
from typing import Any


SCHEMA = "cubey.fluid25d.recording.v1"
ENCODING = "float32-little-endian"
FIELDS = ["h_m", "qx_m2_per_s", "qz_m2_per_s"]
MAX_CELLS = 4_194_304
MAX_FRAMES = 100_000
ASC_HEADER_KEYS = (
    "ncols",
    "nrows",
    "xllcorner",
    "yllcorner",
    "cellsize",
    "nodata_value",
)
RAIN_HISTORY_REL_TOL = 2.0e-8
RAIN_HISTORY_ABS_TOL = 1.0e-14


class RecordingConversionError(ValueError):
    """Input does not satisfy the frozen recording adapter contract."""


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _read_bytes(path: Path, label: str) -> bytes:
    try:
        return path.read_bytes()
    except OSError as error:
        raise RecordingConversionError(f"cannot read {label}: {path}: {error}") from error


def _read_json(path: Path, label: str) -> tuple[dict[str, Any], bytes]:
    raw = _read_bytes(path, label)
    try:
        document = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RecordingConversionError(f"invalid {label}: {path}: {error}") from error
    if not isinstance(document, dict):
        raise RecordingConversionError(f"{label} must contain a JSON object")
    return document, raw


def _finite_number(value: Any, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise RecordingConversionError(f"{label} must be numeric")
    result = float(value)
    if not math.isfinite(result):
        raise RecordingConversionError(f"{label} must be finite")
    return result


def _positive_integer(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise RecordingConversionError(f"{label} must be a positive integer")
    return value


def _read_ascii_grid(path: Path, label: str) -> tuple[dict[str, float | int], list[float], str]:
    raw = _read_bytes(path, label)
    try:
        lines = raw.decode("ascii").splitlines()
    except UnicodeDecodeError as error:
        raise RecordingConversionError(f"{label} must be ASCII: {path}") from error
    if len(lines) < len(ASC_HEADER_KEYS):
        raise RecordingConversionError(f"{label} has an incomplete ASCII grid header")

    header: dict[str, float | int] = {}
    for index, key in enumerate(ASC_HEADER_KEYS):
        fields = lines[index].split()
        if len(fields) != 2 or fields[0].lower() != key:
            raise RecordingConversionError(f"{label} has an invalid {key} header")
        try:
            number = float(fields[1])
        except ValueError as error:
            raise RecordingConversionError(f"{label} has invalid {key} metadata") from error
        if not math.isfinite(number):
            raise RecordingConversionError(f"{label} has non-finite {key} metadata")
        if key in ("ncols", "nrows"):
            if number <= 0 or not number.is_integer():
                raise RecordingConversionError(f"{label} {key} must be a positive integer")
            header[key] = int(number)
        else:
            header[key] = number

    width = int(header["ncols"])
    height = int(header["nrows"])
    if width < 2 or height < 2:
        raise RecordingConversionError(f"{label} grid dimensions must both be at least two")
    if width * height > MAX_CELLS:
        raise RecordingConversionError(f"{label} grid exceeds the supported cell limit")
    values: list[float] = []
    expected_count = width * height
    nodata = float(header["nodata_value"])
    for line in lines[len(ASC_HEADER_KEYS) :]:
        for token in line.split():
            try:
                value = float(token)
            except ValueError as error:
                raise RecordingConversionError(f"{label} contains an invalid cell value") from error
            if not math.isfinite(value):
                raise RecordingConversionError(f"{label} contains a non-finite cell")
            if value == nodata:
                raise RecordingConversionError(f"{label} contains a NODATA cell")
            values.append(value)
            if len(values) > expected_count:
                raise RecordingConversionError(f"{label} contains too many cell values")
    if len(values) != expected_count:
        raise RecordingConversionError(
            f"{label} has {len(values)} cells; expected {expected_count}"
        )
    if float(header["cellsize"]) <= 0:
        raise RecordingConversionError(f"{label} cellsize must be positive")
    return header, values, _sha256_bytes(raw)


def _same_geometry(
    left: dict[str, float | int], right: dict[str, float | int], label: str
) -> None:
    if int(left["ncols"]) != int(right["ncols"]) or int(left["nrows"]) != int(
        right["nrows"]
    ):
        raise RecordingConversionError(f"{label} dimensions do not match")
    for key in ("xllcorner", "yllcorner", "cellsize"):
        if not math.isclose(
            float(left[key]), float(right[key]), rel_tol=0.0, abs_tol=1.0e-9
        ):
            raise RecordingConversionError(f"{label} {key} does not match")


def _as_float32(values: list[float], label: str) -> array:
    if array("f").itemsize != 4:
        raise RecordingConversionError("this Python platform does not provide IEEE float32")
    try:
        converted = array("f", values)
    except (OverflowError, ValueError) as error:
        raise RecordingConversionError(f"{label} contains a value outside float32 range") from error
    for source, stored in zip(values, converted, strict=True):
        if not math.isfinite(stored):
            raise RecordingConversionError(f"{label} contains a non-finite float32 value")
        if source != 0.0 and stored == 0.0:
            raise RecordingConversionError(f"{label} underflows float32 representation")
    return converted


def _little_endian_bytes(values: array) -> bytes:
    encoded = array("f", values)
    if sys.byteorder != "little":
        encoded.byteswap()
    return encoded.tobytes()


def _write_float32(path: Path, values: array) -> str:
    payload = _little_endian_bytes(values)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    return _sha256_bytes(payload)


def _read_native_bed(path: Path, width: int, height: int) -> tuple[array, str]:
    raw = _read_bytes(path, "native numerical bed")
    try:
        lines = raw.decode("ascii").splitlines()
    except UnicodeDecodeError as error:
        raise RecordingConversionError("native numerical bed must be ASCII") from error
    if len(lines) < 3 or lines[0].strip() != "$Element Number":
        raise RecordingConversionError("native numerical bed has an invalid header")
    count = width * height
    if lines[1].strip() != str(count) or lines[2].strip() != "$Element_id  Value":
        raise RecordingConversionError("native numerical bed shape/header does not match grid")

    row_major = [0.0] * count
    boundary_marker = 3 + count
    if len(lines) < boundary_marker + 3 or lines[boundary_marker].strip() != "$Boundary Numbers":
        raise RecordingConversionError("native numerical bed omits its boundary section")
    try:
        boundary_count = int(lines[boundary_marker + 1].strip(), 10)
    except ValueError as error:
        raise RecordingConversionError("native numerical bed has an invalid boundary count") from error
    expected_boundary_count = 2 * width + 2 * height - 4
    if boundary_count != expected_boundary_count or lines[boundary_marker + 2].strip() != "$Element_id  Value":
        raise RecordingConversionError("native numerical bed boundary count/header is invalid")

    data_lines = lines[3:boundary_marker]
    if len(data_lines) != count:
        raise RecordingConversionError("native numerical bed element count does not match grid")
    for expected_id, line in enumerate(data_lines):
        fields = line.split()
        if len(fields) != 2:
            raise RecordingConversionError("native numerical bed has a malformed element row")
        try:
            element_id = int(fields[0], 10)
            value = float(fields[1])
        except ValueError as error:
            raise RecordingConversionError("native numerical bed has invalid element data") from error
        if element_id != expected_id:
            raise RecordingConversionError("native numerical bed IDs must be sequential from zero")
        if not math.isfinite(value):
            raise RecordingConversionError("native numerical bed contains a non-finite value")
        row_from_bottom, col = divmod(element_id, width)
        row = height - 1 - row_from_bottom
        row_major[row * width + col] = value

    boundary_lines = lines[boundary_marker + 3 :]
    if len(boundary_lines) != boundary_count:
        raise RecordingConversionError("native numerical bed boundary records do not match count")
    boundary_ids: set[int] = set()
    for line in boundary_lines:
        fields = line.split()
        if len(fields) != 4:
            raise RecordingConversionError("native numerical bed has a malformed boundary row")
        try:
            element_id, boundary_type, zero_a, zero_b = (int(field, 10) for field in fields)
        except ValueError as error:
            raise RecordingConversionError(
                "native numerical bed boundary values must be integers"
            ) from error
        if not 0 <= element_id < count or element_id in boundary_ids:
            raise RecordingConversionError("native numerical bed boundary IDs are invalid or repeated")
        row_from_bottom, col = divmod(element_id, width)
        if not (
            row_from_bottom == 0
            or row_from_bottom == height - 1
            or col == 0
            or col == width - 1
        ):
            raise RecordingConversionError("native numerical bed boundary ID is not on the perimeter")
        if (boundary_type, zero_a, zero_b) != (2, 0, 0):
            raise RecordingConversionError("native numerical bed has an unsupported boundary record")
        boundary_ids.add(element_id)
    if len(boundary_ids) != expected_boundary_count:
        raise RecordingConversionError("native numerical bed does not list every perimeter cell")
    return _as_float32(row_major, "native numerical bed"), _sha256_bytes(raw)


def _terrain_identity(case_protocol: dict[str, Any]) -> dict[str, Any]:
    source = case_protocol.get("terrain_source_identity")
    if not isinstance(source, dict):
        raise RecordingConversionError("case protocol has no terrain source identity")
    identity_keys = (
        "crop_cell_size_m",
        "crop_xzwh",
        "elevation_dtype",
        "elevation_sha256",
        "elevation_shape",
        "fixture_sha256",
        "manifest_sha256",
        "physical_extent_m",
        "row_orientation",
        "transform",
        "transformed_crop_dtype",
        "transformed_crop_sha256",
        "transformed_crop_shape",
        "transformed_halo_sha256",
        "transformed_halo_shape",
    )
    missing = [key for key in identity_keys if key not in source]
    if missing:
        raise RecordingConversionError(
            "case protocol terrain identity is missing: " + ", ".join(missing)
        )
    identity = {key: source[key] for key in identity_keys}
    for key, value in identity.items():
        if isinstance(value, str) and (value.startswith("/") or ".." in Path(value).parts):
            raise RecordingConversionError(f"terrain identity {key} contains a path")
    return identity


def _parse_rainfall_history(value: Any, duration_s: float, initial_rate_m_per_s: float,
                            label: str) -> list[list[float]]:
    if not isinstance(value, list) or len(value) < 2:
        raise RecordingConversionError(f"{label} must contain at least two time/rate pairs")
    history: list[list[float]] = []
    for index, entry in enumerate(value):
        if not isinstance(entry, list) or len(entry) != 2:
            raise RecordingConversionError(f"{label}[{index}] must be a [time_s, rate_m_per_s] pair")
        timestamp = _finite_number(entry[0], f"{label}[{index}] time_s")
        rate = _finite_number(entry[1], f"{label}[{index}] rate_m_per_s")
        if timestamp < 0.0 or rate < 0.0 or not math.isfinite(rate * 3_600_000.0):
            raise RecordingConversionError(f"{label} times and rates must be nonnegative")
        if index == 0 and timestamp != 0.0:
            raise RecordingConversionError(f"{label} must start at time zero")
        if index > 0 and timestamp <= history[-1][0]:
            raise RecordingConversionError(f"{label} timestamps must be strictly increasing")
        history.append([timestamp, rate])
    if history[-1][0] != duration_s:
        raise RecordingConversionError(f"{label} must end exactly at case duration")
    if not math.isclose(history[0][1], initial_rate_m_per_s,
                        rel_tol=RAIN_HISTORY_REL_TOL, abs_tol=RAIN_HISTORY_ABS_TOL):
        raise RecordingConversionError(f"{label} initial rate does not match case-spec rainfall")
    return history


def _rain_histories_match(actual: Any, expected: list[list[float]], label: str) -> list[list[float]]:
    parsed = _parse_rainfall_history(actual, expected[-1][0], expected[0][1], label)
    if len(parsed) != len(expected):
        raise RecordingConversionError(f"{label} does not match the frozen rainfall history")
    for index, (actual_row, expected_row) in enumerate(zip(parsed, expected, strict=True)):
        if (not math.isclose(actual_row[0], expected_row[0], rel_tol=0.0, abs_tol=1.0e-9)
                or not math.isclose(actual_row[1], expected_row[1],
                                   rel_tol=RAIN_HISTORY_REL_TOL,
                                   abs_tol=RAIN_HISTORY_ABS_TOL)):
            raise RecordingConversionError(
                f"{label}[{index}] does not match the frozen rainfall history"
            )
    return parsed


def _read_serialized_rainfall_history(path: Path) -> tuple[list[list[float]], str]:
    raw = _read_bytes(path, "serialized native rainfall history")
    try:
        lines = raw.decode("ascii").splitlines()
    except UnicodeDecodeError as error:
        raise RecordingConversionError("serialized native rainfall history must be ASCII") from error
    if not lines or lines[0].strip() != "1":
        raise RecordingConversionError("serialized native rainfall history must have header row count 1")
    rows: list[list[float]] = []
    for line_number, line in enumerate(lines[1:], start=2):
        columns = line.split()
        if len(columns) != 2:
            raise RecordingConversionError(
                f"serialized native rainfall row {line_number} must have time and rate columns"
            )
        try:
            timestamp, rate = (float(value) for value in columns)
        except ValueError as error:
            raise RecordingConversionError(
                f"serialized native rainfall row {line_number} is not numeric"
            ) from error
        if not math.isfinite(timestamp) or not math.isfinite(rate):
            raise RecordingConversionError(
                f"serialized native rainfall row {line_number} contains a non-finite value"
            )
        rows.append([timestamp, rate])
    return rows, _sha256_bytes(raw)


def _validate_rainfall_history_proof(
    case: Path, spec: dict[str, Any], case_protocol: dict[str, Any],
    case_result: dict[str, Any], duration_s: float, initial_rate_m_per_s: float,
) -> dict[str, Any] | None:
    if "rainfall_history" not in spec:
        return None
    history = _parse_rainfall_history(
        spec["rainfall_history"], duration_s, initial_rate_m_per_s, "case-spec rainfall_history"
    )
    protocol_settings = case_protocol.get("settings")
    if not isinstance(protocol_settings, dict):
        raise RecordingConversionError("case protocol has no rainfall-history settings proof")
    _rain_histories_match(
        protocol_settings.get("rain_source_schedule"), history,
        "case-protocol settings.rain_source_schedule",
    )
    result_provenance = case_result.get("input_provenance")
    audit = result_provenance.get("native_input_audit_before_solver") if isinstance(
        result_provenance, dict
    ) else None
    if not isinstance(audit, dict):
        raise RecordingConversionError("case result has no native rainfall-history audit proof")
    _rain_histories_match(
        audit.get("serialized_rainfall_history"), history,
        "case-result serialized_rainfall_history",
    )
    native_path = case / "native/input/field/precipitation_source_all.dat"
    serialized_rows, source_sha256 = _read_serialized_rainfall_history(native_path)
    _rain_histories_match(serialized_rows, history, "serialized native rainfall input")
    recorded_sha256 = audit.get("rain_source_sha256")
    if not isinstance(recorded_sha256, str) or recorded_sha256 != source_sha256:
        raise RecordingConversionError("native rainfall source hash does not match its before-solver audit")

    cumulative_m = 0.0
    for (t0, r0), (t1, r1) in zip(history, history[1:]):
        cumulative_m += (r0 + r1) * 0.5 * (t1 - t0)
        if not math.isfinite(cumulative_m) or not math.isfinite(cumulative_m * 1000.0):
            raise RecordingConversionError("rainfall history cumulative depth is outside the supported range")
    return {
        "history_m_per_s": history,
        "native_source_path": "native/input/field/precipitation_source_all.dat",
        "native_source_sha256": source_sha256,
        "serialized_history_tolerances": {
            "time_absolute_s": 1.0e-9,
            "rate_relative": RAIN_HISTORY_REL_TOL,
            "rate_absolute_m_per_s": RAIN_HISTORY_ABS_TOL,
        },
        "piecewise_linear_cumulative_scheduled_rain_mm": cumulative_m * 1000.0,
        "not_a_measured_water_balance": True,
    }


def _validate_case(
    case: Path,
) -> tuple[
    dict[str, Any],
    dict[str, Any],
    dict[str, float | int],
    list[float],
    str,
    str,
    str,
    str,
    array,
    str,
    list[float],
    int,
    int,
    float,
    dict[str, Any] | None,
]:
    spec, spec_bytes = _read_json(case / "case-spec.json", "case specification")
    case_protocol, protocol_bytes = _read_json(case / "case-protocol.json", "case protocol")
    case_result, result_bytes = _read_json(case / "case-result.json", "case result")
    dem_path = case / "DEM.asc"
    dem_header, source_values, dem_sha256 = _read_ascii_grid(dem_path, "source DEM")
    width = _positive_integer(spec.get("cols"), "case-spec cols")
    height = _positive_integer(spec.get("rows"), "case-spec rows")
    if width < 2 or height < 2:
        raise RecordingConversionError("case-spec grid dimensions must both be at least two")
    if width * height > MAX_CELLS:
        raise RecordingConversionError("case grid exceeds the supported cell limit")
    cell_size = _finite_number(spec.get("dx_m"), "case-spec dx_m")
    duration = _finite_number(spec.get("duration_s"), "case-spec duration_s")
    interval = _finite_number(spec.get("output_interval_s"), "case-spec output_interval_s")
    rain_rate = _finite_number(spec.get("rain_rate_m_per_s"), "case-spec rain_rate_m_per_s")
    if cell_size <= 0 or duration <= 0 or interval <= 0 or rain_rate < 0:
        raise RecordingConversionError("case-spec has invalid geometry, duration, or rainfall")
    if not duration.is_integer() or not interval.is_integer():
        raise RecordingConversionError("case output times must use an integer regular cadence")
    duration_integer = int(duration)
    interval_integer = int(interval)
    if duration_integer % interval_integer != 0:
        raise RecordingConversionError("case output times must use an integer regular cadence")
    frame_count = duration_integer // interval_integer + 1
    if frame_count > MAX_FRAMES:
        raise RecordingConversionError("case output cadence exceeds the supported frame limit")
    if width != int(dem_header["ncols"]) or height != int(dem_header["nrows"]):
        raise RecordingConversionError("case-spec geometry does not match source DEM")
    if not math.isclose(cell_size, float(dem_header["cellsize"]), rel_tol=0, abs_tol=1e-9):
        raise RecordingConversionError("case-spec cell size does not match source DEM")
    if case_protocol.get("case") != spec:
        raise RecordingConversionError("case protocol copy does not match case specification")
    input_dem_hash = case_protocol.get("input_dem_ascii_sha256")
    if input_dem_hash != dem_sha256:
        raise RecordingConversionError("case protocol DEM hash does not match source DEM")

    numeric_bed_path = case / "native/input/field/z.dat"
    numeric_bed, numeric_bed_sha256 = _read_native_bed(numeric_bed_path, width, height)
    if case_result.get("case") != spec:
        raise RecordingConversionError("case result copy does not match case specification")
    result_provenance = case_result.get("input_provenance")
    if not isinstance(result_provenance, dict):
        raise RecordingConversionError("case result has no input provenance")
    if result_provenance.get("case_dem_ascii_sha256") != dem_sha256:
        raise RecordingConversionError("case result DEM hash does not match source DEM")
    native_audit = result_provenance.get("native_input_audit_before_solver")
    if not isinstance(native_audit, dict):
        raise RecordingConversionError("case result has no native input audit")
    if native_audit.get("z_field_sha256") != numeric_bed_sha256:
        raise RecordingConversionError("case result numerical-bed hash does not match z.dat")
    if native_audit.get("z_element_count") != width * height:
        raise RecordingConversionError("case result numerical-bed element count does not match")
    rainfall_history_proof = _validate_rainfall_history_proof(
        case, spec, case_protocol, case_result, duration, rain_rate
    )

    expected_times = [float(index * interval_integer) for index in range(frame_count)]
    if case_protocol.get("expected_saved_times_s") != expected_times:
        raise RecordingConversionError("case protocol saved times do not match regular case cadence")
    if case_protocol.get("input_dem_source_bed_semantics") is None and result_provenance.get(
        "case_source_bed_semantics"
    ) is None:
        # The detailed source-bed semantics are recorded in case-protocol's top-level identity.
        source_identity = case_protocol.get("terrain_source_identity")
        if not isinstance(source_identity, dict) or "transform" not in source_identity:
            raise RecordingConversionError("case protocol omits immutable source-bed semantics")

    return (
        spec,
        case_protocol,
        dem_header,
        source_values,
        _sha256_bytes(spec_bytes),
        _sha256_bytes(protocol_bytes),
        _sha256_bytes(result_bytes),
        dem_sha256,
        numeric_bed,
        numeric_bed_sha256,
        expected_times,
        width,
        height,
        cell_size,
        rainfall_history_proof,
    )


def _collect_frame_paths(
    output_dir: Path, expected_times: list[float]
) -> tuple[dict[int, tuple[Path, Path, Path]], dict[str, Any] | None]:
    expected_ids = {int(value) for value in expected_times}
    patterns = {
        "h": re.compile(r"h_(0|[1-9][0-9]*)\.asc\Z"),
        "hUx": re.compile(r"hUx_(0|[1-9][0-9]*)\.asc\Z"),
        "hUy": re.compile(r"hUy_(0|[1-9][0-9]*)\.asc\Z"),
    }
    by_field: dict[str, dict[int, Path]] = {key: {} for key in patterns}
    auxiliary_max_depth: Path | None = None
    duration = int(expected_times[-1])
    max_depth_pattern = re.compile(r"h_max_(0|[1-9][0-9]*)\.asc\Z")
    for path in output_dir.iterdir():
        if path.name.startswith("h_max_"):
            match = max_depth_pattern.fullmatch(path.name)
            if (not path.is_file() or path.is_symlink() or match is None
                    or int(match.group(1)) != duration):
                raise RecordingConversionError(
                    f"native output has an unsupported max-depth auxiliary spelling: {path.name}"
                )
            if auxiliary_max_depth is not None:
                raise RecordingConversionError("native output has duplicate terminal max-depth auxiliaries")
            auxiliary_max_depth = path
            continue
        if path.name.endswith(".asc") and path.name.startswith(("h_", "hUx_", "hUy_")):
            if not path.is_file() or not any(
                pattern.fullmatch(path.name) for pattern in patterns.values()
            ):
                raise RecordingConversionError(
                    f"native output has an unsupported field timestamp spelling: {path.name}"
                )
        for field, pattern in patterns.items():
            match = pattern.fullmatch(path.name)
            if match:
                timestamp = int(match.group(1))
                if timestamp in by_field[field]:
                    raise RecordingConversionError(f"duplicate {field} timestamp {timestamp}")
                by_field[field][timestamp] = path
    for field in patterns:
        actual = set(by_field[field])
        if actual != expected_ids:
            missing = sorted(expected_ids - actual)
            extra = sorted(actual - expected_ids)
            raise RecordingConversionError(
                f"native {field} timestamps do not match protocol (missing={missing[:4]}, "
                f"extra={extra[:4]})"
            )
    frame_paths = {
        timestamp: (by_field["h"][timestamp], by_field["hUx"][timestamp], by_field["hUy"][timestamp])
        for timestamp in sorted(expected_ids)
    }
    auxiliary_provenance = None
    if auxiliary_max_depth is not None:
        aux_header, aux_values, aux_sha256 = _read_ascii_grid(
            auxiliary_max_depth, "terminal max-depth auxiliary"
        )
        first_h_header, _, _ = _read_ascii_grid(frame_paths[0][0], "time-zero depth frame")
        if aux_header != first_h_header or any(value < 0.0 for value in aux_values):
            raise RecordingConversionError("terminal max-depth auxiliary geometry is inconsistent")
        auxiliary_provenance = {
            "path": auxiliary_max_depth.name,
            "sha256": aux_sha256,
            "role": "terminal native maximum-depth diagnostic; not a saved frame",
        }
    return frame_paths, auxiliary_provenance


def _frame_stats(depth: array, qx: array, qz: array, cell_size_m: float) -> dict[str, float | int]:
    area = cell_size_m * cell_size_m
    water_volume = sum(depth) * area
    max_depth = max(depth, default=0.0)
    max_speed = 0.0
    dry_nonzero = 0
    for h, x, z in zip(depth, qx, qz, strict=True):
        if h == 0.0:
            if x != 0.0 or z != 0.0:
                dry_nonzero += 1
            continue
        speed = math.hypot(x, z) / h
        if not math.isfinite(speed) or speed > 3.4028234663852886e38:
            raise RecordingConversionError("derived velocity is outside float32 representation")
        max_speed = max(max_speed, speed)
    if dry_nonzero:
        raise RecordingConversionError(
            f"frame has {dry_nonzero} dry cells with nonzero momentum"
        )
    if not math.isfinite(water_volume):
        raise RecordingConversionError("frame water volume is not representable")
    return {
        "water_volume_m3": water_volume,
        "max_depth_m": float(max_depth),
        "max_speed_m_per_s": max_speed,
        "dry_nonzero_momentum_cells": 0,
    }


def _read_frame(
    paths: tuple[Path, Path, Path],
    timestamp: int,
    reference_geometry: dict[str, float | int],
    cell_size_m: float,
) -> tuple[array, array, array, dict[str, float | int], dict[str, str]]:
    (h_path, qx_path, native_qz_path) = paths
    h_header, h_values, h_sha = _read_ascii_grid(h_path, f"h at {timestamp}s")
    qx_header, qx_values, qx_sha = _read_ascii_grid(qx_path, f"hUx at {timestamp}s")
    qz_header, native_qz_values, qz_sha = _read_ascii_grid(
        native_qz_path, f"hUy at {timestamp}s"
    )
    _same_geometry(reference_geometry, h_header, f"h at {timestamp}s")
    _same_geometry(reference_geometry, qx_header, f"hUx at {timestamp}s")
    _same_geometry(reference_geometry, qz_header, f"hUy at {timestamp}s")
    for h, x, native_z in zip(h_values, qx_values, native_qz_values, strict=True):
        if h < 0.0:
            raise RecordingConversionError(f"h at {timestamp}s contains negative depth")
        if h == 0.0 and (x != 0.0 or native_z != 0.0):
            raise RecordingConversionError(
                f"h at {timestamp}s has zero depth with nonzero momentum"
            )
    depth = _as_float32(h_values, f"h at {timestamp}s")
    qx = _as_float32(qx_values, f"hUx at {timestamp}s")
    qz = _as_float32([-value for value in native_qz_values], f"-hUy at {timestamp}s")
    stats = _frame_stats(depth, qx, qz, cell_size_m)
    return depth, qx, qz, stats, {"h": h_sha, "hUx": qx_sha, "hUy": qz_sha}


def convert_case(case: Path, out: Path) -> Path:
    """Convert a case into a new directory and return its manifest path."""

    case = case.expanduser().resolve()
    out = out.expanduser().absolute()
    if os.path.lexists(out):
        raise RecordingConversionError(f"output already exists; refusing to overwrite: {out}")
    (
        spec,
        case_protocol,
        dem_header,
        source_values,
        spec_sha,
        case_protocol_sha,
        case_result_sha,
        dem_sha,
        numerical_bed,
        numerical_bed_sha,
        times,
        width,
        height,
        cell_size,
        rainfall_history_proof,
    ) = _validate_case(case)
    frame_paths, auxiliary_max_depth = _collect_frame_paths(case / "native/output", times)

    source_bed = _as_float32(source_values, "source DEM bed")
    bed_error = [float(native) - float(source) for native, source in zip(
        numerical_bed, source_bed, strict=True
    )]
    max_bed_error = max(abs(value) for value in bed_error)
    rms_bed_error = math.sqrt(sum(value * value for value in bed_error) / len(bed_error))

    parent = out.parent
    parent.mkdir(parents=True, exist_ok=True)
    try:
        out.mkdir(parents=False, exist_ok=False)
    except FileExistsError as error:
        raise RecordingConversionError(
            f"output already exists; refusing to overwrite: {out}"
        ) from error

    try:
        bed_sha = _write_float32(out / "bed.f32", numerical_bed)
        source_bed_sha = _write_float32(out / "source-bed.f32", source_bed)
        frames_manifest: list[dict[str, Any]] = []
        raw_hashes: list[dict[str, Any]] = []
        frame_dir = out / "frames"
        frame_dir.mkdir()
        for index, timestamp in enumerate(times):
            depth, qx, qz, stats, asc_hashes = _read_frame(
                frame_paths[int(timestamp)], int(timestamp), dem_header, cell_size
            )
            relative_path = f"frames/frame-{index:06d}.f32"
            payload_values = array("f", depth)
            payload_values.extend(qx)
            payload_values.extend(qz)
            payload_sha = _write_float32(out / relative_path, payload_values)
            frames_manifest.append(
                {
                    "time_s": timestamp,
                    "path": relative_path,
                    "sha256": payload_sha,
                    **stats,
                }
            )
            raw_hashes.append({"time_s": timestamp, **asc_hashes})

        source_identity = _terrain_identity(case_protocol)
        manifest: dict[str, Any] = {
            "schema": SCHEMA,
            "encoding": ENCODING,
            "fields": FIELDS,
            "field_layout": "planar",
            "grid": {
                "width": width,
                "height": height,
                "cell_size_m": cell_size,
                "storage_order": "row-major",
                "row_direction": "world-z-positive",
                "sample_location": "cell-center",
            },
            "bed": {"path": "bed.f32", "sha256": bed_sha},
            "source_bed": {"path": "source-bed.f32", "sha256": source_bed_sha},
            "frames": frames_manifest,
            "protocol": spec,
            "provenance": {
                "source_sha256": {
                    "case_spec": spec_sha,
                    "case_protocol": case_protocol_sha,
                    "case_result": case_result_sha,
                    "dem_asc": dem_sha,
                    "native_numerical_bed": numerical_bed_sha,
                },
                "raw_asc_sha256": raw_hashes,
                "terrain_source_identity": source_identity,
                "precision": {
                    "native_numerical_bed_format": "%g (six significant digits)",
                    "native_bed_vs_source_bed_max_abs_error_m": max_bed_error,
                    "native_bed_vs_source_bed_rms_error_m": rms_bed_error,
                    "native_output_ascii_decimal_places": 6,
                    "native_output_ascii_rounding_half_step": 0.0000005,
                    "native_world_z_momentum_sign": -1,
                },
            },
        }
        if rainfall_history_proof is not None:
            manifest["provenance"]["rainfall_history_proof"] = rainfall_history_proof
        if auxiliary_max_depth is not None:
            manifest["provenance"]["native_auxiliary_outputs"] = [auxiliary_max_depth]
        manifest_path = out / "recording.json"
        manifest_path.write_text(
            json.dumps(manifest, indent=2, sort_keys=True, allow_nan=False) + "\n",
            encoding="utf-8",
        )
        return manifest_path
    except Exception:
        shutil.rmtree(out, ignore_errors=True)
        raise


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", required=True, type=Path, help="frozen SynxFlow case directory")
    parser.add_argument("--out", required=True, type=Path, help="new output directory")
    arguments = parser.parse_args(argv)
    try:
        manifest = convert_case(arguments.case, arguments.out)
    except (OSError, RecordingConversionError) as error:
        parser.error(str(error))
    print(manifest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
