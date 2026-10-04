#!/usr/bin/env python3
"""Opt-in rain-only sensitivity runner around the released SynxFlow 1.0.1 wheel.

This harness prepares separate, immutable case directories and launches the
unchanged native solver only after the serialized inputs match the sealed
mountain baseline everywhere except the uniform rainfall schedule.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable


ROOT = Path(__file__).resolve().parents[3]
EXPERIMENT_ROOT = ROOT / "outputs/fluid/native-rain-intensity-v1-20261003-bXgHXx"
WORKER_ROOT = EXPERIMENT_ROOT / "worker"
DEFAULT_PROTOCOL = EXPERIMENT_ROOT / "protocol.json"
BASELINE_ROOT = ROOT / (
    "outputs/fluid/native-runoff-reuse-v1-20261002-trm3Ve/"
    "native-mountain-followup-20261003T042411Z/mountain-30m-7200s"
)
REFERENCE_HELPER = ROOT / "projects/fluid/fluid_25d/run_native_runoff_reuse_v1.py"
REFERENCE_TESTS = ROOT / "projects/fluid/fluid_25d/test_run_native_runoff_reuse_v1.py"
CONVERTER_SOURCE = ROOT / "projects/fluid/fluid_25d/convert_synxflow_recording_v1.py"
NATIVE_PYTHON = ROOT / (
    "outputs/fluid/native-runoff-reuse-v1-20261002-trm3Ve/"
    "synxflow-setup/env/.venv/bin/python"
)

PROTOCOL_SCHEMA = "cubey.fluid25d.native_rain_intensity.v1.protocol"
CASE_PROTOCOL_SCHEMA = "cubey.fluid25d.native_rain_intensity.v1.case-protocol"
CASE_RESULT_SCHEMA = "cubey.fluid25d.native_rain_intensity.v1.case-result"
PHASE_SUMMARY_SCHEMA = "cubey.fluid25d.native_rain_intensity.v1.phase-summary"
EXPECTED_PROTOCOL_SHA256 = "bffc709a0250561cdcd04de4af5f7d56a17e261d9a1b10f262973241d34e74eb"
EXPECTED_HELPER_SHA256 = "879de4536f343ae4fb18028696213acefcadee60a45072d4d14cf4af38e5c77a"
EXPECTED_CONVERTER_SHA256 = "7a56892a19eb9358f087c6284a40643b882b45d533e74d443c7712b4f3f8e4e3"
EXPECTED_BASELINE_CASE_PROTOCOL_SHA256 = "8dcd66da2d9fdf100d8e476b4d97c7a1f132028d6c3ec477d8ca61f4da6896d4"
EXPECTED_BASELINE_CASE_RESULT_SHA256 = "3e28979202059e00757c3c0f78626cc1f76a2d7cede59797e9fbd64a8a1725aa"
EXPECTED_BASELINE_CASE_SPEC_SHA256 = "bbdc9147528c772dc0563aa1c9829ed2a1b5b0246524f7ae37f4b3f3e7d45f84"
EXPECTED_BASELINE_DEM_SHA256 = "04a3594d3d2b6a7c60878facdaa28925a290da040ccd493410bd8f6b7a0a4cbb"
ALLOWED_INPUT_DIFFERENCES = {"field/precipitation_source_all.dat"}
ASC_HALF_QUANTIZATION_M = 0.5e-6
CASE_TIMEOUT_S = 1800.0
EXPECTED_RATES_MM_PER_HOUR = (48, 120)
SAFE_CASE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")


class RainIntensityError(RuntimeError):
    """Raised when frozen protocol, custody, or case health checks fail."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def _marker_value(text: str, prefix: str) -> str | None:
    return next((line[len(prefix):] for line in text.splitlines() if line.startswith(prefix)), None)


def rate_m_per_s(rate_mm_per_hour: int) -> float:
    if rate_mm_per_hour not in EXPECTED_RATES_MM_PER_HOUR:
        raise RainIntensityError(f"unsupported frozen rain rate: {rate_mm_per_hour}")
    return float(rate_mm_per_hour) / 3_600_000.0


def case_specs(protocol: dict[str, Any], rates: tuple[int, ...]) -> list[dict[str, Any]]:
    """Return protocol-defined specs, allowing exactly the declared deltas."""
    if protocol.get("allowed_case_spec_differences") != ["name", "purpose", "rain_rate_m_per_s"]:
        raise RainIntensityError("frozen protocol case-spec difference allowlist changed")
    if protocol.get("new_rain_mm_per_hour_in_order") != list(EXPECTED_RATES_MM_PER_HOUR):
        raise RainIntensityError("frozen protocol rain-rate order changed")
    baseline = protocol.get("baseline_case_spec")
    if not isinstance(baseline, dict):
        raise RainIntensityError("frozen protocol omits the baseline case specification")
    protocol_cases = protocol.get("cases")
    if not isinstance(protocol_cases, list):
        raise RainIntensityError("frozen protocol omits its case list")
    by_rate = {case.get("rain_mm_per_hour"): case for case in protocol_cases if isinstance(case, dict)}
    if set(by_rate) != set(EXPECTED_RATES_MM_PER_HOUR) or len(protocol_cases) != 2:
        raise RainIntensityError("frozen protocol case list differs from the two declared rates")
    results = []
    for rate in rates:
        if rate not in EXPECTED_RATES_MM_PER_HOUR:
            raise RainIntensityError(f"unsupported frozen rain rate: {rate}")
        declaration = by_rate[rate]
        name, purpose = declaration.get("name"), declaration.get("purpose")
        if not isinstance(name, str) or not SAFE_CASE_NAME.fullmatch(name):
            raise RainIntensityError(f"unsafe case name in frozen protocol for {rate} mm/h")
        if not isinstance(purpose, str) or not purpose.strip():
            raise RainIntensityError(f"missing frozen case purpose for {rate} mm/h")
        spec = dict(baseline)
        spec["name"] = name
        spec["purpose"] = purpose
        spec["rain_rate_m_per_s"] = rate_m_per_s(rate)
        changed = {key for key in baseline.keys() | spec.keys() if baseline.get(key) != spec.get(key)}
        if changed != {"name", "purpose", "rain_rate_m_per_s"}:
            raise RainIntensityError(f"case specification changes fields outside the allowlist: {sorted(changed)}")
        results.append(spec)
    return results


def selected_rates(rate: int | None, phase: str | None) -> tuple[int, ...]:
    if (rate is None) == (phase is None):
        raise RainIntensityError("select exactly one of --rate or --phase")
    if rate is not None:
        if rate not in EXPECTED_RATES_MM_PER_HOUR:
            raise RainIntensityError(f"unsupported frozen rain rate: {rate}")
        return (rate,)
    if phase != "all":
        raise RainIntensityError("the only supported --phase value is 'all'")
    return EXPECTED_RATES_MM_PER_HOUR


def _path_has_symlink_component(path: Path) -> bool:
    absolute = Path(os.path.abspath(path))
    current = Path(absolute.anchor)
    for part in absolute.parts[1:]:
        current = current / part
        if current.is_symlink():
            return True
    return False


def checked_output_root(out: Path | str, allowed_root: Path = WORKER_ROOT) -> Path:
    allowed = Path(os.path.abspath(allowed_root))
    candidate = Path(out)
    if not candidate.is_absolute():
        candidate = allowed / candidate
    candidate = Path(os.path.abspath(candidate))
    try:
        candidate.relative_to(allowed)
    except ValueError as error:
        raise RainIntensityError(f"output path escapes the assigned worker root: {candidate}") from error
    if _path_has_symlink_component(allowed) or _path_has_symlink_component(candidate):
        raise RainIntensityError("output path or one of its parent directories is a symlink")
    if candidate.exists() and not candidate.is_dir():
        raise RainIntensityError(f"output root is not a directory: {candidate}")
    return candidate


def case_output_path(out_root: Path, name: str) -> Path:
    if not isinstance(name, str) or not SAFE_CASE_NAME.fullmatch(name) or name in {".", ".."}:
        raise RainIntensityError(f"unsafe case directory name: {name!r}")
    cases_root = out_root / "cases"
    target = cases_root / name
    if _path_has_symlink_component(cases_root) or _path_has_symlink_component(target):
        raise RainIntensityError("case output path or one of its parent directories is a symlink")
    try:
        target.relative_to(out_root)
    except ValueError as error:
        raise RainIntensityError("case output path escapes the assigned worker root") from error
    return target


def assert_fresh_case_directory(path: Path) -> None:
    if path.exists() or path.is_symlink():
        raise RainIntensityError(f"refusing to overwrite existing case directory: {path}")


def essential_process_failure(exit_code: int | None, timed_out: bool,
                              preflight_present: bool, postrun_present: bool,
                              source_identity_stable: bool,
                              success_marker_present: bool) -> tuple[str, str] | None:
    if timed_out:
        return "timeout", f"native child exceeded {CASE_TIMEOUT_S:g} seconds"
    if exit_code != 0:
        return "process_failed", f"native child exited with status {exit_code}"
    if not preflight_present:
        return "preflight_missing", "child exited without a before-solver audit record"
    if not postrun_present:
        return "postrun_audit_missing", "child exited without after-run native input hashes"
    if not source_identity_stable:
        return "source_identity_drift", "runner or reference helper source changed during the run"
    if not success_marker_present:
        return "success_marker_missing", "native child omitted the stock SynxFlow success marker"
    return None


def load_protocol(path: Path, expected_sha256: str | None = EXPECTED_PROTOCOL_SHA256) -> tuple[dict[str, Any], str]:
    if _path_has_symlink_component(path):
        raise RainIntensityError("frozen protocol path contains a symlink")
    if not path.is_file():
        raise RainIntensityError(f"frozen parent protocol is missing: {path}")
    actual_hash = sha256_file(path)
    if expected_sha256 is not None and actual_hash != expected_sha256:
        raise RainIntensityError(
            f"frozen parent protocol hash changed: expected {expected_sha256}, got {actual_hash}"
        )
    protocol = read_json(path)
    if protocol.get("schema") != PROTOCOL_SCHEMA or protocol.get("frozen_before_native_execution") is not True:
        raise RainIntensityError("parent protocol is not the frozen native rain-intensity protocol")
    if protocol.get("native_backend_identity") is None or protocol.get("terrain_source_identity") is None:
        raise RainIntensityError("parent protocol omits frozen backend or terrain identity")
    if protocol.get("baseline_case") != str(BASELINE_ROOT):
        raise RainIntensityError("parent protocol points at an unexpected baseline case")
    if protocol.get("minimum_free_bytes") is not None:
        raise RainIntensityError("free-space policy must be nested under protocol limits")
    limits = protocol.get("limits", {})
    if (limits.get("minimum_free_bytes") != 20 * 1024**3
            or limits.get("study_disk_budget_bytes") != 12 * 1024**3
            or limits.get("native_child_timeout_s_per_case") != 1800
            or limits.get("sequential_native_runs") is not True
            or limits.get("stop_120_case_on_48_essential_failure") is not True
            or limits.get("no_repair_or_tuning_reruns") is not True):
        raise RainIntensityError("parent protocol execution limits changed or are incomplete")
    case_specs(protocol, EXPECTED_RATES_MM_PER_HOUR)
    return protocol, actual_hash


def _load_reference_helper(path: Path = REFERENCE_HELPER) -> Any:
    actual = sha256_file(path)
    if actual != EXPECTED_HELPER_SHA256:
        raise RainIntensityError(f"reference helper source drifted: {actual}")
    spec = importlib.util.spec_from_file_location("cubey_native_runoff_reference", path)
    if spec is None or spec.loader is None:
        raise RainIntensityError(f"cannot load reference helper at its repository source path: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _verify_digest_map(digests: dict[str, str], label: str) -> None:
    for path_text, expected in digests.items():
        path = Path(path_text)
        if not path.is_file():
            raise RainIntensityError(f"{label} file is missing: {path}")
        actual = sha256_file(path)
        if actual != expected:
            raise RainIntensityError(f"{label} identity changed for {path}: {actual}")


def _rain_rows(helper: Any, path: Path) -> Any:
    rows = helper.np.loadtxt(path, skiprows=1, ndmin=2, dtype=helper.np.float64)
    if rows.shape != (2, 2) or not helper.np.isfinite(rows).all():
        raise RainIntensityError(f"serialized rain source is not a finite two-row schedule: {path}")
    return rows


def _validate_rain_rows(helper: Any, path: Path, expected_rate: float) -> list[list[float]]:
    rows = _rain_rows(helper, path)
    if (not helper.np.array_equal(rows[:, 0], helper.np.asarray([0.0, 7200.0]))
            or not helper.np.allclose(rows[:, 1], expected_rate, rtol=2e-6, atol=1e-14)):
        raise RainIntensityError(f"serialized rainfall is not the frozen constant rate/horizon: {path}")
    return rows.tolist()


def compare_native_inputs(
    baseline_hashes: dict[str, str],
    actual_hashes: dict[str, str],
    *,
    helper: Any,
    baseline_rain_path: Path,
    actual_rain_path: Path,
    expected_rate: float,
) -> dict[str, Any]:
    """Require byte equality for all native inputs except the rain history."""
    if set(baseline_hashes) != set(actual_hashes):
        missing = sorted(set(baseline_hashes) - set(actual_hashes))
        extra = sorted(set(actual_hashes) - set(baseline_hashes))
        raise RainIntensityError(f"native input file set changed; missing={missing}, extra={extra}")
    changed = sorted(name for name in baseline_hashes if baseline_hashes[name] != actual_hashes[name])
    if not set(changed).issubset(ALLOWED_INPUT_DIFFERENCES):
        raise RainIntensityError(f"immutable native inputs changed from the baseline: {changed}")
    if changed != ["field/precipitation_source_all.dat"]:
        raise RainIntensityError("rain-only case did not produce exactly one distinct serialized rain schedule")
    baseline_rows = _validate_rain_rows(helper, baseline_rain_path, 12.0 / 3_600_000.0)
    actual_rows = _validate_rain_rows(helper, actual_rain_path, expected_rate)
    if baseline_rows[0][0] != actual_rows[0][0] or baseline_rows[1][0] != actual_rows[1][0]:
        raise RainIntensityError("serialized rainfall start/end times differ from baseline")
    if baseline_rows[0][1] == actual_rows[0][1]:
        raise RainIntensityError("new rain-rate schedule unexpectedly matches the 12 mm/h baseline")
    return {
        "baseline_native_input_hashes": dict(sorted(baseline_hashes.items())),
        "case_native_input_hashes": dict(sorted(actual_hashes.items())),
        "changed_native_input_paths": changed,
        "allowed_changed_native_input_paths": sorted(ALLOWED_INPUT_DIFFERENCES),
        "baseline_rain_source_rows": baseline_rows,
        "case_rain_source_rows": actual_rows,
        "all_other_native_inputs_byte_identical": True,
    }


def validate_native_input_audit(
    spec: dict[str, Any],
    case_dir: Path,
    helper: Any,
    baseline_hashes: dict[str, str],
    baseline_z_sha256: str,
    *,
    baseline_rain_path: Path = BASELINE_ROOT / "native/input/field/precipitation_source_all.dat",
) -> tuple[dict[str, Any], dict[str, str], dict[str, Any]]:
    native_input = case_dir / "native" / "input"
    actual_hashes = helper._input_hashes(native_input)
    audit = helper._native_input_audit(spec, case_dir)
    expected_rate = float(spec["rain_rate_m_per_s"])
    comparison = compare_native_inputs(
        baseline_hashes,
        actual_hashes,
        helper=helper,
        baseline_rain_path=baseline_rain_path,
        actual_rain_path=native_input / "field/precipitation_source_all.dat",
        expected_rate=expected_rate,
    )
    if audit.get("z_field_sha256") != baseline_z_sha256:
        raise RainIntensityError("serialized native numerical bed differs from the sealed baseline z.dat")
    expected_runtime = ["0", str(int(spec["duration_s"])), str(int(spec["output_interval_s"])),
                        str(int(spec["duration_s"]))]
    if audit.get("serialized_runtime_values") != expected_runtime:
        raise RainIntensityError("serialized runtime/export schedule differs from the baseline")
    if audit.get("declared_boundary") != "fall":
        raise RainIntensityError("serialized perimeter is not native fall")
    if audit.get("serialized_initial_h_unique_values") != [0.0] or audit.get(
        "serialized_initial_hU_max_abs_m2_per_s"
    ) != 0.0:
        raise RainIntensityError("serialized initial h/hU is not exactly dry and motionless")
    if audit.get("serialized_manning_unique_values") != [0.05]:
        raise RainIntensityError("serialized Manning field differs from the baseline")
    if any(values != [0.0] for values in audit.get("serialized_sink_unique_values", {}).values()):
        raise RainIntensityError("serialized sink or non-rain source term is nonzero")
    if audit.get("serialized_rain_mask_unique_values") != [0.0]:
        raise RainIntensityError("serialized precipitation mask differs from uniform all-cell rain")
    audit["case_native_input_hashes"] = dict(sorted(actual_hashes.items()))
    audit["comparison_to_baseline"] = comparison
    return audit, actual_hashes, comparison


def run_child_with_preflight(
    case_dir: Path,
    spec: dict[str, Any],
    helper: Any,
    baseline_hashes: dict[str, str],
    baseline_z_sha256: str,
    flood_run: Callable[[str], Any],
    *,
    synxflow_version: str,
    python_executable: str,
    baseline_rain_path: Path = BASELINE_ROOT / "native/input/field/precipitation_source_all.dat",
) -> dict[str, Any]:
    """Build/audit inputs, then call stock flood.run only if comparison passes."""
    if synxflow_version != "1.0.1":
        raise RainIntensityError(f"expected SynxFlow 1.0.1, got {synxflow_version}")
    if (case_dir / "native").exists():
        raise RainIntensityError(f"refusing to overwrite prepared native inputs: {case_dir / 'native'}")
    body_started = time.perf_counter()
    helper._make_model_inputs(spec, case_dir)
    audit, before, comparison = validate_native_input_audit(
        spec, case_dir, helper, baseline_hashes, baseline_z_sha256,
        baseline_rain_path=baseline_rain_path,
    )
    preflight = {
        "schema": CASE_PROTOCOL_SCHEMA + ".native-preflight",
        "status": "passed",
        "case": spec,
        "synxflow_version": synxflow_version,
        "python": python_executable,
        "native_input_audit_before_solver": audit,
        "native_input_hashes_before_run": before,
        "comparison_to_baseline": comparison,
        "native_solver_call_is_gated_by_this_record": True,
    }
    write_json(case_dir / "native-preflight.json", preflight)
    write_json(case_dir / "native-launch.json", {
        "schema": CASE_PROTOCOL_SCHEMA + ".native-launch",
        "status": "calling_stock_flood_run",
        "started_at_utc": datetime.now(timezone.utc).isoformat(),
        "case": spec,
        "flood_run_argument": str(case_dir / "native"),
    })
    print("RUNNER_INPUT_HASHES_BEFORE_RUN=" + json.dumps(before, sort_keys=True), flush=True)
    print("RUNNER_NATIVE_INPUT_AUDIT=" + json.dumps(audit, sort_keys=True), flush=True)
    print("RUNNER_NATIVE_SOLVER_CALL_BEGIN=true", flush=True)
    started = time.perf_counter()
    flood_run(str(case_dir / "native"))
    flood_wall = time.perf_counter() - started
    after = helper._input_hashes(case_dir / "native/input")
    if after != before:
        raise RainIntensityError("native input files changed during flood.run")
    postrun = {
        "schema": CASE_PROTOCOL_SCHEMA + ".postrun-inputs",
        "native_input_hashes_after_run": dict(sorted(after.items())),
        "native_inputs_unchanged_during_run": True,
        "flood_run_wall_seconds": flood_wall,
    }
    write_json(case_dir / "postrun-inputs.json", postrun)
    print(f"RUNNER_FLOOD_RUN_WALL_SECONDS={flood_wall:.9f}", flush=True)
    print(f"RUNNER_MODEL_BODY_SECONDS={time.perf_counter() - body_started:.9f}", flush=True)
    return {"preflight": preflight, "postrun": postrun}


def _copy_verified(source: Path, destination: Path, expected_hash: str | None = None) -> dict[str, Any]:
    before = sha256_file(source)
    if expected_hash is not None and before != expected_hash:
        raise RainIntensityError(f"custody source identity mismatch: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    after = sha256_file(source)
    copied = sha256_file(destination)
    if before != after or before != copied:
        raise RainIntensityError(f"custody copy does not match its source: {source}")
    return {
        "repository_source_path": str(source.resolve()),
        "repository_source_sha256_before_copy": before,
        "repository_source_sha256_after_copy": after,
        "archived_copy_path": str(destination.resolve()),
        "archived_copy_sha256": copied,
        "archive_copy_executed": False,
    }


def _current_launch_sources() -> dict[str, dict[str, str]]:
    return {
        "runner": {"path": str(Path(__file__).resolve()), "sha256": sha256_file(Path(__file__).resolve())},
        "reference_helper": {"path": str(REFERENCE_HELPER.resolve()), "sha256": sha256_file(REFERENCE_HELPER)},
    }


def validate_workspace(protocol: dict[str, Any], helper: Any) -> dict[str, Any]:
    """Validate sealed baseline, helper, package, terrain, masks, and DEM."""
    baseline = Path(protocol["baseline_case"])
    if baseline != BASELINE_ROOT or _path_has_symlink_component(baseline):
        raise RainIntensityError("sealed baseline path changed or contains a symlink")
    baseline_spec_path = baseline / "case-spec.json"
    baseline_protocol_path = baseline / "case-protocol.json"
    baseline_result_path = baseline / "case-result.json"
    dem_path = baseline / "DEM.asc"
    expected_hashes = (
        (baseline_spec_path, EXPECTED_BASELINE_CASE_SPEC_SHA256, "baseline case spec"),
        (baseline_protocol_path, EXPECTED_BASELINE_CASE_PROTOCOL_SHA256, "baseline case protocol"),
        (baseline_result_path, EXPECTED_BASELINE_CASE_RESULT_SHA256, "baseline case result"),
        (dem_path, EXPECTED_BASELINE_DEM_SHA256, "baseline DEM"),
        (REFERENCE_HELPER, EXPECTED_HELPER_SHA256, "reference helper"),
        (CONVERTER_SOURCE, EXPECTED_CONVERTER_SHA256, "recording converter"),
    )
    identities = {}
    for path, expected, label in expected_hashes:
        if not path.is_file():
            raise RainIntensityError(f"{label} is missing: {path}")
        actual = sha256_file(path)
        if actual != expected:
            raise RainIntensityError(f"{label} identity changed: {actual}")
        identities[label.replace(" ", "_")] = {"path": str(path.resolve()), "sha256": actual}

    baseline_spec = read_json(baseline_spec_path)
    if baseline_spec != protocol.get("baseline_case_spec"):
        raise RainIntensityError("baseline case specification no longer matches the frozen protocol")
    baseline_case_protocol = read_json(baseline_protocol_path)
    baseline_result = read_json(baseline_result_path)
    if (baseline_case_protocol.get("case") != baseline_spec
            or baseline_result.get("case") != baseline_spec
            or baseline_result.get("status") != "healthy"
            or baseline_result.get("process_exit_code") != 0
            or baseline_result.get("process_timeout") is not False):
        raise RainIntensityError("sealed baseline protocol/result is incomplete or unhealthy")
    baseline_input = baseline_result.get("input_provenance", {})
    baseline_hashes = baseline_input.get("native_input_hashes_before_run")
    baseline_hashes_after = baseline_input.get("native_input_hashes_after_run")
    if not isinstance(baseline_hashes, dict) or baseline_hashes != baseline_hashes_after:
        raise RainIntensityError("sealed baseline lacks stable before/after native input hashes")
    actual_baseline_hashes = helper._input_hashes(baseline / "native/input")
    if actual_baseline_hashes != baseline_hashes:
        raise RainIntensityError("sealed baseline native input files changed")
    baseline_audit = baseline_input.get("native_input_audit_before_solver")
    if not isinstance(baseline_audit, dict):
        raise RainIntensityError("sealed baseline has no before-solver native input audit")
    baseline_z_path = baseline / "native/input/field/z.dat"
    baseline_z_hash = sha256_file(baseline_z_path)
    if (baseline_audit.get("z_field_sha256") != baseline_z_hash
            or baseline_hashes.get("field/z.dat") != baseline_z_hash):
        raise RainIntensityError("sealed baseline z.dat hash disagrees with its native audit")

    backend = helper.inspect_source_identity()
    if (backend != protocol.get("native_backend_identity")
            or backend != baseline_case_protocol.get("native_backend_identity")
            or backend != baseline_result.get("source_identity")):
        raise RainIntensityError("released SynxFlow backend identity differs from frozen baseline")
    _verify_digest_map(protocol.get("installed_synxflow_payload_files", {}), "installed SynxFlow payload")
    if not protocol.get("installed_synxflow_payload_files"):
        raise RainIntensityError("frozen protocol omits installed SynxFlow payload identities")

    recorded_helper = protocol.get("reference_helper_sha256")
    if recorded_helper != EXPECTED_HELPER_SHA256:
        raise RainIntensityError("frozen protocol helper source identity is incomplete")
    archived_runner = baseline_case_protocol.get("harness_source_identity", {}).get("copies", {}).get("runner", {})
    if (archived_runner.get("repository_source_sha256_before_copy") != EXPECTED_HELPER_SHA256
            or archived_runner.get("archived_copy_sha256") != EXPECTED_HELPER_SHA256):
        raise RainIntensityError("baseline launch helper identity differs from the repository source")
    _, source_bed, _, terrain_identity = helper.mountain_source_arrays()
    if terrain_identity != protocol.get("terrain_source_identity"):
        raise RainIntensityError("immutable terrain source identity differs from the frozen protocol")
    if terrain_identity != baseline_case_protocol.get("terrain_source_identity"):
        raise RainIntensityError("immutable terrain identity differs from the sealed baseline")
    depression_masks, depression_identity = helper.fixed_depression_masks()
    if (depression_identity != protocol.get("depression_mask_identity")
            or depression_identity != baseline_case_protocol.get("depression_mask_identity")):
        raise RainIntensityError("fixed analytical depression masks differ from frozen baseline identities")
    if baseline_case_protocol.get("input_dem_ascii_sha256") != EXPECTED_BASELINE_DEM_SHA256:
        raise RainIntensityError("baseline case protocol DEM hash changed")

    dem_header, dem_values, dem_valid = helper.read_ascii(dem_path)
    if (dem_values.shape != source_bed.shape or not dem_valid.all()
            or dem_header.get("cellsize") != 30.0
            or float(helper.np.max(helper.np.abs(dem_values - source_bed))) > 1e-8):
        raise RainIntensityError("sealed baseline DEM no longer represents the immutable terrain crop")
    source_identity = {
        "native_backend_identity": backend,
        "terrain_source_identity": terrain_identity,
        "depression_mask_identity": depression_identity,
        "baseline_case_protocol_sha256": EXPECTED_BASELINE_CASE_PROTOCOL_SHA256,
        "baseline_case_result_sha256": EXPECTED_BASELINE_CASE_RESULT_SHA256,
        "baseline_native_input_hashes": dict(sorted(baseline_hashes.items())),
        "baseline_native_z_field_sha256": baseline_z_hash,
        "baseline_dem_sha256": EXPECTED_BASELINE_DEM_SHA256,
        "validated_source_files": identities,
    }
    return {
        "baseline_spec": baseline_spec,
        "baseline_case_protocol": baseline_case_protocol,
        "baseline_result": baseline_result,
        "baseline_hashes": baseline_hashes,
        "baseline_z_sha256": baseline_z_hash,
        "source_bed": source_bed,
        "rois": helper.fixed_roi_masks(),
        "depression_masks": depression_masks,
        "backend_identity": backend,
        "terrain_identity": terrain_identity,
        "depression_identity": depression_identity,
        "source_identity": source_identity,
    }


def _case_protocol(
    protocol: dict[str, Any],
    protocol_hash: str,
    spec: dict[str, Any],
    context: dict[str, Any],
    dem_hash: str,
    helper: Any,
    harness_identity: dict[str, Any],
) -> dict[str, Any]:
    return {
        "schema": CASE_PROTOCOL_SCHEMA,
        "frozen_before_native_execution": True,
        "frozen_at_utc": protocol["frozen_at_utc"],
        "parent_protocol_path": str(DEFAULT_PROTOCOL.resolve()),
        "parent_protocol_sha256": protocol_hash,
        "case": spec,
        "input_dem_ascii_sha256": dem_hash,
        "expected_saved_times_s": helper.time_grid(spec),
        "native_backend_identity": context["backend_identity"],
        "terrain_source_identity": context["terrain_identity"],
        "depression_mask_identity": context["depression_identity"],
        "harness_source_identity": harness_identity,
        "settings": {
            "grid_shape_zx": [int(spec["rows"]), int(spec["cols"])],
            "cell_size_m": float(spec["dx_m"]),
            "duration_s": int(spec["duration_s"]),
            "output_interval_s": int(spec["output_interval_s"]),
            "initial_h_hUx_hUy": [0.0, 0.0, 0.0],
            "rain_rate_mm_per_hour": int(round(float(spec["rain_rate_m_per_s"]) * 3_600_000)),
            "rain_source_schedule": [[0.0, float(spec["rain_rate_m_per_s"])],
                                     [float(spec["rain_history_end_s"]), float(spec["rain_rate_m_per_s"])]],
            "manning_n": 0.05,
            "boundary": "native fall all perimeter",
            "all_sinks_and_non_rain_terms_zero": True,
            "unchanged_input_paths_byte_identical_to_baseline_except": [
                "field/precipitation_source_all.dat"
            ],
            "direct_rain_normalization": "excess and source volumes use this case's nominal R*t",
            "saved_fields": ["h", "hUx", "hUy"],
            "export_serialization": "%f with six fractional decimal places",
            "storage_upper_bound_semantics": (
                "saved storage <= R*t*512*512*900 + 512*512*900*0.5e-6m3 "
                "+ small floating-point accounting tolerance"
            ),
            "unobservable": [
                "native applied face flux",
                "intermediate-stage positivity/clipping history",
                "exact pre-serialization native extrema",
            ],
            "ponds_are_diagnostic_only": True,
        },
        "interpretation": protocol["interpretation"],
    }


def _archive_harness(case_dir: Path) -> dict[str, Any]:
    harness = case_dir / "harness"
    if harness.exists():
        raise RainIntensityError(f"refusing to overwrite harness archive: {harness}")
    harness.mkdir()
    copies = {
        "runner": _copy_verified(Path(__file__).resolve(), harness / Path(__file__).name),
        "reference_helper": _copy_verified(REFERENCE_HELPER, harness / REFERENCE_HELPER.name,
                                           EXPECTED_HELPER_SHA256),
        "reference_tests": _copy_verified(REFERENCE_TESTS, harness / REFERENCE_TESTS.name),
        "intensity_tests": _copy_verified(Path(__file__).with_name("test_run_native_rain_intensity_v1.py"),
                                           harness / "test_run_native_rain_intensity_v1.py"),
    }
    return {
        "actual_launch_sources": None,
        "custody_archive_copies": copies,
        "archive_copies_are_not_launch_sources": True,
        "verified_identical_before_native_execution": True,
    }


def _ensure_phase_protocol(out_root: Path, protocol_path: Path, protocol_hash: str) -> Path:
    destination = out_root / "phase-protocol.json"
    if destination.exists() or destination.is_symlink():
        if destination.is_symlink() or not destination.is_file() or sha256_file(destination) != protocol_hash:
            raise RainIntensityError("existing worker phase protocol is not the frozen parent protocol")
    else:
        _copy_verified(protocol_path, destination, protocol_hash)
    return destination


def _study_space(protocol: dict[str, Any]) -> dict[str, Any]:
    root = EXPERIMENT_ROOT if EXPERIMENT_ROOT.exists() else EXPERIMENT_ROOT.parent
    usage = shutil.disk_usage(root)
    total_bytes = sum(
        path.lstat().st_size for path in EXPERIMENT_ROOT.rglob("*")
        if path.is_file() and not path.is_symlink()
    ) if EXPERIMENT_ROOT.exists() else 0
    limits = protocol["limits"]
    return {
        "experiment_root": str(EXPERIMENT_ROOT),
        "experiment_tree_bytes": total_bytes,
        "study_disk_budget_bytes": int(limits["study_disk_budget_bytes"]),
        "free_bytes": usage.free,
        "minimum_free_bytes": int(limits["minimum_free_bytes"]),
        "passed": (total_bytes <= int(limits["study_disk_budget_bytes"])
                   and usage.free >= int(limits["minimum_free_bytes"])),
    }


def _require_study_space(protocol: dict[str, Any]) -> dict[str, Any]:
    status = _study_space(protocol)
    if not status["passed"]:
        raise RainIntensityError(
            f"experiment disk/free-space gate failed: tree={status['experiment_tree_bytes']} bytes, "
            f"free={status['free_bytes']} bytes"
        )
    return status


def _write_phase_summary(out_root: Path, protocol_hash: str, cases: list[dict[str, Any]],
                         phase_status: str, stop_reason: str | None = None) -> dict[str, Any]:
    summary = {
        "schema": PHASE_SUMMARY_SCHEMA,
        "updated_at_utc": datetime.now(timezone.utc).isoformat(),
        "protocol_path": str(DEFAULT_PROTOCOL.resolve()),
        "protocol_sha256": protocol_hash,
        "status": phase_status,
        "stop_reason": stop_reason,
        "cases": cases,
    }
    write_json(out_root / "phase-summary.json", summary)
    return summary


def prepare_cases(protocol: dict[str, Any], protocol_hash: str, rates: tuple[int, ...],
                  out: Path = WORKER_ROOT, *, helper: Any | None = None,
                  allowed_root: Path = WORKER_ROOT) -> dict[str, Any]:
    helper = helper or _load_reference_helper()
    out_root = checked_output_root(out, allowed_root)
    _require_study_space(protocol)
    context = validate_workspace(protocol, helper)
    specs = case_specs(protocol, rates)
    targets = [case_output_path(out_root, str(spec["name"])) for spec in specs]
    for target in targets:
        assert_fresh_case_directory(target)
    out_root.mkdir(parents=True, exist_ok=True)
    _ensure_phase_protocol(out_root, DEFAULT_PROTOCOL, protocol_hash)
    (out_root / "cases").mkdir(exist_ok=True)
    case_records = []
    for spec, case_dir in zip(specs, targets):
        prepare_started = time.perf_counter()
        case_dir.mkdir()
        shutil.copyfile(BASELINE_ROOT / "DEM.asc", case_dir / "DEM.asc")
        dem_hash = sha256_file(case_dir / "DEM.asc")
        if dem_hash != EXPECTED_BASELINE_DEM_SHA256:
            raise RainIntensityError("copied case DEM differs from the sealed baseline")
        helper.write_json(case_dir / "case-spec.json", spec)
        harness_identity = _archive_harness(case_dir)
        write_json(case_dir / "harness-provenance.json", harness_identity)
        case_protocol = _case_protocol(protocol, protocol_hash, spec, context, dem_hash, helper,
                                       harness_identity)
        helper.write_json(case_dir / "case-protocol.json", case_protocol)
        prepare_record = {
            "schema": CASE_PROTOCOL_SCHEMA + ".prepare-result",
            "status": "prepared",
            "case": spec,
            "case_dir": str(case_dir.resolve()),
            "dem_sha256": dem_hash,
            "parent_protocol_sha256": protocol_hash,
            "source_identity": context["source_identity"],
            "native_solver_launched": False,
            "prepare_wall_seconds": time.perf_counter() - prepare_started,
        }
        helper.write_json(case_dir / "prepare-result.json", prepare_record)
        case_records.append({
            "rain_mm_per_hour": int(round(spec["rain_rate_m_per_s"] * 3_600_000)),
            "case_name": spec["name"],
            "case_dir": str(case_dir.resolve()),
            "case_result_path": str((case_dir / "case-result.json").resolve()),
            "status": "prepared",
            "native_solver_launched": False,
        })
    _require_study_space(protocol)
    return _write_phase_summary(out_root, protocol_hash, case_records, "prepared")


def _validate_prepared_case(case_dir: Path, protocol: dict[str, Any], protocol_hash: str,
                            expected_spec: dict[str, Any]) -> None:
    if not case_dir.is_dir() or case_dir.is_symlink():
        raise RainIntensityError(f"prepared case directory is missing or unsafe: {case_dir}")
    if read_json(case_dir / "case-spec.json") != expected_spec:
        raise RainIntensityError("prepared case-spec.json differs from the frozen rate/name/purpose")
    case_protocol = read_json(case_dir / "case-protocol.json")
    if (case_protocol.get("case") != expected_spec
            or case_protocol.get("parent_protocol_sha256") != protocol_hash
            or case_protocol.get("input_dem_ascii_sha256") != EXPECTED_BASELINE_DEM_SHA256
            or case_protocol.get("terrain_source_identity") != protocol.get("terrain_source_identity")):
        raise RainIntensityError("prepared case protocol no longer matches frozen inputs")
    if sha256_file(case_dir / "DEM.asc") != EXPECTED_BASELINE_DEM_SHA256:
        raise RainIntensityError("prepared case DEM changed after preparation")
    if (case_dir / "native").exists() or (case_dir / "case-result.json").exists():
        raise RainIntensityError("refusing to rerun or repair an existing native case")


def _storage_upper_bound(spec: dict[str, Any], timestamp: float, depth: Any, helper: Any) -> dict[str, Any]:
    np = helper.np
    rows, cols = int(spec["rows"]), int(spec["cols"])
    area = float(spec["dx_m"]) ** 2
    cells = rows * cols
    observed = float(np.sum(depth, dtype=np.float64) * area)
    source = float(spec["rain_rate_m_per_s"]) * float(timestamp) * cells * area
    quantization = cells * area * ASC_HALF_QUANTIZATION_M
    tolerance = quantization + max(1e-8, abs(source) * 1e-12)
    return {
        "time_s": float(timestamp),
        "saved_water_volume_m3": observed,
        "direct_rain_source_volume_m3": source,
        "global_export_quantization_bound_m3": quantization,
        "small_floating_point_tolerance_m3": max(1e-8, abs(source) * 1e-12),
        "maximum_allowed_saved_water_volume_m3": source + tolerance,
        "passed": math.isfinite(observed) and observed <= source + tolerance,
        "interpretation": "storage minus source is not an outlet/conservation ledger",
    }


def evaluate_export_health(spec: dict[str, Any], snapshots: dict[float, dict[str, Any]], helper: Any) -> dict[str, Any]:
    """Apply independent rain-run health gates, including exact dry start."""
    np = helper.np
    expected = [float(value) for value in helper.time_grid(spec)]
    actual = sorted(float(value) for value in snapshots)
    issues: list[str] = []
    if actual != expected:
        issues.append("saved exports are missing, duplicated, or out of the frozen time grid")
    shape = (int(spec["rows"]), int(spec["cols"]))
    upper_bounds = []
    all_finite = True
    all_nonnegative = True
    dry_zero = True
    all_dry_q_zero = True
    for timestamp in actual:
        fields = snapshots[timestamp]
        h = np.asarray(fields["h"])
        hux = np.asarray(fields["hUx"])
        huy = np.asarray(fields["hUy"])
        if h.shape != shape or hux.shape != shape or huy.shape != shape:
            issues.append(f"unexpected exported field shape at {timestamp:g}s")
            continue
        finite = bool(np.isfinite(h).all() and np.isfinite(hux).all() and np.isfinite(huy).all())
        nonnegative = bool(np.all(h >= 0.0)) if np.isfinite(h).all() else False
        all_finite = all_finite and finite
        all_nonnegative = all_nonnegative and nonnegative
        if not finite:
            issues.append(f"non-finite h/hUx/hUy value at {timestamp:g}s")
        if not nonnegative:
            issues.append(f"negative or non-finite depth at {timestamp:g}s")
        dry_nonzero = bool(np.any(h == 0.0) and np.any(
            (h == 0.0) & ((hux != 0.0) | (huy != 0.0))
        ))
        all_dry_q_zero = all_dry_q_zero and not dry_nonzero
        if dry_nonzero:
            issues.append(f"zero-depth cell has nonzero momentum at {timestamp:g}s")
        if timestamp == 0.0:
            dry_zero = bool(np.all(h == 0.0) and np.all(hux == 0.0) and np.all(huy == 0.0))
            if not dry_zero:
                issues.append("initial saved h/hUx/hUy are not exactly zero")
        if finite and nonnegative:
            upper = _storage_upper_bound(spec, timestamp, h, helper)
            upper_bounds.append(upper)
            if not upper["passed"]:
                issues.append(f"saved water exceeds direct-rain upper bound at {timestamp:g}s")
    upper_passed = len(upper_bounds) == len(expected) and all(item["passed"] for item in upper_bounds)
    if not upper_passed and len(upper_bounds) == len(expected):
        # A per-time failure is already described above.
        pass
    return {
        "status": "healthy" if not issues else "health_failed",
        "gate_passed": not issues,
        "issues": issues,
        "times_s": actual,
        "expected_times_s": expected,
        "grid_shape": list(shape),
        "expected_saved_export_count": len(expected),
        "actual_saved_export_count": len(actual),
        "all_saved_h_finite_nonnegative": all_finite and all_nonnegative,
        "all_saved_hUx_hUy_finite": all_finite,
        "exact_dry_motionless_time_zero": dry_zero,
        "no_zero_depth_nonzero_momentum": all_dry_q_zero,
        "storage_upper_bound_passed_at_all_exports": upper_passed,
        "storage_upper_bound_observations": upper_bounds,
        "storage_upper_bound_formula": (
            "R*t*512*512*900 + 512*512*900*0.5e-6m3 + small floating-point accounting tolerance"
        ),
        "storage_minus_source_is_outlet_or_conservation_ledger": False,
        "serialization": "%f with six fractional decimal places",
    }


def _summary_case(rate: int, spec: dict[str, Any], case_dir: Path, result: dict[str, Any] | None,
                  status: str, message: str | None = None) -> dict[str, Any]:
    health = result.get("output_health") if isinstance(result, dict) else None
    metrics = result.get("metrics") if isinstance(result, dict) else None
    final_global = None
    corridor = None
    pond = None
    if isinstance(metrics, dict):
        observations = metrics.get("global_observations", [])
        final_global = observations[-1] if observations else None
        corridor = metrics.get("corridor_900s_support_criterion_met")
        pond = metrics.get("pond_15min_criteria_met_by_frozen_depression")
    return {
        "rain_mm_per_hour": rate,
        "case_name": spec["name"],
        "case_dir": str(case_dir.resolve()),
        "case_result_path": str((case_dir / "case-result.json").resolve()),
        "status": status,
        "message": message,
        "output_health": ({
            "gate_passed": health.get("gate_passed"),
            "actual_saved_export_count": health.get("actual_saved_export_count"),
            "exact_dry_motionless_time_zero": health.get("exact_dry_motionless_time_zero"),
            "storage_upper_bound_passed_at_all_exports": health.get("storage_upper_bound_passed_at_all_exports"),
        } if isinstance(health, dict) else None),
        "final_global_observation": ({
            key: final_global.get(key)
            for key in ("time_s", "saved_water_volume_m3", "direct_uniform_rain_volume_m3",
                        "saved_minus_direct_rain_volume_m3", "saved_depth_max_m",
                        "material_h_ge_0.01_cell_count", "all_wet_water_volume_weighted_export_derived_speed_m_per_s")
            if key in final_global
        } if isinstance(final_global, dict) else None),
        "corridor_900s_support_criterion_met": corridor,
        "quiet_pond_15min_criteria_by_fixed_depression": pond,
        "pond_classification_is_diagnostic_only": True,
    }


def _failure_result(spec: dict[str, Any], case_dir: Path, protocol_hash: str, status: str,
                    message: str, process_exit_code: int | None = None,
                    process_timeout: bool = False, process_wall: float | None = None) -> dict[str, Any]:
    result = {
        "schema": CASE_RESULT_SCHEMA,
        "case": spec,
        "case_protocol_path": str((case_dir / "case-protocol.json").resolve()),
        "parent_protocol_sha256": protocol_hash,
        "status": status,
        "failure": message,
        "process_exit_code": process_exit_code,
        "process_timeout": process_timeout,
        "process_wall_seconds": process_wall,
        "native_solver_launched": bool((case_dir / "native-preflight.json").is_file()),
        "output_health": None,
        "metrics": None,
        "input_provenance": {
            "case_dem_ascii_sha256": sha256_file(case_dir / "DEM.asc") if (case_dir / "DEM.asc").is_file() else None,
            "native_input_audit_before_solver": (
                read_json(case_dir / "native-preflight.json").get("native_input_audit_before_solver")
                if (case_dir / "native-preflight.json").is_file() else None
            ),
            "native_input_hashes_before_run": (
                read_json(case_dir / "native-preflight.json").get("native_input_hashes_before_run")
                if (case_dir / "native-preflight.json").is_file() else None
            ),
            "native_input_hashes_after_run": (
                read_json(case_dir / "postrun-inputs.json").get("native_input_hashes_after_run")
                if (case_dir / "postrun-inputs.json").is_file() else None
            ),
        },
    }
    write_json(case_dir / "case-result.json", result)
    return result


def run_one_case(protocol: dict[str, Any], protocol_hash: str, rate: int,
                 out_root: Path = WORKER_ROOT, *, helper: Any | None = None,
                 allowed_root: Path = WORKER_ROOT) -> dict[str, Any]:
    helper = helper or _load_reference_helper()
    if Path(os.path.abspath(sys.executable)) != Path(os.path.abspath(NATIVE_PYTHON)):
        raise RainIntensityError(f"native cases must use the pinned interpreter: {NATIVE_PYTHON}")
    if not Path(sys.executable).is_file() or not NATIVE_PYTHON.is_file():
        raise RainIntensityError(f"pinned SynxFlow Python interpreter is missing: {NATIVE_PYTHON}")
    out_root = checked_output_root(out_root, allowed_root)
    spec = case_specs(protocol, (rate,))[0]
    case_dir = case_output_path(out_root, spec["name"])
    _validate_prepared_case(case_dir, protocol, protocol_hash, spec)
    context = validate_workspace(protocol, helper)
    _require_study_space(protocol)
    if (case_dir / "run-state.json").exists() or (case_dir / "native-preflight.json").exists():
        raise RainIntensityError("refusing to rerun a case that already entered native execution")

    source_before = _current_launch_sources()
    archived = read_json(case_dir / "harness-provenance.json")["custody_archive_copies"]
    for name, descriptor in source_before.items():
        if descriptor["sha256"] != archived[name]["archived_copy_sha256"]:
            raise RainIntensityError(f"{name} changed after case preparation")
    write_json(case_dir / "run-state.json", {
        "schema": CASE_PROTOCOL_SCHEMA + ".run-state",
        "status": "running",
        "started_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_identity_before_launch": source_before,
        "protocol_sha256": protocol_hash,
        "native_solver_launched": False,
    })
    command = [
        str(NATIVE_PYTHON), "-u", str(Path(__file__).resolve()),
        "--child", str(case_dir.resolve()), "--protocol", str(DEFAULT_PROTOCOL.resolve()),
        "--expected-protocol-sha256", protocol_hash,
    ]
    started = time.monotonic()
    timed_out = False
    exit_code: int | None = None
    stdout = ""
    stderr = ""
    cache = case_dir / "cache"
    cache.mkdir()
    try:
        completed = subprocess.run(
            command,
            cwd=case_dir,
            env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1",
                 "MPLCONFIGDIR": str(cache / "matplotlib"),
                 "XDG_CACHE_HOME": str(cache / "xdg")},
            capture_output=True,
            text=True,
            timeout=CASE_TIMEOUT_S,
            check=False,
        )
        stdout, stderr, exit_code = completed.stdout, completed.stderr, completed.returncode
    except subprocess.TimeoutExpired as error:
        timed_out = True
        stdout = error.stdout.decode("utf-8", errors="replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
        stderr = error.stderr.decode("utf-8", errors="replace") if isinstance(error.stderr, bytes) else (error.stderr or "")
    process_wall = time.monotonic() - started
    (case_dir / "native.stdout").write_text(stdout, encoding="utf-8")
    (case_dir / "native.stderr").write_text(stderr, encoding="utf-8")
    source_after = _current_launch_sources()
    launch_provenance = {
        "schema": CASE_PROTOCOL_SCHEMA + ".harness-provenance",
        "actual_launch_sources": {
            "runner": {
                "path": source_before["runner"]["path"],
                "sha256_before_launch": source_before["runner"]["sha256"],
                "sha256_after_child_exit": source_after["runner"]["sha256"],
                "executed": True,
            },
            "reference_helper": {
                "path": source_before["reference_helper"]["path"],
                "sha256_before_launch": source_before["reference_helper"]["sha256"],
                "sha256_after_child_exit": source_after["reference_helper"]["sha256"],
                "imported_by_child_at_repository_source_path": True,
            },
        },
        "custody_archive_copies": read_json(case_dir / "harness-provenance.json")["custody_archive_copies"],
        "archive_copies_are_not_launch_sources": True,
        "source_identity_stable": source_before == source_after,
    }
    write_json(case_dir / "harness-provenance.json", launch_provenance)

    success_marker = "Simulation successfully finished!" in stdout
    failure = essential_process_failure(
        exit_code,
        timed_out,
        (case_dir / "native-preflight.json").is_file(),
        (case_dir / "postrun-inputs.json").is_file(),
        launch_provenance["source_identity_stable"],
        success_marker,
    )

    if failure is not None:
        result = _failure_result(spec, case_dir, protocol_hash, failure[0], failure[1], exit_code,
                                 timed_out, process_wall)
        result["source_identity"] = context["backend_identity"]
        result["raw_stdout"] = str((case_dir / "native.stdout").resolve())
        result["raw_stderr"] = str((case_dir / "native.stderr").resolve())
        write_json(case_dir / "case-result.json", result)
        return result

    preflight = read_json(case_dir / "native-preflight.json")
    postrun = read_json(case_dir / "postrun-inputs.json")
    input_unchanged = (
        preflight.get("native_input_hashes_before_run") == postrun.get("native_input_hashes_after_run")
        and postrun.get("native_inputs_unchanged_during_run") is True
    )
    snapshots = None
    saved_health = None
    metrics = None
    output_hashes: dict[str, str] = {}
    output_failure: str | None = None
    output_validation_started = time.monotonic()
    try:
        snapshots, saved_health = helper.read_case_snapshots(case_dir / "native/output", spec)
        explicit_health = evaluate_export_health(spec, snapshots, helper)
        saved_health.update(explicit_health)
        timestep_log = helper.parse_timestep_log(
            case_dir / "native/output/timestep_log.txt", float(spec["duration_s"])
        )
        if not explicit_health["gate_passed"]:
            output_failure = "; ".join(explicit_health["issues"])
        _, native_bed = helper._grid_from_native_ids(
            case_dir / "native/input/field/z.dat", int(spec["rows"]), int(spec["cols"])
        )
        metrics = helper.mountain_metrics(
            spec,
            snapshots,
            context["source_bed"],
            native_bed,
            context["rois"],
            context["depression_masks"],
        )
        output_hashes = helper._field_output_hashes(case_dir / "native/output")
    except Exception as error:  # retain raw native artifacts and report their failed gate.
        output_failure = f"{type(error).__name__}: {error}"
        timestep_log = None
    output_validation_wall = time.monotonic() - output_validation_started
    if not input_unchanged:
        output_failure = (output_failure + "; " if output_failure else "") + "native inputs changed during solver run"
    healthy = output_failure is None and bool(saved_health and saved_health.get("gate_passed"))
    source_identity_after = helper.inspect_source_identity()
    if source_identity_after != context["backend_identity"]:
        healthy = False
        output_failure = (output_failure + "; " if output_failure else "") + "SynxFlow backend identity drifted"
    space_after = _study_space(protocol)
    if not space_after["passed"]:
        healthy = False
        output_failure = (output_failure + "; " if output_failure else "") + "study disk/free-space budget exceeded"
    prepare_record = read_json(case_dir / "prepare-result.json")
    prepare_wall = float(prepare_record.get("prepare_wall_seconds", 0.0))
    result = {
        "schema": CASE_RESULT_SCHEMA,
        "case": spec,
        "case_protocol_path": str((case_dir / "case-protocol.json").resolve()),
        "parent_protocol_sha256": protocol_hash,
        "working_directory": str(case_dir.resolve()),
        "command": command,
        "python": _marker_value(stdout, "RUNNER_PYTHON=") or str(NATIVE_PYTHON),
        "synxflow_version": "1.0.1",
        "native_gpu_success_marker": success_marker,
        "status": "healthy" if healthy else "health_failed",
        "study_space_after_case": space_after,
        "failure": output_failure,
        "process_exit_code": exit_code,
        "process_timeout": timed_out,
        "process_wall_seconds": process_wall,
        "child_process_wall_seconds_including_imports": process_wall,
        "body_seconds_after_package_imports": _marker_value(stdout, "RUNNER_MODEL_BODY_SECONDS="),
        "flood_run_wall_seconds_after_imports_and_input_write": (
            postrun.get("flood_run_wall_seconds") if isinstance(postrun, dict) else None
        ),
        "output_validation_wall_seconds": output_validation_wall,
        "case_end_to_end_wall_seconds_including_dem_write_and_output_checks": (
            prepare_wall + process_wall + output_validation_wall
        ),
        "case_end_to_end_timing_scope": (
            "sum of measured case preparation including DEM copy, child imports/input/native run, and output parsing, "
            "health checks, timestep validation, output hashes and metrics; excludes workspace preflight, human "
            "pause between prepare/run, and result JSON serialization"
        ),
        "native_solver_launched": (case_dir / "native-launch.json").is_file(),
        "source_identity": context["backend_identity"],
        "source_identity_after_run": source_identity_after,
        "harness_provenance_path": str((case_dir / "harness-provenance.json").resolve()),
        "raw_stdout": str((case_dir / "native.stdout").resolve()),
        "raw_stderr": str((case_dir / "native.stderr").resolve()),
        "output_health": saved_health,
        "output_hashes": output_hashes,
        "timestep_log": timestep_log,
        "input_provenance": {
            "case_dem_ascii_sha256": sha256_file(case_dir / "DEM.asc"),
            "case_source_bed_semantics": context["terrain_identity"]["transform"],
            "native_input_audit_before_solver": preflight["native_input_audit_before_solver"],
            "native_input_hashes_before_run": preflight["native_input_hashes_before_run"],
            "native_input_hashes_after_run": postrun["native_input_hashes_after_run"],
            "native_inputs_unchanged_during_run": input_unchanged,
            "native_input_comparison_to_baseline": preflight["comparison_to_baseline"],
            "native_input_audit_path": str((case_dir / "native-preflight.json").resolve()),
            "terrain_source_identity": context["terrain_identity"],
        },
        "metrics": metrics,
        "interpretation": protocol["interpretation"],
        "unobservable": [
            "native applied face flux",
            "intermediate-stage positivity/clipping history",
            "exact pre-serialization native extrema",
        ],
        "direct_rain_normalization": "R*t using this case's nominal constant rain rate",
        "storage_minus_source_is_outlet_or_conservation_ledger": False,
        "pond_classification_is_diagnostic_only": True,
    }
    write_json(case_dir / "case-result.json", result)
    return result


def run_phase(protocol: dict[str, Any], protocol_hash: str, rates: tuple[int, ...],
              out: Path = WORKER_ROOT, *, helper: Any | None = None,
              allowed_root: Path = WORKER_ROOT) -> dict[str, Any]:
    helper = helper or _load_reference_helper()
    out_root = checked_output_root(out, allowed_root)
    case_records = []
    specs_by_rate = {rate: spec for rate, spec in zip(rates, case_specs(protocol, rates))}
    if rates == (120,):
        first = case_specs(protocol, (48,))[0]
        first_dir = case_output_path(out_root, first["name"])
        if not (first_dir / "case-result.json").is_file():
            raise RainIntensityError("120 mm/h cannot run before a healthy 48 mm/h result")
        first_result = read_json(first_dir / "case-result.json")
        if first_result.get("status") != "healthy" or first_result.get("process_timeout") is not False:
            raise RainIntensityError("120 mm/h cannot run after an unhealthy or timed-out 48 mm/h case")
        case_records.append(_summary_case(48, first, first_dir, first_result, first_result["status"]))

    for index, rate in enumerate(rates):
        spec = specs_by_rate[rate]
        case_dir = case_output_path(out_root, spec["name"])
        try:
            result = run_one_case(protocol, protocol_hash, rate, out_root, helper=helper,
                                  allowed_root=allowed_root)
            summary_record = _summary_case(rate, spec, case_dir, result, result["status"], result.get("failure"))
            case_records.append(summary_record)
            if result["status"] != "healthy":
                remaining = rates[index + 1:]
                if remaining:
                    for skipped_rate in remaining:
                        skipped_spec = specs_by_rate[skipped_rate]
                        skipped_dir = case_output_path(out_root, skipped_spec["name"])
                        case_records.append(_summary_case(
                            skipped_rate, skipped_spec, skipped_dir, None, "not_run",
                            f"stopped after {rate} mm/h essential health/input failure",
                        ))
                return _write_phase_summary(
                    out_root, protocol_hash, case_records, "stopped",
                    f"{rate} mm/h did not pass essential execution/input/output health",
                )
        except Exception as error:
            if not any(record["rain_mm_per_hour"] == rate for record in case_records):
                case_records.append(_summary_case(rate, spec, case_dir, None, "blocked", str(error)))
            for skipped_rate in rates[index + 1:]:
                skipped_spec = specs_by_rate[skipped_rate]
                skipped_dir = case_output_path(out_root, skipped_spec["name"])
                case_records.append(_summary_case(skipped_rate, skipped_spec, skipped_dir, None,
                                                  "not_run", f"stopped before launch: {error}"))
            return _write_phase_summary(out_root, protocol_hash, case_records, "stopped", str(error))
    return _write_phase_summary(out_root, protocol_hash, case_records, "complete")


def child_main(case_dir: Path, protocol_path: Path, expected_protocol_sha256: str) -> int:
    protocol, protocol_hash = load_protocol(protocol_path, expected_protocol_sha256)
    if Path(os.path.abspath(sys.executable)) != Path(os.path.abspath(NATIVE_PYTHON)):
        raise RainIntensityError("native child is not running under the pinned SynxFlow Python interpreter")
    if _path_has_symlink_component(case_dir):
        raise RainIntensityError("native case path contains a symlink")
    if case_dir.resolve().parent != (WORKER_ROOT / "cases").resolve():
        raise RainIntensityError("native child case is outside the assigned case directory")
    helper = _load_reference_helper()
    context = validate_workspace(protocol, helper)
    spec = read_json(case_dir / "case-spec.json")
    expected_specs = case_specs(protocol, EXPECTED_RATES_MM_PER_HOUR)
    if spec not in expected_specs:
        raise RainIntensityError("native child case spec does not match a frozen rate/name/purpose")
    case_protocol = read_json(case_dir / "case-protocol.json")
    if (case_protocol.get("case") != spec or case_protocol.get("parent_protocol_sha256") != protocol_hash
            or case_protocol.get("input_dem_ascii_sha256") != EXPECTED_BASELINE_DEM_SHA256):
        raise RainIntensityError("native child case protocol does not match frozen prepared inputs")
    if sha256_file(case_dir / "DEM.asc") != EXPECTED_BASELINE_DEM_SHA256:
        raise RainIntensityError("native child DEM differs from sealed baseline")

    from synxflow import __version__, flood

    record = run_child_with_preflight(
        case_dir,
        spec,
        helper,
        context["baseline_hashes"],
        context["baseline_z_sha256"],
        flood.run,
        synxflow_version=__version__,
        python_executable=str(NATIVE_PYTHON),
    )
    print("RUNNER_NATIVE_PREFLIGHT=" + json.dumps(record["preflight"], sort_keys=True), flush=True)
    print("RUNNER_NATIVE_POSTRUN=" + json.dumps(record["postrun"], sort_keys=True), flush=True)
    return 0


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--prepare", action="store_true", help="prepare frozen case folders; never launches SynxFlow")
    mode.add_argument("--run", action="store_true", help="run prepared cases; requires explicit --go")
    mode.add_argument("--child", type=Path, help=argparse.SUPPRESS)
    selection = parser.add_mutually_exclusive_group(required=False)
    selection.add_argument("--rate", type=int, choices=EXPECTED_RATES_MM_PER_HOUR)
    selection.add_argument("--phase", choices=("all",))
    parser.add_argument("--out", type=Path, default=WORKER_ROOT,
                        help="worker output directory (must remain under the assigned worker root)")
    parser.add_argument("--protocol", type=Path, default=DEFAULT_PROTOCOL,
                        help="parent-frozen phase protocol JSON")
    parser.add_argument("--go", action="store_true", help="required explicit authorization to launch the native child")
    parser.add_argument("--expected-protocol-sha256", default=EXPECTED_PROTOCOL_SHA256, help=argparse.SUPPRESS)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    try:
        if args.child is not None:
            child_main(args.child, args.protocol, args.expected_protocol_sha256)
            return 0
        rates = selected_rates(args.rate, args.phase)
        protocol, protocol_hash = load_protocol(args.protocol, EXPECTED_PROTOCOL_SHA256)
        helper = _load_reference_helper()
        if args.prepare:
            summary = prepare_cases(protocol, protocol_hash, rates, args.out, helper=helper)
            print(json.dumps({"status": summary["status"], "cases": summary["cases"]}, indent=2))
            return 0
        if args.run:
            if not args.go:
                raise RainIntensityError("native run refused: provide --go only after frozen pre-run review")
            if Path(args.protocol).resolve() != DEFAULT_PROTOCOL.resolve():
                raise RainIntensityError("native run requires the parent protocol at its frozen root path")
            summary = run_phase(protocol, protocol_hash, rates, args.out, helper=helper)
            print(json.dumps({"status": summary["status"], "stop_reason": summary["stop_reason"],
                              "cases": summary["cases"]}, indent=2))
            return 0 if summary["status"] == "complete" else 1
    except (RainIntensityError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"native-rain-intensity: {type(error).__name__}: {error}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
