#!/usr/bin/env python3
"""Run the isolated CPU transport proof and preserve a no-overwrite receipt.

This harness launches only the study benchmark executable. It never starts the
application, GPU oracle, mountain scenario, or GUI. A negative numerical
verdict still writes the full report and raw process streams before returning
the benchmark's status code.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_BINARY = ROOT / "build/dev/projects/fluid/fluid_25d/cubey_project_fluid_25d_transport_study_benchmark"
SOURCE_PATHS = (
    "projects/fluid/fluid_25d/CMakeLists.txt",
    "projects/fluid/sim/fluid_25d/fluid_25d_transport_study_benchmark.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_transport_study.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_transport_study.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_finite_volume_oracle.cpp",
    "projects/fluid/sim/fluid_25d/fluid_25d_finite_volume_oracle.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_config.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_scenarios.h",
    "projects/fluid/sim/fluid_25d/fluid_25d_solver_state.h",
    "projects/fluid/fluid_25d/run_transport_study_v3.py",
)
MODE_ARGUMENTS = {
    "full": (),
    "probe": ("--probe",),
    "fallback-probe": ("--fallback-probe",),
    "self-test": ("--self-test",),
}
REPORT_CONTRACT = {
    "full": ("full_cpu_report", "candidate_acceptance_pass"),
    "probe": ("probe", "candidate_acceptance_pass"),
    "fallback-probe": ("bounded_fallback_probe", "fallback_probe_acceptance_pass"),
    "self-test": ("self_test", "candidate_acceptance_pass"),
}
REPORT_SCHEMA = "cubey.fluid25d.transport_study.v3"


def sha256_bytes(contents: bytes) -> str:
    return hashlib.sha256(contents).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_bytes_new(path: Path, contents: bytes) -> None:
    with path.open("xb") as destination:
        destination.write(contents)


def write_json_new(path: Path, value: dict[str, Any]) -> bytes:
    contents = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")
    write_bytes_new(path, contents)
    return contents


def input_hashes(root: Path) -> dict[str, str]:
    hashes: dict[str, str] = {}
    for relative in SOURCE_PATHS:
        path = root / relative
        if not path.is_file():
            raise FileNotFoundError(f"required study provenance input is missing: {relative}")
        hashes[relative] = sha256_file(path)
    return hashes


def command_for(binary: Path, mode: str) -> list[str]:
    if mode not in MODE_ARGUMENTS:
        raise ValueError(f"unsupported benchmark mode: {mode}")
    return [str(binary), *MODE_ARGUMENTS[mode]]


def validate_report(report: dict[str, Any], mode: str, exit_code: int) -> list[str]:
    """Check report identity, booleans, and the benchmark's exit contract."""
    if mode not in REPORT_CONTRACT:
        return [f"unsupported benchmark mode: {mode}"]
    expected_mode, acceptance_key = REPORT_CONTRACT[mode]
    errors: list[str] = []
    if report.get("schema") != REPORT_SCHEMA:
        errors.append(f"report schema must be {REPORT_SCHEMA!r}")
    if report.get("mode") != expected_mode:
        errors.append(f"report mode must be {expected_mode!r}")
    diagnostic = report.get("diagnostic_success")
    acceptance = report.get(acceptance_key)
    if not isinstance(diagnostic, bool):
        errors.append("diagnostic_success must be a boolean")
    if not isinstance(acceptance, bool):
        errors.append(f"{acceptance_key} must be a boolean")
    if mode == "full" and not isinstance(report.get("conservation_health_pass"), bool):
        errors.append("full report conservation_health_pass must be a boolean")

    if isinstance(diagnostic, bool) and isinstance(acceptance, bool):
        if not diagnostic and acceptance:
            errors.append("acceptance cannot pass when diagnostics fail")
        expected_exit = 1 if not diagnostic else (0 if acceptance else 2)
        if exit_code != expected_exit:
            errors.append(
                f"benchmark exit code {exit_code} conflicts with report verdict; "
                f"expected {expected_exit}"
            )
    return errors


def execute_receipt(
    *,
    binary: Path,
    output: Path,
    mode: str,
    baseline: Path,
    root: Path = ROOT,
) -> tuple[int, dict[str, Any]]:
    binary = binary.expanduser().resolve()
    output = output.expanduser().resolve()
    baseline = baseline.expanduser().resolve()
    if not binary.is_file() or not binary.stat().st_mode & 0o111:
        raise FileNotFoundError(f"study benchmark is not an executable file: {binary}")
    if not baseline.is_file():
        raise FileNotFoundError(f"retained V2 baseline report is missing: {baseline}")
    sources_before = input_hashes(root)
    baseline_bytes = baseline.read_bytes()
    binary_hash_before = sha256_file(binary)
    command = command_for(binary, mode)

    # Exclusive creation refuses to reuse an evidence folder, including an
    # empty one. Parent folders may be shared, but this run's leaf is unique.
    output.mkdir(parents=True, exist_ok=False)
    write_bytes_new(output / "baseline-transport.json", baseline_bytes)
    result = subprocess.run(command, cwd=root, capture_output=True, check=False)
    write_bytes_new(output / "stdout.raw", result.stdout)
    write_bytes_new(output / "stderr.raw", result.stderr)

    report: dict[str, Any] | None = None
    parse_error: str | None = None
    try:
        parsed = json.loads(result.stdout.decode("utf-8"))
        if not isinstance(parsed, dict):
            raise ValueError("benchmark stdout JSON root is not an object")
        report = parsed
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
        parse_error = str(error)

    report_hash: str | None = None
    if report is not None:
        report_bytes = write_json_new(output / "report.json", report)
        report_hash = sha256_bytes(report_bytes)
    else:
        write_json_new(output / "report-parse-error.json", {"error": parse_error})

    sources_after = input_hashes(root)
    unchanged_inputs = sources_before == sources_after
    if baseline.read_bytes() != baseline_bytes:
        unchanged_inputs = False
    binary_hash_after = sha256_file(binary)
    binary_unchanged = binary_hash_before == binary_hash_after
    report_validation_errors = (
        validate_report(report, mode, result.returncode) if report is not None else []
    )
    receipt = {
        "schema": "cubey.fluid25d.transport_study_receipt.v1",
        "created_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "mode": mode,
        "command": command,
        "working_directory": str(root.resolve()),
        "study_only": True,
        "production_app_or_gpu_invoked": False,
        "legacy_cpu_oracle_comparison_included": mode in ("full", "probe"),
        "gui_or_mountain_invoked": False,
        "benchmark_exit_code": result.returncode,
        "report_json_valid": report is not None,
        "report_parse_error": parse_error,
        "report_contract_valid": report is not None and not report_validation_errors,
        "report_validation_errors": report_validation_errors,
        "diagnostic_success": report.get("diagnostic_success") if report else None,
        "candidate_acceptance_pass": (
            report.get("candidate_acceptance_pass", report.get("fallback_probe_acceptance_pass"))
            if report
            else None
        ),
        "source_hashes_before": sources_before,
        "source_hashes_after": sources_after,
        "source_inputs_unchanged": unchanged_inputs,
        "binary_sha256_before": binary_hash_before,
        "binary_sha256_after": binary_hash_after,
        "binary_unchanged": binary_unchanged,
        "baseline_snapshot": {
            "source_path": str(baseline),
            "evidence_path": "baseline-transport.json",
            "sha256": sha256_bytes(baseline_bytes),
        },
        "stdout_raw_sha256": sha256_bytes(result.stdout),
        "stderr_raw_sha256": sha256_bytes(result.stderr),
        "report_sha256": report_hash,
        "fixture_reference_provenance": (
            "report.json contains the equations, quadrature, float-rounded scenario inputs, "
            "time samples, gates, and measured SSPRK-weighted face transfers for each fixture"
            if report
            else None
        ),
    }
    write_json_new(output / "execution-receipt.json", receipt)
    if not unchanged_inputs or not binary_unchanged:
        return 1, receipt
    if report is None:
        return 1, receipt
    if report_validation_errors:
        return 1, receipt
    return result.returncode, receipt


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY,
                        help="focused CPU study benchmark executable")
    parser.add_argument("--output", required=True, type=Path,
                        help="new evidence directory; existing paths are refused")
    parser.add_argument("--mode", choices=tuple(MODE_ARGUMENTS), default="full")
    parser.add_argument("--baseline", required=True, type=Path,
                        help="retained V2 transport report copied into the new receipt")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        code, receipt = execute_receipt(binary=args.binary, output=args.output,
                                        mode=args.mode, baseline=args.baseline)
    except (FileExistsError, FileNotFoundError, OSError, ValueError) as error:
        print(f"transport study receipt failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps({"output": str(args.output.expanduser().resolve()),
                      "benchmark_exit_code": receipt.get("benchmark_exit_code"),
                      "report_json_valid": receipt.get("report_json_valid"),
                      "candidate_acceptance_pass": receipt.get("candidate_acceptance_pass")},
                     sort_keys=True))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
