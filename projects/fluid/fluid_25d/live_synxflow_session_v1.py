#!/usr/bin/env python3
"""Run one pinned SynxFlow case and publish an atomic growing Cubey stream.

The launcher owns the foreground child process. It copies only the audited
case specification, protocol, source DEM, and native input tree from a
completed baseline case. Native output is always fresh for this session.
"""

from __future__ import annotations

import argparse
from array import array
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import stat
import subprocess
import sys
import time
import traceback
from typing import Any
import uuid

import convert_synxflow_recording_v1 as converter


ROOT = Path(__file__).resolve().parents[3]
PINNED_PYTHON = (
    ROOT
    / "outputs/fluid/native-runoff-reuse-v1-20261002-trm3Ve/"
    "synxflow-setup/env/.venv/bin/python"
)
PINNED_PROTOCOL = ROOT / "outputs/fluid/native-rain-recession-v1-20261003-1ZMqTJ/protocol.json"
STREAM_SCHEMA = "cubey.fluid25d.stream.v1"
SESSION_SCHEMA = "cubey.fluid25d.live_synxflow_session.v1"
CASE_RESULT_SCHEMA = SESSION_SCHEMA + ".case-result"
PRE_SOLVER_AUDIT_SCHEMA = SESSION_SCHEMA + ".pre-solver-audit"
TIMING_SCHEMA = SESSION_SCHEMA + ".timing"
RESULT_SCHEMA = SESSION_SCHEMA + ".result"
ENCODING = converter.ENCODING
FIELDS = converter.FIELDS

# The accepted 512x512, 241-frame reference fits under these limits even with
# conservative native ASCII expansion. Limits are checked before child launch
# and against actual session growth while the child is running.
MAX_LIVE_FRAMES = 2048
MAX_LIVE_CELLS = converter.MAX_CELLS
MAX_ASC_BYTES_PER_CELL_FIELD = 32
MAX_SESSION_BYTES = 8 * 1024**3
MINIMUM_FREE_BYTES = 1 * 1024**3
MAX_SESSION_FILES = 20_000
MAX_JSON_BYTES = 32 * 1024**2
MAX_AUDIT_JSON_BYTES = 16 * 1024**2
POLL_INTERVAL_S = 0.1
MANIFEST_MIN_INTERVAL_S = 0.25
CHILD_STOP_GRACE_S = 15.0
SUCCESS_MARKER = "Simulation successfully finished!"

_FIELD_PATTERNS = {
    "h": re.compile(r"h_(0|[1-9][0-9]*)\.asc\Z"),
    "hUx": re.compile(r"hUx_(0|[1-9][0-9]*)\.asc\Z"),
    "hUy": re.compile(r"hUy_(0|[1-9][0-9]*)\.asc\Z"),
}
_MAX_DEPTH_PATTERN = re.compile(r"h_max_(0|[1-9][0-9]*)\.asc\Z")


class LiveSessionError(RuntimeError):
    """A baseline, native output, or live publication failed validation."""


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _sha256_bytes(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def _json_bytes(value: Any) -> bytes:
    try:
        return (json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n").encode("utf-8")
    except (TypeError, ValueError) as error:
        raise LiveSessionError(f"cannot serialize finite session JSON: {error}") from error


def _atomic_json(path: Path, value: Any, *, replace: bool, limit: int = MAX_JSON_BYTES) -> str:
    raw = _json_bytes(value)
    if len(raw) > limit:
        raise LiveSessionError(f"JSON document exceeds {limit} byte limit: {path.name}")
    path.parent.mkdir(parents=True, exist_ok=True)
    if replace and os.path.lexists(path):
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode):
            raise LiveSessionError(f"refusing to replace non-regular JSON target: {path}")
    if not replace and os.path.lexists(path):
        raise LiveSessionError(f"refusing to overwrite existing file: {path}")
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.pending")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    descriptor = os.open(temporary, flags, 0o600)
    try:
        with os.fdopen(descriptor, "wb", closefd=True) as stream:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        if replace:
            os.replace(temporary, path)
        else:
            # link() publishes only a complete file and fails if the name exists.
            os.link(temporary, path, follow_symlinks=False)
            temporary.unlink()
        _fsync_directory(path.parent)
    except Exception:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise
    return _sha256_bytes(raw)


def _atomic_new_bytes(path: Path, raw: bytes) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    if os.path.lexists(path):
        raise LiveSessionError(f"refusing to overwrite published payload: {path}")
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.pending")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    descriptor = os.open(temporary, flags, 0o600)
    try:
        with os.fdopen(descriptor, "wb", closefd=True) as stream:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temporary, path, follow_symlinks=False)
        temporary.unlink()
        _fsync_directory(path.parent)
    except Exception:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise
    return _sha256_bytes(raw)


def _fsync_directory(path: Path) -> None:
    try:
        descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    except OSError:
        return
    try:
        os.fsync(descriptor)
    except OSError:
        pass
    finally:
        os.close(descriptor)


def _read_json(path: Path, label: str, limit: int = MAX_AUDIT_JSON_BYTES) -> tuple[dict[str, Any], bytes]:
    _require_regular(path, label)
    size = path.stat().st_size
    if size > limit:
        raise LiveSessionError(f"{label} exceeds {limit} byte limit")
    raw = path.read_bytes()
    try:
        value = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise LiveSessionError(f"invalid {label}: {error}") from error
    if not isinstance(value, dict):
        raise LiveSessionError(f"{label} must be a JSON object")
    return value, raw


def _require_regular(path: Path, label: str) -> os.stat_result:
    try:
        info = path.lstat()
    except OSError as error:
        raise LiveSessionError(f"cannot inspect {label}: {path}: {error}") from error
    if not stat.S_ISREG(info.st_mode):
        raise LiveSessionError(f"{label} must be a regular non-symlink file: {path}")
    return info


def _require_directory(path: Path, label: str) -> None:
    try:
        info = path.lstat()
    except OSError as error:
        raise LiveSessionError(f"cannot inspect {label}: {path}: {error}") from error
    if not stat.S_ISDIR(info.st_mode):
        raise LiveSessionError(f"{label} must be a real directory, not a symlink: {path}")


def _checked_child(root: Path, child: Path, label: str) -> Path:
    root_real = root.resolve(strict=True)
    child_real = child.resolve(strict=True)
    if child_real == root_real or root_real not in child_real.parents:
        raise LiveSessionError(f"{label} escapes its parent directory: {child}")
    return child_real


def _safe_tree_hashes(
    root: Path,
    *,
    max_files: int = MAX_SESSION_FILES,
    max_bytes: int = MAX_SESSION_BYTES,
) -> tuple[dict[str, str], int, int]:
    _require_directory(root, "tree root")
    root_real = root.resolve(strict=True)
    hashes: dict[str, str] = {}
    total_bytes = 0
    stack = [root]
    while stack:
        directory = stack.pop()
        with os.scandir(directory) as entries:
            for entry in entries:
                path = Path(entry.path)
                if entry.is_symlink():
                    raise LiveSessionError(f"symlink is not allowed in audited tree: {path}")
                if entry.is_dir(follow_symlinks=False):
                    if path.resolve(strict=True) != root_real and root_real not in path.resolve(strict=True).parents:
                        raise LiveSessionError(f"directory escapes audited tree: {path}")
                    stack.append(path)
                    continue
                if not entry.is_file(follow_symlinks=False):
                    raise LiveSessionError(f"special file is not allowed in audited tree: {path}")
                if len(hashes) >= max_files:
                    raise LiveSessionError(f"tree exceeds {max_files} file limit")
                relative = path.relative_to(root).as_posix()
                size = entry.stat(follow_symlinks=False).st_size
                total_bytes += size
                if total_bytes > max_bytes:
                    raise LiveSessionError(f"tree exceeds {max_bytes} byte limit")
                hashes[relative] = _sha256_file(path)
    return dict(sorted(hashes.items())), total_bytes, len(hashes)


def _safe_tree_size(root: Path, *, max_files: int = MAX_SESSION_FILES, max_bytes: int = MAX_SESSION_BYTES) -> tuple[int, int]:
    _require_directory(root, "session root")
    total_bytes = 0
    file_count = 0
    stack = [root]
    while stack:
        directory = stack.pop()
        with os.scandir(directory) as entries:
            for entry in entries:
                if entry.is_symlink():
                    raise LiveSessionError(f"session contains a symlink: {entry.path}")
                if entry.is_dir(follow_symlinks=False):
                    stack.append(Path(entry.path))
                elif entry.is_file(follow_symlinks=False):
                    total_bytes += entry.stat(follow_symlinks=False).st_size
                    file_count += 1
                    if file_count > max_files:
                        raise LiveSessionError(f"tree exceeds {max_files} file limit")
                    if total_bytes > max_bytes:
                        raise LiveSessionError(f"tree exceeds {max_bytes} byte limit")
                else:
                    raise LiveSessionError(f"session contains a special file: {entry.path}")
    return total_bytes, file_count


def _load_pinned_identity(verify: bool) -> dict[str, Any]:
    protocol, raw = _read_json(PINNED_PROTOCOL, "pinned SynxFlow protocol")
    if protocol.get("schema") != "cubey.fluid25d.native_rain_recession.v1.protocol":
        raise LiveSessionError("pinned SynxFlow protocol schema is not recognized")
    identity = protocol.get("native_backend_identity")
    payload = protocol.get("installed_synxflow_payload_files")
    if not isinstance(identity, dict) or not isinstance(payload, dict) or not payload:
        raise LiveSessionError("pinned protocol omits native backend identity or payload hashes")
    if identity.get("extension_sha256") not in set(payload.values()):
        raise LiveSessionError("pinned extension identity is not listed in installed payload hashes")
    if verify:
        for name, expected in payload.items():
            if not isinstance(name, str) or not isinstance(expected, str):
                raise LiveSessionError("pinned payload hash map is malformed")
            path = Path(name)
            _require_regular(path, "pinned SynxFlow payload")
            if _sha256_file(path) != expected:
                raise LiveSessionError(f"pinned SynxFlow payload drift: {path}")
    return {
        "native_backend_identity": identity,
        "installed_synxflow_payload_sha256": hashlib.sha256(
            json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
        ).hexdigest(),
        "pinned_protocol_sha256": _sha256_bytes(raw),
    }


def _completed_baseline(case: Path) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any], dict[str, str], int]:
    if case.is_symlink():
        raise LiveSessionError(f"baseline case may not be a symlink: {case}")
    case = case.resolve(strict=True)
    _require_directory(case, "baseline case")
    for name in ("case-spec.json", "case-protocol.json", "case-result.json", "DEM.asc"):
        _require_regular(case / name, f"baseline {name}")
    _require_directory(case / "native", "baseline native directory")
    _require_directory(case / "native/input", "baseline native input")
    _require_directory(case / "native/output", "baseline native output")

    validated = converter._validate_case(case)
    (spec, case_protocol, dem_header, _source_values, spec_sha, protocol_sha,
     _result_sha, dem_sha, _numeric_bed, bed_sha, times, width, height,
     cell_size, _rainfall_proof) = validated
    frame_count = len(times)
    if frame_count < 1 or frame_count > MAX_LIVE_FRAMES:
        raise LiveSessionError(f"case frame count {frame_count} exceeds live limit {MAX_LIVE_FRAMES}")
    if width * height > MAX_LIVE_CELLS:
        raise LiveSessionError("case grid exceeds live cell limit")

    case_result, _ = _read_json(case / "case-result.json", "baseline case result")
    if case_result.get("case") != spec or case_result.get("status") not in {"healthy", "pass", "completed"}:
        raise LiveSessionError("baseline is not an audited completed case")
    if case_result.get("process_exit_code") not in (None, 0) or case_result.get("process_timeout") is True:
        raise LiveSessionError("baseline case result does not prove native success")
    if case_result.get("native_gpu_success_marker") is False:
        raise LiveSessionError("baseline case result records a missing native success marker")
    provenance = case_result.get("input_provenance")
    if not isinstance(provenance, dict):
        raise LiveSessionError("baseline case result lacks input provenance")
    if provenance.get("native_inputs_unchanged_during_run") is False:
        raise LiveSessionError("baseline native input audit records input drift")

    input_hashes, input_bytes, _ = _safe_tree_hashes(case / "native/input")
    audit = provenance.get("native_input_audit_before_solver")
    if not isinstance(audit, dict):
        preflight_path = case / "native-preflight.json"
        if os.path.lexists(preflight_path):
            audit, _ = _read_json(preflight_path, "baseline native preflight")
        else:
            raise LiveSessionError("baseline omits its native pre-solver audit")
    preflight_path = case / "native-preflight.json"
    if os.path.lexists(preflight_path):
        preflight, _ = _read_json(preflight_path, "baseline native preflight")
        audited_hashes = preflight.get("native_input_sha256", audit.get("native_input_sha256"))
    else:
        preflight = {}
        audited_hashes = audit.get("native_input_sha256")
    if audited_hashes is None or audited_hashes != input_hashes:
        raise LiveSessionError("baseline native input files differ from or omit their pre-solver hash audit")
    postrun_path = case / "native-postrun.json"
    if os.path.lexists(postrun_path):
        postrun, _ = _read_json(postrun_path, "baseline native postrun audit")
        post_hashes = postrun.get("input_sha256_after_run")
        if post_hashes is not None and post_hashes != input_hashes:
            raise LiveSessionError("baseline native input files differ from their postrun audit")

    # A completed converter-valid baseline also needs a complete regular raw
    # frame set, not just a copied success label. New frames are parsed with
    # the converter reader before each live publication.
    frame_paths, _ = converter._collect_frame_paths(case / "native/output", times)
    del frame_paths
    native_output_bytes, _ = _safe_tree_size(case / "native/output")
    return {
        "spec": spec,
        "case_protocol": case_protocol,
        "dem_header": dem_header,
        "spec_sha256": spec_sha,
        "case_protocol_sha256": protocol_sha,
        "dem_asc_sha256": dem_sha,
        "native_numerical_bed_sha256": bed_sha,
        "times": times,
        "width": width,
        "height": height,
        "cell_size_m": cell_size,
        "baseline_input_hashes": input_hashes,
        "baseline_input_bytes": input_bytes,
        "baseline_native_output_bytes": native_output_bytes,
        "baseline_preflight": preflight,
        "baseline_case_result_sha256": _sha256_file(case / "case-result.json"),
    }, case_result, audit, input_hashes, native_output_bytes


def _validate_output_budget(
    metadata: dict[str, Any], parent: Path, minimum_free_bytes: int, disk_usage=shutil.disk_usage
) -> dict[str, int]:
    cells = metadata["width"] * metadata["height"]
    count = len(metadata["times"])
    payload_bytes = cells * len(FIELDS) * 4 * count
    ascii_field_bound = cells * count * len(_FIELD_PATTERNS) * MAX_ASC_BYTES_PER_CELL_FIELD
    baseline_growth_bound = metadata["baseline_native_output_bytes"] * 2
    native_output_bound = max(ascii_field_bound, baseline_growth_bound)
    fixed_overhead = metadata["baseline_input_bytes"] + 64 * 1024**2
    projected_session_bytes = payload_bytes + native_output_bound + fixed_overhead
    if projected_session_bytes > MAX_SESSION_BYTES:
        raise LiveSessionError(
            f"projected session storage {projected_session_bytes} exceeds {MAX_SESSION_BYTES} byte cap"
        )
    free = disk_usage(parent).free
    required = projected_session_bytes + minimum_free_bytes
    if free < required:
        raise LiveSessionError(
            f"insufficient free space for bounded session: have {free}, require {required} bytes"
        )
    return {
        "projected_session_bytes": projected_session_bytes,
        "payload_bytes": payload_bytes,
        "native_output_bytes_bound": native_output_bound,
        "minimum_free_bytes": minimum_free_bytes,
        "free_bytes_at_preflight": free,
        "session_cap_bytes": MAX_SESSION_BYTES,
    }


def _rainfall_history_proof_from_audit(
    case_dir: Path, spec: dict[str, Any], case_protocol: dict[str, Any], audit: dict[str, Any]
) -> dict[str, Any] | None:
    if "rainfall_history" not in spec:
        return None
    duration = converter._finite_number(spec.get("duration_s"), "case-spec duration_s")
    initial = converter._finite_number(spec.get("rain_rate_m_per_s"), "case-spec rain_rate_m_per_s")
    history = converter._parse_rainfall_history(
        spec["rainfall_history"], duration, initial, "case-spec rainfall_history"
    )
    settings = case_protocol.get("settings")
    if not isinstance(settings, dict):
        raise LiveSessionError("case protocol has no rainfall history settings")
    converter._rain_histories_match(
        settings.get("rain_source_schedule"), history, "case-protocol rainfall schedule"
    )
    native_source = case_dir / "native/input/field/precipitation_source_all.dat"
    serialized, source_hash = converter._read_serialized_rainfall_history(native_source)
    converter._rain_histories_match(serialized, history, "serialized native rainfall input")
    if audit.get("rain_source_sha256") != source_hash:
        raise LiveSessionError("native rainfall source differs from pre-solver audit")
    converter._rain_histories_match(
        audit.get("serialized_rainfall_history"), history, "pre-solver rainfall history audit"
    )
    cumulative_m = 0.0
    for (t0, r0), (t1, r1) in zip(history, history[1:]):
        cumulative_m += (r0 + r1) * 0.5 * (t1 - t0)
        if not math.isfinite(cumulative_m) or not math.isfinite(cumulative_m * 1000.0):
            raise LiveSessionError("scheduled rainfall integral is outside finite range")
    return {
        "history_m_per_s": history,
        "native_source_path": "native/input/field/precipitation_source_all.dat",
        "native_source_sha256": source_hash,
        "serialized_history_tolerances": {
            "time_absolute_s": 1.0e-9,
            "rate_relative": converter.RAIN_HISTORY_REL_TOL,
            "rate_absolute_m_per_s": converter.RAIN_HISTORY_ABS_TOL,
        },
        "piecewise_linear_cumulative_scheduled_rain_mm": cumulative_m * 1000.0,
        "not_a_measured_water_balance": True,
    }


def _prepare_case_copy(baseline: Path, case_dir: Path, metadata: dict[str, Any]) -> None:
    case_dir.mkdir(mode=0o700)
    for name in ("case-spec.json", "case-protocol.json", "DEM.asc"):
        source = baseline / name
        target = case_dir / name
        if os.path.lexists(target):
            raise LiveSessionError(f"refusing to overwrite fresh session case file: {target}")
        shutil.copyfile(source, target, follow_symlinks=False)
    native_root = case_dir / "native"
    native_root.mkdir(mode=0o700)
    input_target = native_root / "input"
    source_input = baseline / "native/input"
    input_target.mkdir(mode=0o700)
    for relative in metadata["baseline_input_hashes"]:
        source = source_input / relative
        target = input_target / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target, follow_symlinks=False)
    (native_root / "output").mkdir(mode=0o700)
    copied_hashes, _, _ = _safe_tree_hashes(input_target)
    if copied_hashes != metadata["baseline_input_hashes"]:
        raise LiveSessionError("copied native input differs from audited baseline input")
    for name in ("case-spec.json", "case-protocol.json", "DEM.asc"):
        if _sha256_file(case_dir / name) != _sha256_file(baseline / name):
            raise LiveSessionError(f"copied {name} differs from audited baseline")


def _now_pair() -> tuple[float, float]:
    unix_s = time.time()
    mono_s = time.monotonic()
    if not math.isfinite(unix_s) or unix_s <= 0 or not math.isfinite(mono_s) or mono_s < 0:
        raise LiveSessionError("system clock returned an invalid session timestamp")
    return unix_s, mono_s


def _iso_utc(unix_s: float) -> str:
    return datetime.fromtimestamp(unix_s, timezone.utc).isoformat().replace("+00:00", "Z")


class LiveSession:
    """Prepared private native case plus a single-frame-at-a-time publisher."""

    def __init__(
        self,
        baseline: Path,
        out: Path,
        python: Path,
        *,
        verify_backend: bool = True,
        minimum_free_bytes: int = MINIMUM_FREE_BYTES,
        disk_usage=shutil.disk_usage,
    ) -> None:
        self.baseline = baseline.expanduser().absolute()
        self.out = out.expanduser().absolute()
        self.python = python.expanduser().absolute()
        if self.baseline.is_symlink():
            raise LiveSessionError(f"baseline case may not be a symlink: {self.baseline}")
        self.baseline = self.baseline.resolve(strict=True)
        if os.path.lexists(self.out):
            raise LiveSessionError(f"output already exists; refusing to overwrite: {self.out}")
        try:
            resolved_python = self.python.resolve(strict=True)
            pinned_python = PINNED_PYTHON.resolve(strict=True)
        except OSError as error:
            raise LiveSessionError(f"--python must resolve to the pinned venv interpreter: {self.python}") from error
        if not resolved_python.is_file() or not os.access(self.python, os.X_OK):
            raise LiveSessionError(f"--python must be an existing executable file: {self.python}")
        if resolved_python != pinned_python:
            raise LiveSessionError(f"--python must name the pinned SynxFlow venv interpreter: {PINNED_PYTHON}")

        self.metadata, self.baseline_result, self.baseline_audit, self.input_hashes, _ = _completed_baseline(self.baseline)
        if not self.out.parent.exists():
            self.out.parent.mkdir(parents=True, exist_ok=True)
        parent = self.out.parent.resolve(strict=True)
        self.budget = _validate_output_budget(self.metadata, parent, minimum_free_bytes, disk_usage)
        # Tests may inject a known fake identity; production always rehashes the
        # pinned wheel payload before launch.
        self.pinned_identity = _load_pinned_identity(verify_backend)
        self.case_dir = self.out / "case"
        self.output_dir = self.case_dir / "native/output"
        self.session_id = uuid.uuid4().hex
        self.pid = os.getpid()
        self.frames: list[dict[str, Any]] = []
        self.raw_hashes: list[dict[str, Any]] = []
        self.frame_timings: list[dict[str, Any]] = []
        self.revision = -1
        self.latest_native_time_s = 0.0
        self.native_running = False
        self.native_call_started = False
        self._last_manifest_update_mono = 0.0
        self._manifest_native_time = -1.0
        self._manifest_native_running: bool | None = None
        self._seen_h_times: set[int] = set()
        self._seen_export_paths: set[str] = set()
        self._published_raw_stats: dict[str, tuple[int, int, int]] = {}
        self._observed_h: dict[int, tuple[float, float]] = {}
        self._observed_barrier: dict[int, tuple[float, float]] = {}
        self._log_offset = 0
        self._log_partial = b""
        self._timelog_latest = 0.0
        self._child_started_unix_s: float | None = None
        self._child_started_monotonic_s: float | None = None
        self._child_exit_observed: tuple[float, float] | None = None
        self._session_started_unix_s, self._session_started_monotonic_s = _now_pair()
        self._native_call_timing: dict[str, Any] | None = None
        self._completed_auxiliary: dict[str, Any] | None = None
        self._child_stopped_by_launcher = False
        self._manifest_static: dict[str, Any] | None = None
        self._result_written = False
        self._initial_payload_hashes: dict[str, str] = {}
        self._pre_solver_audit: dict[str, Any] | None = None
        self._pre_solver_audit_sha256: str | None = None
        self._case_spec_sha256 = self.metadata["spec_sha256"]
        self._case_protocol_sha256 = self.metadata["case_protocol_sha256"]
        self._dem_sha256 = self.metadata["dem_asc_sha256"]
        self._native_bed_sha256 = self.metadata["native_numerical_bed_sha256"]
        try:
            self._initialize()
        except Exception as error:
            if os.path.lexists(self.out) and self.out.is_dir() and not os.path.islink(self.out):
                try:
                    _atomic_json(self.out / "session-result.json", {
                        "schema": RESULT_SCHEMA,
                        "session_id": self.session_id,
                        "state": "failed",
                        "message": f"{type(error).__name__}: {error}",
                        "pid": self.pid,
                        "frames_published": 0,
                        "expected_frames": len(self.metadata["times"]),
                    }, replace=False, limit=MAX_AUDIT_JSON_BYTES)
                except Exception:
                    pass
            raise

    def _initialize(self) -> None:
        self.out.mkdir(mode=0o700)
        self.case_dir.parent.mkdir(parents=True, exist_ok=True)
        _prepare_case_copy(self.baseline, self.case_dir, self.metadata)

        case_validated = converter._validate_case(self.baseline)
        (_spec, case_protocol, dem_header, source_values, _spec_sha, _protocol_sha,
         _result_sha, _dem_sha, numerical_bed, _bed_sha, times, width, height,
         cell_size, _rainfall) = case_validated
        source_bed = converter._as_float32(source_values, "source DEM bed")
        bed_error = [float(native) - float(source) for native, source in zip(numerical_bed, source_bed, strict=True)]
        max_bed_error = max(abs(value) for value in bed_error)
        rms_bed_error = math.sqrt(sum(value * value for value in bed_error) / len(bed_error))
        bed_raw = converter._little_endian_bytes(numerical_bed)
        source_bed_raw = converter._little_endian_bytes(source_bed)
        bed_digest = _atomic_new_bytes(self.out / "bed.f32", bed_raw)
        source_bed_digest = _atomic_new_bytes(self.out / "source-bed.f32", source_bed_raw)

        self._manifest_static = {
            "schema": STREAM_SCHEMA,
            "encoding": ENCODING,
            "fields": list(FIELDS),
            "field_layout": "planar",
            "grid": {
                "width": width,
                "height": height,
                "cell_size_m": cell_size,
                "storage_order": "row-major",
                "row_direction": "world-z-positive",
                "sample_location": "cell-center",
            },
            "bed": {"path": "bed.f32", "sha256": bed_digest},
            "source_bed": {"path": "source-bed.f32", "sha256": source_bed_digest},
            "protocol": self.metadata["spec"],
            "provenance": {
                "source_sha256": {
                    "case_spec": self._case_spec_sha256,
                    "case_protocol": self._case_protocol_sha256,
                    "dem_asc": self._dem_sha256,
                    "native_numerical_bed": self._native_bed_sha256,
                },
                "terrain_source_identity": converter._terrain_identity(case_protocol),
                "precision": {
                    "native_numerical_bed_format": "%g (six significant digits)",
                    "native_bed_vs_source_bed_max_abs_error_m": max_bed_error,
                    "native_bed_vs_source_bed_rms_error_m": rms_bed_error,
                    "native_output_ascii_decimal_places": 6,
                    "native_output_ascii_rounding_half_step": 0.0000005,
                    "native_world_z_momentum_sign": -1,
                },
            },
            "session_id": self.session_id,
            "revision": 0,
            "producer": {
                "state": "running",
                "native_running": False,
                "pid": self.pid,
                "latest_native_time_s": 0.0,
                "message": "Preparing pinned SynxFlow child and waiting for the first complete export.",
            },
            "frames": [],
        }

        input_hashes, input_bytes, file_count = _safe_tree_hashes(self.case_dir / "native/input")
        if input_hashes != self.metadata["baseline_input_hashes"]:
            raise LiveSessionError("session native inputs changed during copy")
        z_path = self.case_dir / "native/input/field/z.dat"
        _require_regular(z_path, "native numerical bed")
        numeric_bed, bed_sha = converter._read_native_bed(
            z_path, self.metadata["width"], self.metadata["height"]
        )
        if bed_sha != self._native_bed_sha256:
            raise LiveSessionError("copied native numerical bed differs from baseline")
        audit: dict[str, Any] = {
            "schema": PRE_SOLVER_AUDIT_SCHEMA,
            "source": "fresh session copy of the audited completed baseline input tree",
            "baseline_case_result_sha256": self.metadata["baseline_case_result_sha256"],
            "native_input_sha256": input_hashes,
            "native_input_bytes": input_bytes,
            "native_input_file_count": file_count,
            "z_field_sha256": bed_sha,
            "z_element_count": self.metadata["width"] * self.metadata["height"],
            "case_dem_ascii_sha256": self._dem_sha256,
        }
        if "rainfall_history" in self.metadata["spec"]:
            rain_path = self.case_dir / "native/input/field/precipitation_source_all.dat"
            rows, source_hash = converter._read_serialized_rainfall_history(rain_path)
            audit["rain_source_sha256"] = source_hash
            audit["serialized_rainfall_history"] = rows
        self._pre_solver_audit = audit
        self._pre_solver_audit_sha256 = _atomic_json(
            self.case_dir / "native-preflight.json", audit, replace=False, limit=MAX_AUDIT_JSON_BYTES
        )
        self._initial_payload_hashes = {
            "bed.f32": bed_digest,
            "source-bed.f32": source_bed_digest,
        }
        proof = _rainfall_history_proof_from_audit(
            self.case_dir, self.metadata["spec"], case_protocol, audit
        )
        if proof is not None:
            self._manifest_static["provenance"]["rainfall_history_proof"] = proof
        self._manifest_static["provenance"]["source_sha256"]["pre_solver_audit"] = self._pre_solver_audit_sha256

        _atomic_json(
            self.case_dir / "harness-provenance.json",
            {
                "schema": SESSION_SCHEMA + ".harness-provenance",
                "session_id": self.session_id,
                "launcher_path": str(Path(__file__).resolve()),
                "launcher_sha256": _sha256_file(Path(__file__).resolve()),
                "baseline_case": str(self.baseline),
                "baseline_case_result_sha256": self.metadata["baseline_case_result_sha256"],
                "pinned_python": str(self.python),
                "pinned_native_backend_identity": self.pinned_identity["native_backend_identity"],
                "pinned_protocol_sha256": self.pinned_identity["pinned_protocol_sha256"],
                "clone_scope": ["case-spec.json", "case-protocol.json", "DEM.asc", "native/input/**"],
                "native_output_started_empty": True,
            },
            replace=False,
            limit=MAX_AUDIT_JSON_BYTES,
        )
        del source_bed, source_values, numeric_bed, bed_raw, source_bed_raw
        self._check_session_budget()
        self._write_timing()

    @property
    def expected_times(self) -> list[float]:
        return self.metadata["times"]

    def _check_session_budget(self) -> None:
        size, _ = _safe_tree_size(self.out)
        if size > MAX_SESSION_BYTES:
            raise LiveSessionError(f"session storage {size} exceeds {MAX_SESSION_BYTES} byte cap")

    def _read_native_running(self) -> bool:
        path = self.case_dir / "native-call-status.json"
        if not os.path.lexists(path):
            self.native_call_started = False
            return False
        document, _ = _read_json(path, "native call status", limit=64 * 1024)
        value = document.get("native_running")
        if not isinstance(value, bool):
            raise LiveSessionError("native call status has no boolean native_running field")
        call_started = document.get("native_call_started")
        if not isinstance(call_started, bool):
            raise LiveSessionError("native call status has no boolean native_call_started field")
        self.native_running = value
        self.native_call_started = call_started
        return value

    def _running_message(self) -> str:
        if self.native_running:
            return "SynxFlow flood.run is computing; validated exports are being published."
        if self.native_call_started:
            return "SynxFlow flood.run has ended; publisher is draining exports and auditing the case."
        return "SynxFlow child is importing/initializing; flood.run has not started."

    def _update_native_time(self) -> None:
        path = self.output_dir / "timestep_log.txt"
        if not os.path.lexists(path):
            return
        info = _require_regular(path, "native timestep log")
        if info.st_size < self._log_offset:
            raise LiveSessionError("native timestep log was truncated or replaced")
        with path.open("rb") as stream:
            stream.seek(self._log_offset)
            raw = stream.read()
        self._log_offset += len(raw)
        data = self._log_partial + raw
        complete = data.split(b"\n")
        self._log_partial = complete.pop()
        for line in complete:
            if not line.strip():
                continue
            try:
                fields = line.decode("ascii").split()
                if len(fields) < 1:
                    continue
                timestamp = float(fields[0])
            except (UnicodeDecodeError, ValueError) as error:
                raise LiveSessionError("native timestep log contains a malformed time row") from error
            if not math.isfinite(timestamp) or timestamp < 0.0:
                raise LiveSessionError("native timestep log contains a non-finite or negative time")
            self._timelog_latest = max(self._timelog_latest, timestamp)
        duration = float(self.metadata["spec"]["duration_s"])
        self.latest_native_time_s = min(
            duration,
            max(self._timelog_latest, self.frames[-1]["time_s"] if self.frames else 0.0),
        )

    def _observe_exports(self) -> dict[str, dict[int, Path]]:
        found: dict[str, dict[int, Path]] = {field: {} for field in _FIELD_PATTERNS}
        if not os.path.lexists(self.output_dir):
            return found
        _require_directory(self.output_dir, "native output directory")
        expected_ids = {int(value) for value in self.expected_times}
        current_paths: set[str] = set()
        for path in self.output_dir.iterdir():
            name = path.name
            if name.startswith("h_max_"):
                match = _MAX_DEPTH_PATTERN.fullmatch(name)
                if match is None or int(match.group(1)) != int(self.expected_times[-1]):
                    raise LiveSessionError(f"unsupported native max-depth output name: {name}")
                _require_regular(path, "native max-depth output")
                continue
            for field, pattern in _FIELD_PATTERNS.items():
                match = pattern.fullmatch(name)
                if match:
                    timestamp = int(match.group(1))
                    if timestamp not in expected_ids:
                        raise LiveSessionError(f"native {field} output has out-of-protocol time {timestamp}")
                    _require_regular(path, f"native {field} export")
                    found[field][timestamp] = path
                    current_paths.add(path.name)
                    break
                if name.endswith(".asc") and name.startswith(field + "_"):
                    raise LiveSessionError(f"native {field} export has malformed timestamp spelling: {name}")

        if self._seen_export_paths and not self._seen_export_paths.issubset(current_paths):
            missing = sorted(self._seen_export_paths - current_paths)
            raise LiveSessionError(f"native export disappeared after observation: {missing[0]}")
        self._seen_export_paths.update(current_paths)
        for relative, expected_signature in self._published_raw_stats.items():
            path = self.output_dir / relative
            info = _require_regular(path, "published native source export")
            signature = (info.st_size, info.st_mtime_ns, info.st_ino)
            if signature != expected_signature:
                raise LiveSessionError(f"published native source export changed: {relative}")

        h_times = set(found["h"])
        self._seen_h_times.update(h_times)
        if h_times:
            first_missing: int | None = None
            for timestamp in self.expected_times:
                timestamp_id = int(timestamp)
                if timestamp_id not in self._seen_h_times:
                    first_missing = timestamp_id
                    break
            if first_missing is not None and any(value > first_missing for value in self._seen_h_times):
                raise LiveSessionError(f"native depth export sequence skipped {first_missing}s")
        now = _now_pair()
        for timestamp in h_times:
            if timestamp not in self._observed_h:
                self._observed_h[timestamp] = now
        return found

    def _manifest_document(self, state: str, message: str) -> dict[str, Any]:
        if self._manifest_static is None:
            raise LiveSessionError("static stream metadata was not initialized")
        document = dict(self._manifest_static)
        document["provenance"] = dict(self._manifest_static["provenance"])
        document["provenance"]["source_sha256"] = dict(
            self._manifest_static["provenance"]["source_sha256"]
        )
        document["provenance"]["raw_asc_sha256"] = list(self.raw_hashes)
        if self._result_written and os.path.lexists(self.case_dir / "case-result.json"):
            document["provenance"]["source_sha256"]["case_result"] = _sha256_file(
                self.case_dir / "case-result.json"
            )
        document["session_id"] = self.session_id
        document["revision"] = self.revision + 1
        document["frames"] = list(self.frames)
        document["producer"] = {
            "state": state,
            "native_running": self.native_running if state == "running" else False,
            "pid": self.pid,
            "latest_native_time_s": self.latest_native_time_s,
            "message": message,
        }
        if self._completed_auxiliary is not None:
            document["provenance"]["native_auxiliary_outputs"] = [self._completed_auxiliary]
        return document

    def _publish_manifest(self, *, force: bool = False, message: str = "") -> None:
        if not self.frames:
            return
        unix_s, mono_s = _now_pair()
        if not force and mono_s - self._last_manifest_update_mono < MANIFEST_MIN_INTERVAL_S:
            return
        # A publication always carries the full immutable descriptor prefix.
        document = self._manifest_document("running", message or self._running_message())
        document["revision"] = self.revision + 1
        _atomic_json(self.out / "stream.json", document, replace=self.revision >= 0, limit=MAX_JSON_BYTES)
        self.revision += 1
        self._last_manifest_update_mono = mono_s
        self._manifest_native_time = self.latest_native_time_s
        self._manifest_native_running = self.native_running
        self._write_timing(now=(unix_s, mono_s))

    def _write_timing(self, now: tuple[float, float] | None = None) -> None:
        current_unix, current_mono = now or _now_pair()
        native_call = self._native_call_timing or {}
        timing = {
            "schema": TIMING_SCHEMA,
            "session_id": self.session_id,
            "started_unix_s": self._session_started_unix_s,
            "started_utc": _iso_utc(self._session_started_unix_s),
            "started_monotonic_s": self._session_started_monotonic_s,
            "child_started_unix_s": self._child_started_unix_s,
            "child_started_monotonic_s": self._child_started_monotonic_s,
            "native_call": native_call,
            "frames": list(self.frame_timings),
            "updated_unix_s": current_unix,
            "updated_monotonic_s": current_mono,
            "native_duration_wall_s": native_call.get("native_call_wall_s"),
        }
        _atomic_json(self.out / "timing.json", timing, replace=os.path.lexists(self.out / "timing.json"), limit=MAX_JSON_BYTES)

    def _eligible_triplet(
        self, timestamp: int, found: dict[str, dict[int, Path]]
    ) -> tuple[Path, Path, Path]:
        paths: list[Path] = []
        for field in ("h", "hUx", "hUy"):
            path = found[field].get(timestamp)
            if path is None:
                raise LiveSessionError(
                    f"native frame {timestamp}s crossed its successor barrier without {field} export"
                )
            paths.append(path)
        return paths[0], paths[1], paths[2]

    def publish_ready_frames(self, *, successful_exit: bool = False) -> int:
        """Publish zero or more consecutive eligible frames; never waits on bad data."""
        self._check_session_budget()
        self._read_native_running()
        self._update_native_time()
        found = self._observe_exports()
        published = 0
        next_index = len(self.frames)
        while next_index < len(self.expected_times):
            # Refresh between conversions: the native call can end while the
            # parent drains several ready ASC frames from its one-frame queue.
            self._read_native_running()
            self._update_native_time()
            found = self._observe_exports()
            timestamp = int(self.expected_times[next_index])
            is_final = next_index == len(self.expected_times) - 1
            eligible = successful_exit if is_final else int(self.expected_times[next_index + 1]) in found["h"]
            if not eligible:
                break
            paths = self._eligible_triplet(timestamp, found)
            for path in paths:
                _require_regular(path, f"native frame field at {timestamp}s")
            before_signatures = {
                path.name: (path.stat().st_size, path.stat().st_mtime_ns, path.stat().st_ino)
                for path in paths
            }
            depth, qx, qz, stats, raw_hashes = converter._read_frame(
                paths,
                timestamp,
                self.metadata["dem_header"],
                float(self.metadata["cell_size_m"]),
            )
            combined = array("f", depth)
            combined.extend(qx)
            combined.extend(qz)
            raw_payload = converter._little_endian_bytes(combined)
            # CPU parsing/packing can take long enough for the native call to
            # finish or for another export to start. Use fresh state in the
            # descriptor that the consumer will actually observe.
            self._read_native_running()
            self._update_native_time()
            found = self._observe_exports()
            for path in paths:
                info = _require_regular(path, f"native frame field at {timestamp}s")
                signature = (info.st_size, info.st_mtime_ns, info.st_ino)
                if signature != before_signatures[path.name]:
                    raise LiveSessionError(f"native source changed while parsing frame {timestamp}s")
            if is_final:
                if not successful_exit:
                    del depth, qx, qz, combined, raw_payload
                    break
                barrier_seen = self._child_exit_observed
                if barrier_seen is None:
                    raise LiveSessionError("final frame has no successful child-exit observation")
            else:
                barrier_seen = self._observed_h.get(int(self.expected_times[next_index + 1]))
                if barrier_seen is None:
                    raise LiveSessionError("frame successor barrier observation is missing")
            relative_path = f"frames/frame-{next_index:06d}.f32"
            payload_sha = _atomic_new_bytes(self.out / relative_path, raw_payload)
            published_unix, published_mono = _now_pair()
            export_seen = self._observed_h.get(timestamp, barrier_seen)
            if next_index not in self._observed_barrier:
                self._observed_barrier[next_index] = barrier_seen
            self._published_raw_stats.update(before_signatures)
            descriptor = {
                "time_s": float(timestamp),
                "path": relative_path,
                "sha256": payload_sha,
                **stats,
                "published_unix_s": published_unix,
                "published_monotonic_s": published_mono,
            }
            # Publish only after payload bytes are closed, durable, and visible.
            if os.path.lexists(self.out / relative_path) and _sha256_file(self.out / relative_path) != payload_sha:
                raise LiveSessionError(f"published payload hash changed before manifest update: {relative_path}")
            self.frames.append(descriptor)
            self.raw_hashes.append({"time_s": float(timestamp), **raw_hashes})
            queue_lag = max(0.0, published_mono - barrier_seen[1])
            self.frame_timings.append({
                "time_s": float(timestamp),
                "native_export_observed_unix_s": export_seen[0],
                "native_export_observed_monotonic_s": export_seen[1],
                "successor_barrier_observed_unix_s": barrier_seen[0],
                "successor_barrier_observed_monotonic_s": barrier_seen[1],
                "published_unix_s": published_unix,
                "published_monotonic_s": published_mono,
                "queue_lag_after_barrier_s": queue_lag,
                "export_to_publication_wall_s": max(0.0, published_mono - export_seen[1]),
            })
            self.latest_native_time_s = max(self.latest_native_time_s, float(timestamp))
            del depth, qx, qz, combined, raw_payload
            self._publish_manifest(force=True)
            self._check_session_budget()
            published += 1
            next_index += 1
            # Native time is monotonic in this protocol; every following frame
            # is considered on the next bounded conversion iteration.
        return published

    def _load_child_timing(self) -> dict[str, Any] | None:
        path = self.case_dir / "native-call-status.json"
        if not os.path.lexists(path):
            return None
        status, _ = _read_json(path, "native call status", limit=64 * 1024)
        if status.get("native_running") is not False:
            raise LiveSessionError("native child exited without closing its flood.run timing interval")
        started_unix = status.get("native_call_started_unix_s")
        started_mono = status.get("native_call_started_monotonic_s")
        returned_unix = status.get("native_call_returned_unix_s")
        returned_mono = status.get("native_call_returned_monotonic_s")
        finished_unix = status.get("native_call_finished_unix_s", returned_unix)
        finished_mono = status.get("native_call_finished_monotonic_s", returned_mono)
        if status.get("native_call_started") is not True:
            return None
        values = (started_unix, started_mono, finished_unix, finished_mono)
        if any(isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) for value in values):
            raise LiveSessionError("native call timing contains a missing or non-finite timestamp")
        if finished_mono < started_mono or finished_unix <= 0 or started_unix <= 0:
            raise LiveSessionError("native call timing has a backwards or invalid interval")
        result = {
            "native_call_started_unix_s": float(started_unix),
            "native_call_started_utc": _iso_utc(float(started_unix)),
            "native_call_started_monotonic_s": float(started_mono),
            "native_call_finished_unix_s": float(finished_unix),
            "native_call_finished_utc": _iso_utc(float(finished_unix)),
            "native_call_finished_monotonic_s": float(finished_mono),
            "native_call_wall_s": float(finished_mono - started_mono),
            "native_call_returned_normally": status.get("native_call_returned_normally") is True,
            "synxflow_version": status.get("synxflow_version"),
            "python": status.get("python"),
            "native_extension_path": status.get("native_extension_path"),
            "native_extension_sha256": status.get("native_extension_sha256"),
        }
        if returned_unix is not None and returned_mono is not None:
            result.update({
                "native_call_returned_unix_s": float(returned_unix),
                "native_call_returned_utc": _iso_utc(float(returned_unix)),
                "native_call_returned_monotonic_s": float(returned_mono),
            })
        if status.get("native_call_terminated_by_launcher") is True:
            result["native_call_terminated_by_launcher"] = True
        return result

    def _close_interrupted_call_status(self, state: str) -> None:
        path = self.case_dir / "native-call-status.json"
        if not os.path.lexists(path):
            return
        status, _ = _read_json(path, "native call status", limit=64 * 1024)
        if status.get("native_running") is not True:
            return
        finished_unix, finished_mono = _now_pair()
        status.update({
            "native_running": False,
            "native_call_finished_unix_s": finished_unix,
            "native_call_finished_utc": _iso_utc(finished_unix),
            "native_call_finished_monotonic_s": finished_mono,
            "native_call_returned_normally": False,
            "native_call_terminated_by_launcher": self._child_stopped_by_launcher or state == "cancelled",
            "native_call_terminal_observation": "launcher observed owned child exit",
        })
        _atomic_json(path, status, replace=True, limit=64 * 1024)

    def _read_success_marker(self) -> bool:
        path = self.case_dir / "native.stdout"
        if not os.path.lexists(path):
            return False
        _require_regular(path, "native stdout")
        marker = SUCCESS_MARKER.encode("ascii")
        overlap = b""
        with path.open("rb") as stream:
            while True:
                block = stream.read(64 * 1024)
                if not block:
                    return False
                data = overlap + block
                if marker in data:
                    return True
                overlap = data[-(len(marker) - 1):]

    def _case_result(self, state: str, process_exit_code: int | None, message: str, *, native_success: bool) -> dict[str, Any]:
        before_hashes = self._pre_solver_audit["native_input_sha256"] if self._pre_solver_audit else {}
        after_hashes: dict[str, str] = {}
        if os.path.lexists(self.case_dir / "native/input"):
            after_hashes, _, _ = _safe_tree_hashes(self.case_dir / "native/input")
        unchanged = bool(before_hashes) and before_hashes == after_hashes
        current_unix, current_mono = _now_pair()
        command = [str(self.python), "-u", str(Path(__file__).resolve()), "--_solver-child", str(self.case_dir)]
        call_timing = self._native_call_timing or {}
        result: dict[str, Any] = {
            "schema": CASE_RESULT_SCHEMA,
            "case": self.metadata["spec"],
            "status": state,
            "message": message,
            "command": command,
            "working_directory": str(self.case_dir),
            "process_exit_code": process_exit_code,
            "process_timeout": False,
            "native_gpu_success_marker": native_success,
            "native_child_started_unix_s": self._child_started_unix_s,
            "native_child_started_monotonic_s": self._child_started_monotonic_s,
            "native_child_finished_unix_s": current_unix,
            "native_child_finished_monotonic_s": current_mono,
            "native_child_wall_s": (
                current_mono - self._child_started_monotonic_s
                if self._child_started_monotonic_s is not None else None
            ),
            **call_timing,
            "source_identity": self.pinned_identity["native_backend_identity"],
            "input_provenance": {
                "case_dem_ascii_sha256": self._dem_sha256,
                "native_input_hashes_before_run": before_hashes,
                "native_input_hashes_after_run": after_hashes,
                "native_inputs_unchanged_during_run": unchanged,
                "native_input_audit_before_solver": self._pre_solver_audit,
            },
            "output_provenance": {
                "published_frame_count": len(self.frames),
                "expected_frame_count": len(self.expected_times),
                "raw_asc_sha256": list(self.raw_hashes),
                "raw_stdout": "native.stdout",
                "raw_stderr": "native.stderr",
            },
            "postrun_audit": {
                "all_expected_frames_published": len(self.frames) == len(self.expected_times),
                "native_inputs_unchanged": unchanged,
                "native_gpu_success_marker": native_success,
                "converter_parity_ready": False,
            },
        }
        return result

    def _write_terminal_sidecar(self, state: str, message: str, process_exit_code: int | None) -> None:
        finished_unix, finished_mono = _now_pair()
        result = {
            "schema": RESULT_SCHEMA,
            "session_id": self.session_id,
            "state": state,
            "message": message,
            "pid": self.pid,
            "process_exit_code": process_exit_code,
            "frames_published": len(self.frames),
            "expected_frames": len(self.expected_times),
            "started_unix_s": self._session_started_unix_s,
            "started_monotonic_s": self._session_started_monotonic_s,
            "finished_unix_s": finished_unix,
            "finished_utc": _iso_utc(finished_unix),
            "finished_monotonic_s": finished_mono,
            "case_result": "case/case-result.json" if os.path.lexists(self.case_dir / "case-result.json") else None,
            "stream": "stream.json" if os.path.lexists(self.out / "stream.json") else None,
            "timing": "timing.json",
            **(self._native_call_timing or {}),
        }
        _atomic_json(self.out / "session-result.json", result, replace=os.path.lexists(self.out / "session-result.json"), limit=MAX_AUDIT_JSON_BYTES)

    def _finalize_success(self, process_exit_code: int, message: str) -> None:
        self._native_call_timing = self._load_child_timing()
        if self._native_call_timing is None or not self._native_call_timing["native_call_returned_normally"]:
            raise LiveSessionError("native child omitted a normal flood.run timing interval")
        if self._native_call_timing.get("synxflow_version") != "1.0.1":
            raise LiveSessionError("native child did not prove SynxFlow 1.0.1")
        if Path(str(self._native_call_timing.get("python", ""))).resolve() != self.python.resolve():
            raise LiveSessionError("native child did not run under the requested pinned Python")
        if not self._read_success_marker():
            raise LiveSessionError("native child exited zero without the stock SynxFlow success marker")
        after_hashes, _, _ = _safe_tree_hashes(self.case_dir / "native/input")
        if after_hashes != self._pre_solver_audit["native_input_sha256"]:
            raise LiveSessionError("native input hashes changed during flood.run")

        expected = [int(value) for value in self.expected_times]
        frame_paths, auxiliary = converter._collect_frame_paths(self.output_dir, self.expected_times)
        actual_triplets = sorted(frame_paths)
        if actual_triplets != expected:
            raise LiveSessionError("native postrun export set differs from frozen expected times")
        if len(self.frames) != len(expected):
            raise LiveSessionError("not all expected native frames crossed the live publisher")

        # Re-read each finalized raw export with the exact converter parser and
        # compare its hashes/stats to the descriptor already exposed live.
        for index, timestamp in enumerate(expected):
            depth, qx, qz, stats, raw_hashes = converter._read_frame(
                frame_paths[timestamp], timestamp, self.metadata["dem_header"], self.metadata["cell_size_m"]
            )
            if raw_hashes != {key: value for key, value in self.raw_hashes[index].items() if key != "time_s"}:
                raise LiveSessionError(f"native raw exports changed after publishing frame {timestamp}s")
            descriptor = self.frames[index]
            for key, value in stats.items():
                if descriptor.get(key) != value:
                    raise LiveSessionError(f"converter frame statistic changed for {timestamp}s: {key}")
            del depth, qx, qz

        if auxiliary is not None:
            self._completed_auxiliary = auxiliary
        self.native_running = False
        self.latest_native_time_s = float(self.metadata["spec"]["duration_s"])
        # Save actual child evidence before the converter's unchanged validator
        # is asked to independently accept the completed case.
        result = self._case_result("completed", process_exit_code, message, native_success=True)
        result["postrun_audit"]["converter_parity_ready"] = True
        _atomic_json(self.case_dir / "native-postrun.json", {
            "schema": SESSION_SCHEMA + ".postrun-audit",
            "native_call_timing": self._native_call_timing,
            "input_sha256_after_run": after_hashes,
            "unchanged_inputs": True,
            "complete_native_frame_count": len(expected),
            "converter_frame_validation": "converter._read_frame accepted every finalized h/hUx/hUy triplet",
        }, replace=False, limit=MAX_AUDIT_JSON_BYTES)
        _atomic_json(self.case_dir / "case-result.json", result, replace=False, limit=MAX_AUDIT_JSON_BYTES)
        self._result_written = True
        converter._validate_case(self.case_dir)
        final_paths, _ = converter._collect_frame_paths(self.output_dir, self.expected_times)
        if sorted(final_paths) != expected:
            raise LiveSessionError("unchanged recording converter no longer sees the complete native frame set")
        case_result_sha = _sha256_file(self.case_dir / "case-result.json")
        self._manifest_static["provenance"]["source_sha256"]["case_result"] = case_result_sha
        # Publish the terminal state only after the actual result digest and
        # full prefix are available.
        self.revision += 1
        completed = self._manifest_document(
            "completed", "SynxFlow completed; all frames passed the recording converter audit."
        )
        completed["revision"] = self.revision
        _atomic_json(self.out / "stream.json", completed, replace=True, limit=MAX_JSON_BYTES)
        self._last_manifest_update_mono = time.monotonic()
        self._write_timing()
        self._write_terminal_sidecar("completed", message, process_exit_code)

    def finish_failure(
        self,
        state: str,
        message: str,
        process_exit_code: int | None,
        *,
        native_success: bool = False,
    ) -> None:
        if state not in {"failed", "cancelled"}:
            raise ValueError("terminal state must be failed or cancelled")
        try:
            self._close_interrupted_call_status(state)
            self._native_call_timing = self._load_child_timing()
        except LiveSessionError:
            # A killed/import-failed child may stop before writing its closing
            # status. Preserve what is available and mark native_running false.
            self.native_running = False
        self.native_running = False
        try:
            after_hashes, _, _ = _safe_tree_hashes(self.case_dir / "native/input")
        except LiveSessionError:
            after_hashes = {}
        if self._pre_solver_audit is not None:
            try:
                result = self._case_result(state, process_exit_code, message, native_success=native_success)
                result["input_provenance"]["native_input_hashes_after_run"] = after_hashes
                result["input_provenance"]["native_inputs_unchanged_during_run"] = (
                    after_hashes == self._pre_solver_audit["native_input_sha256"]
                )
                result["postrun_audit"]["failure"] = message
                _atomic_json(self.case_dir / "case-result.json", result, replace=os.path.lexists(self.case_dir / "case-result.json"), limit=MAX_AUDIT_JSON_BYTES)
                self._result_written = True
            except (OSError, LiveSessionError):
                pass
        if self.frames:
            self.revision += 1
            terminal = self._manifest_document(state, message)
            terminal["revision"] = self.revision
            _atomic_json(self.out / "stream.json", terminal, replace=True, limit=MAX_JSON_BYTES)
        self._write_timing()
        self._write_terminal_sidecar(state, message, process_exit_code)

    def finish_child(self, process: subprocess.Popen[bytes]) -> int:
        exit_code = process.returncode
        self._child_exit_observed = _now_pair()
        self._native_call_timing = self._load_child_timing()
        self.native_running = False
        native_success = exit_code == 0 and self._read_success_marker()
        try:
            self.publish_ready_frames(successful_exit=native_success)
            if exit_code == 0 and native_success:
                self._finalize_success(exit_code, "Pinned SynxFlow child exited successfully.")
                return exit_code
            reason = (
                f"SynxFlow child exited with status {exit_code}."
                if exit_code is not None else "SynxFlow child did not return an exit status."
            )
            if exit_code == 0 and not native_success:
                reason = "SynxFlow child omitted the native success marker."
            self.finish_failure("failed", reason, exit_code, native_success=native_success)
            return exit_code if exit_code not in (None, 0) else 1
        except Exception as error:
            reason = f"{type(error).__name__}: {error}"
            try:
                self.finish_failure("failed", reason, exit_code, native_success=native_success)
            except Exception:
                pass
            raise LiveSessionError(reason) from error

    def run(self, poll_interval_s: float = POLL_INTERVAL_S) -> int:
        if not math.isfinite(poll_interval_s) or poll_interval_s <= 0 or poll_interval_s > 5:
            raise LiveSessionError("poll interval must be finite and in (0, 5] seconds")
        command = [
            str(self.python),
            "-u",
            str(Path(__file__).resolve()),
            "--_solver-child",
            str(self.case_dir),
        ]
        environment = {
            **os.environ,
            "PYTHONDONTWRITEBYTECODE": "1",
            "MPLCONFIGDIR": str(self.out / "cache/matplotlib"),
            "XDG_CACHE_HOME": str(self.out / "cache/xdg"),
        }
        stdout_path = self.case_dir / "native.stdout"
        stderr_path = self.case_dir / "native.stderr"
        if os.path.lexists(stdout_path) or os.path.lexists(stderr_path):
            raise LiveSessionError("native log path already exists in fresh case")
        process: subprocess.Popen[bytes] | None = None
        process_started_unix_s, process_started_mono = _now_pair()
        self._child_started_unix_s = process_started_unix_s
        self._child_started_monotonic_s = process_started_mono
        prior_sigint = signal.getsignal(signal.SIGINT)
        prior_sigterm = signal.getsignal(signal.SIGTERM)
        interrupted_state: list[str] = []

        def handle_sigterm(_signum, _frame) -> None:
            interrupted_state.append("SIGTERM")
            raise KeyboardInterrupt

        signal.signal(signal.SIGTERM, handle_sigterm)
        try:
            with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
                process = subprocess.Popen(
                    command,
                    cwd=self.case_dir,
                    env=environment,
                    stdout=stdout,
                    stderr=stderr,
                    close_fds=True,
                )
            while process.poll() is None:
                try:
                    self.publish_ready_frames()
                    # Producer revisions also expose solver progress and the
                    # call-status transition without rewriting unchanged data.
                    self.native_running = self._read_native_running()
                    self._update_native_time()
                    native_changed = self.native_running != self._manifest_native_running
                    if self.frames and (
                        native_changed
                        or self.latest_native_time_s != self._manifest_native_time
                    ):
                        self._publish_manifest(force=native_changed)
                    self._check_session_budget()
                except Exception:
                    self._stop_child(process)
                    raise
                time.sleep(poll_interval_s)
            return self.finish_child(process)
        except KeyboardInterrupt:
            # Ignore repeated cancellation signals while the finite terminal
            # evidence is being written. A controller may send SIGTERM after
            # its initial SIGINT; it must not interrupt this cleanup again.
            signal.signal(signal.SIGINT, signal.SIG_IGN)
            signal.signal(signal.SIGTERM, signal.SIG_IGN)
            if process is not None:
                self._stop_child(process)
                exit_code = process.poll()
            else:
                exit_code = None
            self.native_running = False
            self.finish_failure(
                "cancelled",
                "Foreground launcher was cancelled; only its SynxFlow child was stopped.",
                exit_code,
            )
            return 130
        except Exception as error:
            if process is not None:
                self._stop_child(process)
                exit_code = process.poll()
            else:
                exit_code = None
            self.finish_failure("failed", f"{type(error).__name__}: {error}", exit_code)
            raise
        finally:
            signal.signal(signal.SIGINT, prior_sigint)
            signal.signal(signal.SIGTERM, prior_sigterm)

    def _stop_child(self, process: subprocess.Popen[bytes]) -> None:
        if process.poll() is not None:
            return
        self._child_stopped_by_launcher = True
        process.terminate()
        try:
            process.wait(timeout=CHILD_STOP_GRACE_S)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def _child_status_path(case_dir: Path) -> Path:
    candidate = case_dir.expanduser().absolute()
    try:
        resolved_case = candidate.resolve(strict=True)
    except OSError as error:
        raise LiveSessionError("solver child case path does not resolve") from error
    if candidate.is_symlink() or resolved_case != candidate:
        raise LiveSessionError("solver child case path must not contain symlinks or aliases")
    if not (resolved_case / "native/input").is_dir():
        raise LiveSessionError("solver child case has no native input directory")
    return resolved_case / "native-call-status.json"
def _solver_child(case_dir: Path) -> int:
    case_dir = case_dir.expanduser().absolute()
    status_path = _child_status_path(case_dir)
    audit, _ = _read_json(case_dir / "native-preflight.json", "native preflight audit")
    expected_hashes = audit.get("native_input_sha256")
    if not isinstance(expected_hashes, dict) or not expected_hashes:
        raise LiveSessionError("native preflight audit omits input hashes")
    before, _, _ = _safe_tree_hashes(case_dir / "native/input")
    if before != expected_hashes:
        raise LiveSessionError("native input tree changed before solver child started")
    from synxflow import __version__, flood

    if __version__ != "1.0.1":
        raise LiveSessionError(f"expected SynxFlow 1.0.1, got {__version__}")
    pinned_protocol, _ = _read_json(PINNED_PROTOCOL, "pinned SynxFlow protocol")
    pinned_identity = pinned_protocol.get("native_backend_identity")
    payload_hashes = pinned_protocol.get("installed_synxflow_payload_files")
    if not isinstance(pinned_identity, dict) or not isinstance(payload_hashes, dict):
        raise LiveSessionError("pinned protocol omits the native extension identity")
    expected_extension_sha = pinned_identity.get("extension_sha256")
    extension_matches = [
        Path(name).resolve(strict=True)
        for name, digest in payload_hashes.items()
        if digest == expected_extension_sha and isinstance(name, str)
    ]
    imported_path = Path(str(flood.__file__))
    if imported_path.is_symlink() or not imported_path.is_file():
        raise LiveSessionError("imported SynxFlow flood extension is not a regular pinned file")
    imported_path = imported_path.resolve(strict=True)
    imported_sha = _sha256_file(imported_path)
    if (len(extension_matches) != 1 or imported_path != extension_matches[0]
            or imported_sha != expected_extension_sha):
        raise LiveSessionError("imported SynxFlow flood extension differs from the pinned wheel payload")
    status = {
        "schema": SESSION_SCHEMA + ".native-call-status",
        "native_running": True,
        "native_call_started": False,
        "synxflow_version": __version__,
        "python": str(Path(sys.executable).resolve()),
        "native_extension_path": str(imported_path),
        "native_extension_sha256": imported_sha,
    }
    _atomic_json(status_path, status, replace=os.path.lexists(status_path), limit=64 * 1024)
    print(f"LIVE_SYNXFLOW_VERSION={__version__}", flush=True)
    print(f"LIVE_PYTHON={Path(sys.executable).resolve()}", flush=True)
    # Record the call boundary immediately before invoking the unchanged stock
    # entry point. The status flag is already true so the UI does not infer
    # solver activity from the launcher or child process lifetime.
    unix_started, mono_started = _now_pair()
    status.update({
        "native_call_started": True,
        "native_call_started_unix_s": unix_started,
        "native_call_started_utc": _iso_utc(unix_started),
        "native_call_started_monotonic_s": mono_started,
    })
    returned_normally = False
    error_text: str | None = None
    try:
        flood.run(str(case_dir / "native"))
        returned_normally = True
        return 0
    except BaseException as error:
        error_text = f"{type(error).__name__}: {error}"
        raise
    finally:
        unix_returned, mono_returned = _now_pair()
        final = {
            **status,
            "native_running": False,
            "native_call_returned_unix_s": unix_returned,
            "native_call_returned_utc": _iso_utc(unix_returned),
            "native_call_returned_monotonic_s": mono_returned,
            "native_call_finished_unix_s": unix_returned,
            "native_call_finished_utc": _iso_utc(unix_returned),
            "native_call_finished_monotonic_s": mono_returned,
            "native_call_wall_s": mono_returned - mono_started,
            "native_call_returned_normally": returned_normally,
            "error": error_text,
        }
        _atomic_json(status_path, final, replace=True, limit=64 * 1024)
        print(f"LIVE_NATIVE_CALL_RETURNED_UNIX_S={unix_returned:.9f}", flush=True)
        print(f"LIVE_NATIVE_CALL_RETURNED_MONOTONIC_S={mono_returned:.9f}", flush=True)
        print(f"LIVE_NATIVE_CALL_WALL_S={mono_returned - mono_started:.9f}", flush=True)


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-case", type=Path, help="existing completed converter-audited case")
    parser.add_argument("--out", type=Path, help="fresh live session directory")
    parser.add_argument("--python", type=Path, help="existing pinned SynxFlow venv Python")
    parser.add_argument("--poll-interval-s", type=float, default=POLL_INTERVAL_S)
    parser.add_argument("--_solver-child", type=Path, help=argparse.SUPPRESS)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = _parse_args(argv)
    if arguments._solver_child is not None:
        try:
            return _solver_child(arguments._solver_child)
        except BaseException:
            traceback.print_exc()
            return 1
    if arguments.baseline_case is None or arguments.out is None or arguments.python is None:
        raise SystemExit("--baseline-case, --out, and --python are required")
    try:
        session = LiveSession(arguments.baseline_case, arguments.out, arguments.python)
        return session.run(arguments.poll_interval_s)
    except (OSError, LiveSessionError) as error:
        print(f"live SynxFlow session failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
