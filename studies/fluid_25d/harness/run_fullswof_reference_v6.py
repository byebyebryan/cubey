#!/usr/bin/env python3
"""Source-bound, opt-in FullSWOF_2D v1.10.00 reference cases for Rain V6."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import re
import struct
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[3]
STUDY_REL = Path("outputs/fluid/hillside-rain-v6-fullswof-20261002-Po5ckP")
SETUP_REL = STUDY_REL / "reference-setup"
LOCK_REL = SETUP_REL / "native-lock.json"
PARAMETERS_REL = Path("Examples/Simple/Inputs/parameters.txt")
GRAVITY = 9.81
HE_CA = 1.0e-12
DEPTH_LIMIT = 0.01
VELOCITY_LIMIT = 0.05


class ReferenceError(RuntimeError):
    pass


@dataclass(frozen=True)
class CaseSpec:
    name: str
    family: str
    dx: float
    nx: int
    ny: int
    horizon: float
    timestep: float
    bed: tuple[tuple[float, ...], ...]
    h0: tuple[tuple[float, ...], ...]
    u0: tuple[tuple[float, ...], ...]
    v0: tuple[tuple[float, ...], ...]
    boundaries: tuple[int, int, int, int]
    reference_depths: tuple[tuple[float, ...], ...]
    reference_velocities: tuple[tuple[float, ...], ...]
    reference_description: str
    measured_cells: tuple[tuple[int, int], ...]
    parameters: dict[str, str]
    expected_observation_times: tuple[float, ...]
    exploratory: bool = False


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _checked_path(repo_root: Path, relative: str) -> Path:
    candidate = (repo_root / relative).resolve()
    if repo_root.resolve() not in candidate.parents:
        raise ReferenceError(f"locked path escapes repository: {relative}")
    return candidate


def _fresh_output_path(study_root: Path, output_relative: str) -> Path:
    relative_path = Path(output_relative)
    if relative_path.is_absolute():
        raise ReferenceError("output path must be relative to the V6 evidence root")
    case_dir = (study_root / relative_path).resolve()
    if study_root.resolve() not in case_dir.parents:
        raise ReferenceError("output path escapes the V6 evidence root")
    if case_dir.exists():
        raise ReferenceError(f"refusing to overwrite existing output leaf: {case_dir}")
    return case_dir


def _verify_manifest(repo_root: Path, manifest: Path, base: Path) -> int:
    checked = 0
    for line_number, line in enumerate(manifest.read_text(encoding="utf-8").splitlines(), 1):
        if not line:
            continue
        parts = re.split(r"\s{2,}", line, maxsplit=1)
        if len(parts) != 2 or not re.fullmatch(r"[0-9a-f]{64}", parts[0]):
            raise ReferenceError(f"malformed source manifest at line {line_number}")
        raw_path = parts[1]
        if raw_path.startswith("./"):
            path = (base / raw_path).resolve()
        else:
            path = (repo_root / raw_path).resolve()
        if base.resolve() not in path.parents:
            raise ReferenceError(f"manifest path escapes its bound tree: {raw_path}")
        if not path.is_file() or _sha256(path) != parts[0]:
            raise ReferenceError(f"source file differs from archive manifest: {raw_path}")
        checked += 1
    if checked == 0:
        raise ReferenceError(f"empty source manifest: {manifest}")
    return checked


def verify_binding(repo_root: Path) -> dict[str, Any]:
    lock_path = _checked_path(repo_root, LOCK_REL.as_posix())
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    for key, sha_key in (
        ("archive_path", "archive_sha256"),
        ("source_tree_manifest_path", "source_tree_manifest_sha256"),
        ("build_copy_manifest_path", "build_copy_manifest_sha256"),
    ):
        path = _checked_path(repo_root, lock[key])
        actual = _sha256(path)
        if actual != lock[sha_key]:
            raise ReferenceError(f"{key} SHA-256 mismatch: {actual}")
    source_root = _checked_path(repo_root, lock["source_tree_path"])
    build_root = _checked_path(repo_root, lock["build_tree_path"])
    source_manifest = _checked_path(repo_root, lock["source_tree_manifest_path"])
    build_manifest = _checked_path(repo_root, lock["build_copy_manifest_path"])
    source_file_count = _verify_manifest(repo_root, source_manifest, source_root)
    build_file_count = _verify_manifest(repo_root, build_manifest, build_root)
    observer = lock["observer_cli"]
    binary = _checked_path(repo_root, observer["path"])
    if not binary.is_file() or _sha256(binary) != observer["sha256"]:
        raise ReferenceError("source-bound FullSWOF observer binary SHA-256 mismatch")
    release_cli = lock["native_cli"]
    release_binary = _checked_path(repo_root, release_cli["path"])
    if not release_binary.is_file() or _sha256(release_binary) != release_cli["sha256"]:
        raise ReferenceError("source-bound release FullSWOF binary SHA-256 mismatch")
    return {
        "lock": lock,
        "source_root": source_root,
        "build_root": build_root,
        "binary": binary,
        "release_binary": release_binary,
        "source_file_count": source_file_count,
        "build_file_count": build_file_count,
        "archive_sha256": lock["archive_sha256"],
        "source_manifest_sha256": lock["source_tree_manifest_sha256"],
        "binary_sha256": observer["sha256"],
        "release_binary_sha256": release_cli["sha256"],
    }


def _float32(value: float) -> float:
    return struct.unpack("=f", struct.pack("=f", value))[0]


def _shallow_root(energy_minus_bed: float, discharge: float, gravity: float) -> float:
    critical = (discharge * discharge / gravity) ** (1.0 / 3.0)
    lower = max(1.0e-15, critical * 1.0e-14)
    upper = critical

    def residual(depth: float) -> float:
        return depth + discharge * discharge / (2.0 * gravity * depth * depth) - energy_minus_bed

    if residual(lower) <= 0.0 or residual(upper) > 0.0:
        raise ReferenceError("shallow Bernoulli root is not bracketed by (0, critical depth]")
    for _ in range(100):
        middle = 0.5 * (lower + upper)
        if residual(middle) > 0.0:
            lower = middle
        else:
            upper = middle
    return 0.5 * (lower + upper)


def _simpson_cell_mean(x0: float, width: float, samples: int, energy: float, discharge: float) -> float:
    if samples <= 0 or samples % 2:
        raise ReferenceError("Simpson interval count must be a positive even number")
    total = _shallow_root(energy - 2.0 * math.sin(2.0 * math.pi * x0 / 480.0), discharge, GRAVITY)
    total += _shallow_root(energy - 2.0 * math.sin(2.0 * math.pi * (x0 + width) / 480.0), discharge, GRAVITY)
    for index in range(1, samples):
        x = x0 + width * index / samples
        bed = 2.0 * math.sin(2.0 * math.pi * x / 480.0)
        total += (4.0 if index % 2 else 2.0) * _shallow_root(energy - bed, discharge, GRAVITY)
    return total / (3.0 * samples)


def _grid(rows: int, columns: int, function: Any) -> tuple[tuple[float, ...], ...]:
    return tuple(tuple(float(function(i, j)) for j in range(columns)) for i in range(rows))


def _curved_case(
    dx: float,
    timestep: float,
    point_initial: bool = False,
    exploratory: bool = False,
    periods: int = 1,
    central_period: int | None = None,
) -> CaseSpec:
    cells_per_period = round(480.0 / dx)
    nx = cells_per_period * periods
    ny = 2
    discharge = 0.2
    energy = 0.02 + discharge * discharge / (2.0 * GRAVITY * 0.02**2)
    bed_rows = _grid(nx, ny, lambda i, _j: _float32(2.0 * math.sin(2.0 * math.pi * ((i + 0.5) * dx) / 480.0)))
    if point_initial:
        refs_x = tuple(
            _shallow_root(energy - 2.0 * math.sin(2.0 * math.pi * ((i + 0.5) * dx) / 480.0), discharge, GRAVITY)
            for i in range(nx)
        )
        h_rows = _grid(nx, ny, lambda i, _j: _float32(refs_x[i]))
        description = "smooth sine point-depth steady solution; independent bisection at cell-center bed samples"
        name = f"curved-point-{dx:g}m-dt{int(round(timestep * 1000)):03d}"
    else:
        means = tuple(_simpson_cell_mean(i * dx, dx, 64, energy, discharge) for i in range(nx))
        refs_x = means
        h_rows = _grid(nx, ny, lambda i, _j: _float32(means[i]))
        description = "historical finite-volume reference: smooth sine bed with independent 64-panel composite Simpson cell mean"
        name = (
            f"curved-{dx:g}m-dt{int(round(timestep * 1000)):03d}"
            if periods == 1
            else f"curved-{periods}period-{dx:g}m-dt{int(round(timestep * 1000)):03d}"
        )
    u_rows = _grid(nx, ny, lambda i, _j: discharge / h_rows[i][0])
    v_rows = _grid(nx, ny, lambda _i, _j: 0.0)
    ref_u = tuple(discharge / value for value in refs_x)
    ref_depth_rows = _grid(nx, ny, lambda i, _j: refs_x[i])
    ref_velocity_rows = _grid(nx, ny, lambda i, _j: ref_u[i])
    return CaseSpec(
        name=name,
        family="curved-moving-equilibrium",
        dx=dx,
        nx=nx,
        ny=ny,
        horizon=10.0,
        timestep=timestep,
        bed=bed_rows,
        h0=h_rows,
        u0=u_rows,
        v0=v_rows,
        boundaries=(4, 4, 2, 2),
        reference_depths=ref_depth_rows,
        reference_velocities=ref_velocity_rows,
        reference_description=(
            f"{description}; measure only period {central_period} of {periods} native-periodic bed periods"
            if central_period is not None
            else description
        ),
        measured_cells=tuple(
            (i, j)
            for i in range(nx)
            for j in range(ny)
            if central_period is None
            or central_period * 480.0 <= (i + 0.5) * dx <= (central_period + 1) * 480.0
        ),
        parameters={
            "discharge_m2_s": str(discharge),
            "energy_m": format(energy, ".17g"),
            "point_initialization": str(point_initial).lower(),
            "domain_length_x_m": format(nx * dx, ".17g"),
            "period_count": str(periods),
            "measured_period_index": "all" if central_period is None else str(central_period),
        },
        expected_observation_times=(2.0, 4.0, 10.0),
        exploratory=exploratory,
    )


def _incline_case(timestep: float) -> CaseSpec:
    dx = 30.0
    nx, ny, slope, depth = 64, 8, 0.25, 0.02
    bed = _grid(nx, ny, lambda i, _j: -slope * ((i + 0.5) * dx))
    h = _grid(nx, ny, lambda _i, _j: depth)
    u = _grid(nx, ny, lambda _i, _j: 0.0)
    v = _grid(nx, ny, lambda _i, _j: 0.0)
    return CaseSpec(
        name=f"incline-frictionless-30m-dt{int(round(timestep * 1000)):03d}",
        family="frictionless-accelerating-incline",
        dx=dx,
        nx=nx,
        ny=ny,
        horizon=10.0,
        timestep=timestep,
        bed=bed,
        h0=h,
        u0=u,
        v0=v,
        boundaries=(3, 3, 2, 2),
        reference_depths=_grid(nx, ny, lambda _i, _j: depth),
        reference_velocities=_grid(nx, ny, lambda _i, _j: 0.0),
        reference_description="exact constant-depth, constant-acceleration incline; u(t)=g*S*t",
        measured_cells=tuple(
            (i, j)
            for i in range(nx)
            for j in range(ny)
            if 480.0 <= (i + 0.5) * dx <= 1440.0 and 60.0 <= (j + 0.5) * dx <= 180.0
        ),
        parameters={"slope": str(slope), "depth_m": str(depth), "gravity_m_s2": str(GRAVITY), "control": "frictionless"},
        expected_observation_times=(2.0, 4.0, 10.0),
    )


def _lake_case(eta: float) -> CaseSpec:
    dx, nx, ny, horizon, timestep = 30.0, 16, 8, 10.0, 0.05
    bed = _grid(
        nx,
        ny,
        lambda i, j: _float32(
            2.0 * math.sin(2.0 * math.pi * ((i + 0.5) * dx) / 480.0)
            + 0.5 * math.cos(2.0 * math.pi * ((j + 0.5) * dx) / 240.0)
        ),
    )
    depth = _grid(nx, ny, lambda i, j: max(eta - bed[i][j], 0.0))
    zero = _grid(nx, ny, lambda _i, _j: 0.0)
    kind = "fully-wet" if eta == 3.0 else "partially-dry"
    return CaseSpec(
        name=f"lake-rest-{kind}-30m",
        family="lake-at-rest",
        dx=dx,
        nx=nx,
        ny=ny,
        horizon=horizon,
        timestep=timestep,
        bed=bed,
        h0=depth,
        u0=zero,
        v0=zero,
        boundaries=(2, 2, 2, 2),
        reference_depths=depth,
        reference_velocities=zero,
        reference_description="closed lake at rest: h=max(eta-z,0), zero velocity, exact per-cell depth held fixed",
        measured_cells=tuple((i, j) for i in range(nx) for j in range(ny)),
        parameters={"free_surface_eta_m": str(eta), "depth_drift_limit_m": "1e-5", "velocity_component_limit_m_s": "1e-5"},
        expected_observation_times=(10.0,),
    )


def _rain_case(timestep: float) -> CaseSpec:
    dx, nx, ny, horizon = 30.0, 16, 8, 600.0
    rain_rate = _float32(12.0 / 3600000.0)
    bed_elevation = 1000.0
    bed = _grid(nx, ny, lambda _i, _j: bed_elevation)
    zero = _grid(nx, ny, lambda _i, _j: 0.0)
    depth = _grid(nx, ny, lambda _i, _j: 0.0)
    return CaseSpec(
        name=f"closed-flat-dry-rain-12mmph-dt{int(round(timestep * 1000)):03d}",
        family="closed-flat-dry-rain",
        dx=dx,
        nx=nx,
        ny=ny,
        horizon=horizon,
        timestep=timestep,
        bed=bed,
        h0=depth,
        u0=zero,
        v0=zero,
        boundaries=(2, 2, 2, 2),
        reference_depths=depth,
        reference_velocities=zero,
        reference_description="closed flat bed at 1000 m, dry-start domain under constant native rain R; exact h=R*t and zero flow",
        measured_cells=tuple((i, j) for i in range(nx) for j in range(ny)),
        parameters={
            "bed_elevation_m": _number(bed_elevation),
            "rain_rate_m_s_float32": format(rain_rate, ".17g"),
            "rain_total_time_s": str(horizon),
        },
        expected_observation_times=(horizon,),
    )


def _manning_case() -> CaseSpec:
    dx, nx, ny, slope, depth, manning_n = 30.0, 64, 8, 0.1, 0.004, 0.03
    speed = math.sqrt(slope) * depth ** (2.0 / 3.0) / manning_n
    bed = _grid(nx, ny, lambda i, _j: -slope * ((i + 0.5) * dx))
    h = _grid(nx, ny, lambda _i, _j: depth)
    u = _grid(nx, ny, lambda _i, _j: speed)
    v = _grid(nx, ny, lambda _i, _j: 0.0)
    return CaseSpec(
        name="manning-thin-steady-sheet-30m",
        family="manning-normal-flow",
        dx=dx,
        nx=nx,
        ny=ny,
        horizon=10.0,
        timestep=0.05,
        bed=bed,
        h0=h,
        u0=u,
        v0=v,
        boundaries=(3, 3, 2, 2),
        reference_depths=h,
        reference_velocities=u,
        reference_description="uniform normal-flow sheet: u=sqrt(S)*h^(2/3)/n, Manning friction balances bed slope",
        measured_cells=tuple(
            (i, j)
            for i in range(nx)
            for j in range(ny)
            if 480.0 <= (i + 0.5) * dx <= 1440.0 and 60.0 <= (j + 0.5) * dx <= 180.0
        ),
        parameters={
            "slope": str(slope),
            "depth_m": str(depth),
            "manning_n": str(manning_n),
            "normal_velocity_m_s": format(speed, ".17g"),
        },
        expected_observation_times=(2.0, 4.0, 10.0),
    )


def available_cases() -> dict[str, CaseSpec]:
    cases: dict[str, CaseSpec] = {}
    for dx, timestep in ((30.0, 0.05), (30.0, 0.025), (15.0, 0.05), (7.5, 0.05)):
        spec = _curved_case(dx, timestep)
        cases[spec.name] = spec
    point = _curved_case(30.0, 0.05, point_initial=True)
    cases[point.name] = point
    for timestep in (0.05, 0.025):
        spec = _incline_case(timestep)
        cases[spec.name] = spec
    # Native terrain ghosts extrapolate the end-to-end average slope instead
    # of wrapping this sine. These buffer cases isolate the central period
    # without patching native boundary code or relaxing the original gate.
    for dx, timestep in ((30.0, 0.05), (30.0, 0.025), (15.0, 0.05), (7.5, 0.05)):
        spec = _curved_case(dx, timestep, periods=5, central_period=2)
        cases[spec.name] = spec
    spec = _curved_case(30.0, 0.05, periods=3, central_period=1)
    cases[spec.name] = spec
    for eta in (3.0, 0.5):
        spec = _lake_case(eta)
        cases[spec.name] = spec
    for timestep in (0.5, 0.25):
        spec = _rain_case(timestep)
        cases[spec.name] = spec
    spec = _manning_case()
    cases[spec.name] = spec
    return cases


def _number(value: float) -> str:
    return format(value, ".17g")


def _format_rows(rows: tuple[tuple[float, ...], ...], make_line: Any) -> str:
    return "".join(make_line(i, j, value) for i, row in enumerate(rows) for j, value in enumerate(row))


def _point_coordinates(spec: CaseSpec) -> tuple[tuple[float, float], ...]:
    return tuple(
        ((i + 0.5) * spec.dx, (j + 0.5) * spec.dx)
        for i in range(spec.nx)
        for j in range(spec.ny)
    )


def _set_tag(parameters: str, tag: str, value: str) -> str:
    target = f"<{tag}>::"
    lines = parameters.splitlines()
    found = 0
    for index, line in enumerate(lines):
        if target in line:
            prefix = line.split(target, 1)[0] + target
            lines[index] = f"{prefix} {value}"
            found += 1
    if found != 1:
        raise ReferenceError(f"expected exactly one parameter tag {tag!r}; found {found}")
    return "\n".join(lines) + "\n"


def _write_inputs(spec: CaseSpec, case_dir: Path, source_root: Path) -> dict[str, str]:
    inputs = case_dir / "Inputs"
    outputs = case_dir / "Outputs"
    inputs.mkdir(parents=True)
    outputs.mkdir()
    template = (source_root / PARAMETERS_REL).read_text(encoding="utf-8")
    lx = spec.nx * spec.dx
    ly = spec.ny * spec.dx
    fields = {
        "Nxcell": str(spec.nx),
        "Nycell": str(spec.ny),
        "L": _number(lx),
        "l": _number(ly),
        "T": _number(spec.horizon),
        "nbtimes": str(int(spec.horizon / 2.0) + 1),
        "scheme_type": "2",
        "dtfix": _number(spec.timestep),
        "cflfix": "0.4",
        "L_bc_init": "2",
        "Lbound": str(spec.boundaries[0]),
        "R_bc_init": "2",
        "Rbound": str(spec.boundaries[1]),
        "B_bc_init": "2",
        "Bbound": str(spec.boundaries[2]),
        "T_bc_init": "2",
        "Tbound": str(spec.boundaries[3]),
        "fric": "1" if "manning_n" in spec.parameters else "0",
        "fric_init": "2",
        "friccoef": spec.parameters.get("manning_n", "0"),
        "flux": "2",
        "order": "2",
        "rec": "1",
        "lim": "1",
        "topo": "1",
        "topo_NF": "topography.txt",
        "huv_init": "1",
        "huv_NF": "huv.txt",
        "rain": "1" if "rain_rate_m_s_float32" in spec.parameters else "0",
        "rain_NF": "rain.txt" if "rain_rate_m_s_float32" in spec.parameters else "",
        "inf": "0",
        "output_f": "1",
        "Choice_points": "2",
        "list_point_NF": "list_of_points.txt",
        "Choice_dt_specific_points": "1",
        "dt_specific_points": "0",
    }
    for tag, value in fields.items():
        template = _set_tag(template, tag, value)
    (inputs / "parameters.txt").write_text(template, encoding="ascii")
    (inputs / "topography.txt").write_text(
        _format_rows(spec.bed, lambda i, j, z: f"{_number((i + 0.5) * spec.dx)} {_number((j + 0.5) * spec.dx)} {_number(z)}\n"),
        encoding="ascii",
    )
    (inputs / "huv.txt").write_text(
        _format_rows(
            spec.h0,
            lambda i, j, h: f"{_number((i + 0.5) * spec.dx)} {_number((j + 0.5) * spec.dx)} {_number(h)} {_number(spec.u0[i][j])} {_number(spec.v0[i][j])}\n",
        ),
        encoding="ascii",
    )
    point_text = "".join(f"{_number(x)} {_number(y)}\n" for x, y in _point_coordinates(spec))
    (inputs / "list_of_points.txt").write_text(point_text, encoding="ascii")
    if "rain_rate_m_s_float32" in spec.parameters:
        (inputs / "rain.txt").write_text(f"0 {_number(float(spec.parameters['rain_rate_m_s_float32']))}\n", encoding="ascii")
    hashes = {
        path.name: _sha256(path)
        for path in sorted(inputs.iterdir())
        if path.is_file()
    }
    return hashes


def parse_specific_points(
    path: Path,
    expected_cells: int,
    expected_coordinates: tuple[tuple[float, float], ...] | None = None,
) -> dict[float, dict[tuple[float, float], tuple[float, float, float]]]:
    if not path.is_file():
        raise ReferenceError(f"native all-cell series is missing: {path}")
    frames: dict[float, dict[tuple[float, float], tuple[float, float, float]]] = {}
    frame_order: dict[float, list[tuple[float, float]]] = {}
    current_time: float | None = None
    previous_time: float | None = None
    for line_number, line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        parts = stripped.split()
        if len(parts) != 6:
            raise ReferenceError(f"malformed specific-point row {line_number}: expected 6 fields")
        try:
            values = tuple(float(token) for token in parts)
        except ValueError as exc:
            raise ReferenceError(f"non-numeric native state in specific-point row {line_number}") from exc
        if not all(math.isfinite(value) for value in values):
            raise ReferenceError(f"non-finite native state in specific-point row {line_number}")
        time_value, x, y, h, u, v = values
        if current_time is None or time_value != current_time:
            if current_time is not None:
                if len(frames[current_time]) != expected_cells:
                    raise ReferenceError(f"frame {current_time} ended before all expected cells were written")
                previous_time = current_time
            if previous_time is not None and time_value <= previous_time:
                raise ReferenceError(f"native frame times are not strictly increasing at row {line_number}")
            current_time = time_value
        frame = frames.setdefault(time_value, {})
        key = (x, y)
        if key in frame:
            raise ReferenceError(f"duplicate native point at frame {time_value}: {key}")
        frame[key] = (h, u, v)
        frame_order.setdefault(time_value, []).append(key)
    if not frames:
        raise ReferenceError("native specific-point series contains no state frames")
    for time_value, frame in frames.items():
        if len(frame) != expected_cells:
            raise ReferenceError(f"frame {time_value} has {len(frame)} cells; expected {expected_cells}")
        if expected_coordinates is not None and tuple(frame_order[time_value]) != expected_coordinates:
            raise ReferenceError(f"native coordinate order differs from the generated point list at frame {time_value}")
    return frames


def _saved_state_coverage(
    spec: CaseSpec,
    frames: dict[float, dict[tuple[float, float], tuple[float, float, float]]],
) -> dict[str, Any]:
    dt_steps = round(spec.horizon / spec.timestep)
    expected_count = dt_steps + 1
    observed_times = sorted(frames)
    time_tolerance = max(2.0e-9, spec.timestep * 2.0e-8)
    coverage_failures: list[dict[str, float]] = []
    time_index = 0
    for step in range(expected_count):
        wanted = step * spec.timestep
        while time_index + 1 < len(observed_times) and abs(observed_times[time_index + 1] - wanted) < abs(observed_times[time_index] - wanted):
            time_index += 1
        if not observed_times or abs(observed_times[time_index] - wanted) > time_tolerance:
            coverage_failures.append({"step": step, "expected_s": wanted})
    coverage_ok = len(observed_times) == expected_count and not coverage_failures
    return {
        "available": True,
        "expected_frame_count": expected_count,
        "observed_frame_count": len(observed_times),
        "expected_step_s": spec.timestep,
        "observed_min_time_s": observed_times[0],
        "observed_max_time_s": observed_times[-1],
        "every_fixed_step_saved": coverage_ok,
        "missing_steps": coverage_failures,
        "time_tolerance_s": time_tolerance,
    }


def _measure_lake(spec: CaseSpec, frames: dict[float, dict[tuple[float, float], tuple[float, float, float]]]) -> dict[str, Any]:
    coverage = _saved_state_coverage(spec, frames)
    max_depth_abs = -1.0
    max_velocity_component = -1.0
    depth_at: dict[str, Any] | None = None
    velocity_at: dict[str, Any] | None = None
    dry_initial = {
        (i, j)
        for i, j in spec.measured_cells
        if spec.reference_depths[i][j] <= HE_CA
    }
    rewet_counts: dict[str, int] = {}
    for time_value in sorted(frames):
        frame = frames[time_value]
        rewet_counts[_number(time_value)] = 0
        for i, j in spec.measured_cells:
            x = (i + 0.5) * spec.dx
            y = (j + 0.5) * spec.dx
            h_actual, u_actual, v_actual = frame[(x, y)]
            if (i, j) in dry_initial and h_actual > HE_CA:
                rewet_counts[_number(time_value)] += 1
            h_abs = abs(h_actual - spec.reference_depths[i][j])
            velocity_component = max(abs(u_actual), abs(v_actual))
            if h_abs > max_depth_abs:
                max_depth_abs = h_abs
                depth_at = {"time_s": time_value, "cell_ij": [i, j]}
            if velocity_component > max_velocity_component:
                max_velocity_component = velocity_component
                velocity_at = {"time_s": time_value, "cell_ij": [i, j]}
    depth_limit = 1.0e-5
    velocity_limit = 1.0e-5
    return {
        "saved_state_coverage": coverage,
        "observations": {
            "all_saved_states": {
                "measured_cell_count": len(spec.measured_cells),
                "max_absolute_depth_drift_m": max_depth_abs,
                "depth_drift_at": depth_at,
                "depth_limit_m": depth_limit,
                "depth_pass": max_depth_abs <= depth_limit and coverage["every_fixed_step_saved"],
                "max_absolute_velocity_component_m_s": max_velocity_component,
                "velocity_at": velocity_at,
                "velocity_limit_m_s": velocity_limit,
                "velocity_pass": max_velocity_component <= velocity_limit and coverage["every_fixed_step_saved"],
                "initially_dry_cell_count": len(dry_initial),
                "dry_rewet_count_by_time": rewet_counts,
            }
        },
        "depth_pass": max_depth_abs <= depth_limit and coverage["every_fixed_step_saved"],
        "velocity_pass": max_velocity_component <= velocity_limit and coverage["every_fixed_step_saved"],
        "positive_control_pass": (
            max_depth_abs <= depth_limit
            and max_velocity_component <= velocity_limit
            and coverage["every_fixed_step_saved"]
        ),
        "actual_cumulative_applied_face_transfer": {
            "value": None,
            "status": "unsupported_by_unmodified_native_cli",
            "note": "Native internal face arrays and Heun-weighted integrals are not emitted by the stock CLI.",
        },
        "positivity_and_clipping": {
            "published_state_series": "every fixed step; native specific-point output replaces h below HE_CA by zero",
            "intermediate_stage_minimum_depth": None,
            "clipping_correction_volume": None,
            "all_stage_positivity_status": "unsupported_by_unmodified_native_cli",
            "HE_CA_m": HE_CA,
            "VE_CA_m_s": HE_CA,
        },
    }


def _decimal_quantization_bound(value: float) -> float:
    if value == 0.0:
        return 0.0
    return 0.5 * 10.0 ** (math.floor(math.log10(abs(value))) - 9)


def _depth_within_precision(actual: float, expected: float) -> bool:
    text_bound = max(_decimal_quantization_bound(actual), _decimal_quantization_bound(expected))
    binary_bound = 4.0 * max(math.ulp(actual), math.ulp(expected))
    return abs(actual - expected) <= text_bound + binary_bound


def _ledger_classification(residual_abs: float, rounding_bound: float, budget: float) -> tuple[str, float]:
    worst_case_residual = residual_abs + rounding_bound
    best_case_residual = max(0.0, residual_abs - rounding_bound)
    margin = worst_case_residual - budget
    if worst_case_residual <= budget:
        return "definite_pass_with_text_rounding_bound", margin
    if best_case_residual > budget:
        return "definite_failure_beyond_text_rounding_bound", margin
    return "inconclusive_rounding_interval_overlaps_budget", margin


def _measure_rain(spec: CaseSpec, frames: dict[float, dict[tuple[float, float], tuple[float, float, float]]]) -> dict[str, Any]:
    coverage = _saved_state_coverage(spec, frames)
    rain_rate = float(spec.parameters["rain_rate_m_s_float32"])
    cell_area = spec.dx * spec.dx
    total_area = spec.nx * spec.ny * cell_area
    supplied_volume = rain_rate * spec.horizon * total_area
    max_depth_error = 0.0
    max_velocity = 0.0
    depth_precision_ok = True
    per_frame: dict[str, Any] = {}
    ledger_statuses: list[str] = []
    worst_budget_margin = -math.inf
    final_volume = final_volume_error = final_rounding_bound = 0.0
    for time_value in sorted(frames):
        frame = frames[time_value]
        volume = 0.0
        rounding_bound = 0.0
        frame_depth_error = 0.0
        frame_velocity = 0.0
        frame_depth_precision_ok = True
        exact_depth = rain_rate * time_value
        for i, j in spec.measured_cells:
            value = frame[((i + 0.5) * spec.dx, (j + 0.5) * spec.dx)]
            h_actual, u_actual, v_actual = value
            volume += h_actual * cell_area
            depth_rounding_bound = _decimal_quantization_bound(h_actual)
            rounding_bound += depth_rounding_bound * cell_area
            cell_error = abs(h_actual - exact_depth)
            frame_depth_error = max(frame_depth_error, cell_error)
            if not _depth_within_precision(h_actual, exact_depth):
                frame_depth_precision_ok = False
            frame_velocity = max(frame_velocity, abs(u_actual), abs(v_actual))
        expected_volume = exact_depth * total_area
        volume_error = volume - expected_volume
        max_depth_error = max(max_depth_error, frame_depth_error)
        max_velocity = max(max_velocity, frame_velocity)
        frame_budget = max(1.0e-6, expected_volume * 1.0e-8)
        ledger_state, margin = _ledger_classification(abs(volume_error), rounding_bound, frame_budget)
        worst_budget_margin = max(worst_budget_margin, margin)
        ledger_statuses.append(ledger_state)
        depth_precision_ok = depth_precision_ok and frame_depth_precision_ok
        per_frame[_number(time_value)] = {
            "observed_volume_m3": volume,
            "expected_volume_m3": expected_volume,
            "volume_error_m3": volume_error,
            "point_series_rounding_bound_m3": rounding_bound,
            "max_cell_depth_error_m": frame_depth_error,
            "depth_within_frame_precision_bound": frame_depth_precision_ok,
            "external_ledger_budget_m3": frame_budget,
            "external_ledger_worst_case_residual_m3": abs(volume_error) + rounding_bound,
            "external_ledger_margin_m3": margin,
            "external_ledger_status": ledger_state,
        }
        if time_value == spec.horizon:
            final_volume = volume
            final_volume_error = volume_error
            final_rounding_bound = rounding_bound
    text_depth_accuracy = depth_precision_ok
    velocity_pass = max_velocity <= 1.0e-10
    if "definite_failure_beyond_text_rounding_bound" in ledger_statuses:
        ledger_overall = "fail"
        ledger_pass: bool | None = False
    elif "inconclusive_rounding_interval_overlaps_budget" in ledger_statuses:
        ledger_overall = "inconclusive"
        ledger_pass = None
    else:
        ledger_overall = "pass"
        ledger_pass = True
    coverage_pass = coverage["every_fixed_step_saved"]
    increment = rain_rate * spec.timestep
    cutoff_loss_expected = 0.0 if increment > HE_CA else increment
    return {
        "saved_state_coverage": coverage,
        "observations": {
            "all_saved_states": {
                "max_cell_depth_error_from_Rt_m": max_depth_error,
                "depth_error_within_precision10_rounding_envelope": text_depth_accuracy,
                "max_absolute_velocity_component_m_s": max_velocity,
                "velocity_pass_1e-10_m_s": velocity_pass,
                "frame_count": len(per_frame),
                "final_observed_water_volume_m3": final_volume,
                "final_expected_rain_volume_m3": supplied_volume,
                "final_volume_error_m3": final_volume_error,
                "external_ledger_budget_m3": per_frame.get(_number(spec.horizon), {}).get("external_ledger_budget_m3"),
                "final_point_series_rounding_bound_m3": final_rounding_bound,
                "external_ledger_classification_all_saved_times": ledger_overall,
                "ledger_pass_requires_residual_plus_rounding_bound_within_budget": ledger_pass,
                "worst_budget_margin_m3": worst_budget_margin,
                "volume_by_frame": per_frame,
                "rain_increment_per_native_step_m": increment,
                "HE_CA_m": HE_CA,
                "increment_to_HE_CA_ratio": increment / HE_CA,
                "rainfall_cutoff_loss_assessment": "model_derived_expectation_only",
                "rainfall_cutoff_loss_m_per_cell_per_step_expected": cutoff_loss_expected,
                "rainfall_cutoff_observed_clipping_volume_m3": None,
            }
        },
        "depth_pass": text_depth_accuracy and coverage_pass,
        "velocity_pass": velocity_pass and coverage_pass,
        "rain_ledger_pass": ledger_pass is True and coverage_pass,
        "positive_control_pass": (
            True
            if text_depth_accuracy and velocity_pass and ledger_pass is True and coverage_pass
            else None
            if text_depth_accuracy and velocity_pass and ledger_pass is None and coverage_pass
            else False
        ),
        "actual_cumulative_applied_face_transfer": {
            "value": None,
            "status": "unsupported_by_unmodified_native_cli",
            "note": "Closed wall boundaries imply no intended external face transfer; native per-face internal/applied transfer series is not emitted.",
        },
        "positivity_and_clipping": {
            "published_state_series": "every fixed step; native specific-point output replaces h below HE_CA by zero",
            "intermediate_stage_minimum_depth": None,
            "clipping_correction_volume": None,
            "all_stage_positivity_status": "unsupported_by_unmodified_native_cli",
            "HE_CA_m": HE_CA,
            "VE_CA_m_s": HE_CA,
        },
    }


def _measure(spec: CaseSpec, frames: dict[float, dict[tuple[float, float], tuple[float, float, float]]]) -> dict[str, Any]:
    if spec.family == "lake-at-rest":
        return _measure_lake(spec, frames)
    if spec.family == "closed-flat-dry-rain":
        return _measure_rain(spec, frames)
    coverage = _saved_state_coverage(spec, frames)
    dt_steps = round(spec.horizon / spec.timestep)
    expected_count = dt_steps + 1
    observed_times = sorted(frames)
    time_tolerance = coverage["time_tolerance_s"]
    coverage_failures = coverage["missing_steps"]
    coverage_ok = coverage["every_fixed_step_saved"]
    observations: dict[str, Any] = {}
    xcoords = tuple((i + 0.5) * spec.dx for i in range(spec.nx))
    ycoords = tuple((j + 0.5) * spec.dx for j in range(spec.ny))
    for target_time in spec.expected_observation_times:
        nearest = min(observed_times, key=lambda t: abs(t - target_time))
        if abs(nearest - target_time) > time_tolerance:
            raise ReferenceError(f"requested observation {target_time}s is absent; nearest native frame is {nearest}s")
        frame = frames[nearest]
        max_h_rel = -1.0
        max_u_rel = -1.0
        max_h_cell: list[int] | None = None
        max_u_cell: list[int] | None = None
        max_h_abs = 0.0
        max_u_abs = 0.0
        for i, j in spec.measured_cells:
            key = (xcoords[i], ycoords[j])
            if key not in frame:
                raise ReferenceError(f"native frame {nearest}s lacks measured cell {key}")
            h_actual, u_actual, _v_actual = frame[key]
            h_expected = spec.reference_depths[i][j]
            if spec.family == "frictionless-accelerating-incline":
                u_expected = GRAVITY * float(spec.parameters["slope"]) * target_time
            else:
                u_expected = spec.reference_velocities[i][j]
            h_abs = abs(h_actual - h_expected)
            u_abs = abs(u_actual - u_expected)
            h_rel = h_abs / abs(h_expected) if h_expected else h_abs
            u_rel = u_abs / abs(u_expected) if u_expected else u_abs
            if h_rel > max_h_rel:
                max_h_rel, max_h_abs, max_h_cell = h_rel, h_abs, [i, j]
            if u_rel > max_u_rel:
                max_u_rel, max_u_abs, max_u_cell = u_rel, u_abs, [i, j]
        observations[str(target_time)] = {
            "native_frame_time_s": nearest,
            "measured_cell_count": len(spec.measured_cells),
            "depth_max_relative_error": max_h_rel,
            "depth_max_absolute_error_m": max_h_abs,
            "depth_error_at_ij": max_h_cell,
            "depth_pass_1pct": max_h_rel <= DEPTH_LIMIT,
            "velocity_max_relative_error": max_u_rel,
            "velocity_max_absolute_error_m_s": max_u_abs,
            "velocity_error_at_ij": max_u_cell,
            "velocity_pass_5pct": max_u_rel <= VELOCITY_LIMIT,
        }
    return {
        "saved_state_coverage": coverage,
        "observations": observations,
        "depth_pass": all(row["depth_pass_1pct"] for row in observations.values()) and coverage_ok,
        "velocity_pass": all(row["velocity_pass_5pct"] for row in observations.values()) and coverage_ok,
        "actual_cumulative_applied_face_transfer": {
            "value": None,
            "status": "unsupported_by_unmodified_native_cli",
            "required_limit_relative_error": 0.05,
            "note": "Native face arrays are held in solver memory; stock CLI does not emit every internal face flux or its Heun-weighted time integral. Center h*u is not used as a substitute.",
        },
        "positivity_and_clipping": {
            "published_state_series": "available at each fixed step; native output zeros values below HE_CA",
            "intermediate_stage_minimum_depth": None,
            "clipping_correction_volume": None,
            "all_stage_positivity_status": "unsupported_by_unmodified_native_cli",
            "HE_CA_m": HE_CA,
            "VE_CA_m_s": HE_CA,
        },
    }


def _write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")


def _case_reference(spec: CaseSpec) -> dict[str, Any]:
    if spec.family == "lake-at-rest":
        measurement_contract = {
            "times_s": "all saved fixed steps",
            "cells_ij": [list(cell) for cell in spec.measured_cells],
            "max_absolute_depth_drift_m": 1.0e-5,
            "max_absolute_velocity_component_m_s": 1.0e-5,
            "record_dry_rewet_count": True,
        }
    elif spec.family == "closed-flat-dry-rain":
        area = spec.nx * spec.ny * spec.dx * spec.dx
        rate = float(spec.parameters["rain_rate_m_s_float32"])
        measurement_contract = {
            "times_s": "all saved fixed steps",
            "cells_ij": [list(cell) for cell in spec.measured_cells],
            "exact_depth_m": "float32(R) * t",
            "bed_elevation_m": float(spec.parameters["bed_elevation_m"]),
            "external_ledger_budget_m3": f"max(1e-6, R*t*{area:g}*1e-8)",
            "rain_rate_m_s_float32": rate,
            "text_precision": "precision(10) observer; include summed rounding interval",
        }
    else:
        measurement_contract = {
            "times_s": list(spec.expected_observation_times),
            "cells_ij": [list(cell) for cell in spec.measured_cells],
            "max_depth_relative_error": DEPTH_LIMIT,
            "max_velocity_relative_error": VELOCITY_LIMIT,
            "max_actual_applied_face_transfer_relative_error": 0.05,
        }
    return {
        "case_name": spec.name,
        "family": spec.family,
        "reference_convention": spec.reference_description,
        "measurement_contract": measurement_contract,
        "depth_by_i_and_j": spec.reference_depths,
        "velocity_by_i_and_j": spec.reference_velocities,
        "case_parameters": spec.parameters,
        "dt_s": spec.timestep,
        "dx_dy_m": spec.dx,
        "nx_ny": [spec.nx, spec.ny],
        "horizon_s": spec.horizon,
    }


def run_case(
    spec: CaseSpec,
    repo_root: Path = REPO_ROOT,
    output_relative: str | None = None,
    study_root_override: Path | None = None,
) -> tuple[Path, dict[str, Any], int]:
    binding = verify_binding(repo_root)
    study_root = (
        _checked_path(repo_root, STUDY_REL.as_posix())
        if study_root_override is None
        else (study_root_override if study_root_override.is_absolute() else repo_root / study_root_override).resolve()
    )
    if not study_root.is_dir():
        raise ReferenceError(f"study root does not exist or is not a directory: {study_root}")
    output_relative = output_relative or f"native-runs/{spec.name}"
    case_dir = _fresh_output_path(study_root, output_relative)
    case_dir.mkdir(parents=True)
    input_hashes = _write_inputs(spec, case_dir, binding["source_root"])
    _write_json(case_dir / "reference-contract.json", _case_reference(spec))
    runner_path = Path(__file__).resolve()
    runner_snapshot = case_dir / "runner-source.py"
    runner_snapshot.write_bytes(runner_path.read_bytes())
    runner_sha256 = _sha256(runner_snapshot)
    started_at = dt.datetime.now(dt.timezone.utc).isoformat()
    provenance = {
        "case_name": spec.name,
        "probe_class": "initial_frozen_single_case_probe" if spec.exploratory else "frozen_native_reference_case",
        "created_at_utc": started_at,
        "repository_root": str(repo_root.resolve()),
        "study_root": str(study_root),
        "command": [str(binding["binary"])],
        "working_directory": str(case_dir),
        "runner_path": str(runner_path),
        "runner_snapshot_path": runner_snapshot.name,
        "runner_sha256": runner_sha256,
        "native_binary_path": str(binding["binary"]),
        "native_binary_sha256": binding["binary_sha256"],
        "release_cli_binary_path": str(binding["release_binary"]),
        "release_cli_binary_sha256": binding["release_binary_sha256"],
        "source_archive_sha256": binding["archive_sha256"],
        "source_tree_manifest_sha256": binding["source_manifest_sha256"],
        "verified_archive_source_file_count": binding["source_file_count"],
        "verified_build_copy_file_count": binding["build_file_count"],
        "case_input_sha256": input_hashes,
        "source_license_note": "CeCILL-V2 reference executable used as a standalone external program; no FullSWOF source is copied into Cubey product source.",
    }
    _write_json(case_dir / "run-provenance.json", provenance)
    started = time.perf_counter()
    native_error: str | None = None
    native_returncode = 124
    timeout_s = max(60.0, round(spec.horizon / spec.timestep) * 0.5)
    try:
        completed = subprocess.run(
            [str(binding["binary"])],
            cwd=case_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=timeout_s,
        )
        native_returncode = completed.returncode
        native_stdout = completed.stdout
        native_stderr = completed.stderr
    except subprocess.TimeoutExpired as exc:
        native_error = f"native CLI exceeded case timeout of {timeout_s:.1f}s"
        native_stdout = exc.stdout or b""
        native_stderr = exc.stderr or b""
        if isinstance(native_stdout, str):
            native_stdout = native_stdout.encode("utf-8", errors="replace")
        if isinstance(native_stderr, str):
            native_stderr = native_stderr.encode("utf-8", errors="replace")
    except OSError as exc:
        native_error = f"failed to execute the source-bound native binary: {exc}"
        native_stdout = b""
        native_stderr = str(exc).encode("utf-8", errors="replace")
    elapsed = time.perf_counter() - started
    (case_dir / "native.stdout.bin").write_bytes(native_stdout)
    (case_dir / "native.stderr.bin").write_bytes(native_stderr)
    stdout_sha = _sha256(case_dir / "native.stdout.bin")
    stderr_sha = _sha256(case_dir / "native.stderr.bin")
    native_output_hashes = {
        str(path.relative_to(case_dir)): _sha256(path)
        for path in sorted((case_dir / "Outputs").rglob("*"))
        if path.is_file()
    } if (case_dir / "Outputs").is_dir() else {}
    post_binding_error: str | None = None
    try:
        post_binding = verify_binding(repo_root)
        for field in ("archive_sha256", "source_manifest_sha256", "binary_sha256", "release_binary_sha256"):
            if post_binding[field] != binding[field]:
                raise ReferenceError(f"native {field} changed during case execution")
        post_input_hashes = {
            path.name: _sha256(path)
            for path in sorted((case_dir / "Inputs").iterdir())
            if path.is_file()
        }
        if post_input_hashes != input_hashes:
            raise ReferenceError("case inputs changed during native execution")
    except (OSError, ValueError, ReferenceError) as exc:
        post_binding_error = str(exc)
    frames: dict[float, dict[tuple[float, float], tuple[float, float, float]]] = {}
    parse_error: str | None = None
    if native_returncode == 0 and native_error is None and post_binding_error is None:
        try:
            frames = parse_specific_points(
                case_dir / "Outputs/hu_specific_points.dat",
                spec.nx * spec.ny,
                _point_coordinates(spec),
            )
        except (OSError, ValueError, ReferenceError) as exc:
            parse_error = str(exc)
    measurements: dict[str, Any] | None = None
    if native_returncode == 0 and parse_error is None and post_binding_error is None:
        try:
            measurements = _measure(spec, frames)
        except (OSError, ValueError, ReferenceError) as exc:
            parse_error = str(exc)
    accepted_depth_velocity = bool(measurements and measurements["depth_pass"] and measurements["velocity_pass"])
    case_checks_value = measurements.get("positive_control_pass", accepted_depth_velocity) if measurements else False
    case_checks_pass = bool(case_checks_value)
    report = {
        "case_name": spec.name,
        "family": spec.family,
        "probe_class": provenance["probe_class"],
        "native_exit_code": native_returncode,
        "native_wall_seconds": elapsed,
        "native_error": native_error,
        "post_run_binding_error": post_binding_error,
        "raw_stdout_sha256": stdout_sha,
        "raw_stderr_sha256": stderr_sha,
        "raw_stdout_path": "native.stdout.bin",
        "raw_stderr_path": "native.stderr.bin",
        "native_output_sha256": native_output_hashes,
        "parse_error": parse_error,
        "input_sha256": input_hashes,
        "native_output_precision": "10 significant decimal digits in DEBUG all-cell specific-point series; the observer build uses -O3 and adds -DDEBUG to unchanged upstream source. Gnuplot snapshots use native round5 plus stream default precision and are not used for accuracy measurements.",
        "case_parameters": spec.parameters,
        "measurements": measurements,
        "depth_and_velocity_essential_checks_pass": accepted_depth_velocity if measurements else False,
        "case_specific_checks_pass": case_checks_pass,
        "case_specific_checks_inconclusive": case_checks_value is None,
        "positive_control_pass": measurements.get("positive_control_pass") if measurements else None,
        "full_accuracy_gate_pass": False,
        "full_accuracy_gate_status": (
            "inconclusive_case_specific_measurement"
            if measurements and case_checks_value is None
            else
            "rejected_case_specific_measurement"
            if measurements and not case_checks_pass
            else "incomplete_actual_face_transfer_unavailable"
            if measurements
            else "native_run_or_parse_failed"
        ),
        "exploratory_initial_probe": spec.exploratory,
    }
    _write_json(case_dir / "report.json", report)
    if native_returncode != 0 or native_error is not None or post_binding_error is not None:
        exit_code = 1
    elif parse_error is not None or measurements is None:
        exit_code = 1
    elif case_checks_value is None:
        exit_code = 4
    elif not case_checks_pass:
        exit_code = 2
    else:
        exit_code = 3
    return case_dir, report, exit_code


def _arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=sorted(available_cases()))
    parser.add_argument("--output", help="fresh leaf relative to the V6 evidence root")
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT, help="checkout root; defaults to the parent of this runner")
    parser.add_argument("--study-root", type=Path, help="existing evidence root; defaults to outputs/fluid/hillside-rain-v6-fullswof-20261002-Po5ckP")
    parser.add_argument("--initial-probe", action="store_true", help="mark this separately retained first-case probe as exploratory")
    parser.add_argument("--list-cases", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = _arg_parser()
    args = parser.parse_args(argv)
    if args.list_cases:
        for name in sorted(available_cases()):
            print(name)
        return 0
    if args.case is None:
        parser.error("--case is required unless --list-cases is used")
    spec = available_cases()[args.case]
    if args.initial_probe:
        spec = CaseSpec(**{**spec.__dict__, "exploratory": True})
    try:
        case_dir, report, result = run_case(
            spec,
            repo_root=args.repo_root.resolve(),
            output_relative=args.output,
            study_root_override=args.study_root,
        )
    except (OSError, ValueError, ReferenceError) as exc:
        print(f"FullSWOF V6 case failed before acceptance: {exc}", file=sys.stderr)
        return 1
    print(json.dumps({"case_dir": str(case_dir), "verdict": report["full_accuracy_gate_status"], "exit_code": result}, sort_keys=True))
    return result


if __name__ == "__main__":
    raise SystemExit(main())
