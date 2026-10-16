#!/usr/bin/env python3
"""Explicit, isolated source-to-wheel comparison; never installs a solver.

Requires the study's pinned Python/NumPy environment. Retained case inputs and
outputs are read-only. A fresh output leaf is mandatory. Numerical gates are
declared in source-correspondence-protocol.json before this command is run.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

import numpy as np

import run_native_runoff_reuse_v1 as reference


def identity(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def input_hashes(path: Path) -> dict[str, str]:
    return {str(item.relative_to(path)): identity(item) for item in sorted(path.rglob("*"))
            if item.is_file()}


def compare_field(observed: np.ndarray, expected: np.ndarray, *, mountain: bool,
                  depth: bool) -> dict:
    if observed.shape != expected.shape or not np.isfinite(observed).all() or not np.isfinite(expected).all():
        raise ValueError("nonfinite field or mismatched grid")
    delta = np.abs(observed - expected)
    maximum = float(np.max(delta))
    relative_l1 = float(delta.sum(dtype=np.float64) /
                        max(np.abs(expected).sum(dtype=np.float64), 1e-6))
    if mountain:
        absolute_bound = (0.005 if depth else 0.01) + (0.01 if depth else 0.02) * float(
            np.max(np.abs(expected)))
        passed = maximum <= absolute_bound and relative_l1 <= (0.001 if depth else 0.005)
    else:
        absolute_bound = None
        passed = bool(np.all(delta <= 2e-6 + 1e-4 * np.abs(expected)))
    return {"exact": bool(np.array_equal(observed, expected)), "maximum_absolute": maximum,
            "relative_l1": relative_l1, "absolute_bound": absolute_bound,
            "passed": bool(passed)}


def run_case(extension: Path, baseline: Path, output: Path, entry_point: str = "stock") -> dict:
    spec = json.loads((baseline / "case-spec.json").read_text())
    output.mkdir()
    shutil.copytree(baseline / "native/input", output / "native/input")
    (output / "native/output").mkdir()
    shutil.copy2(baseline / "case-spec.json", output / "case-spec.json")
    before = input_hashes(output / "native/input")
    if before != input_hashes(baseline / "native/input"):
        raise ValueError("input copy is not byte-exact")
    started = time.monotonic()
    with (output / "native.stdout").open("wb") as stdout, (output / "native.stderr").open("wb") as stderr:
        result = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--child",
                                 str(extension), str(output / "native"), entry_point, str(baseline)],
                                stdout=stdout, stderr=stderr, timeout=300)
    unchanged = before == input_hashes(output / "native/input")
    record = {"case": spec["name"], "baseline": str(baseline), "entry_point": entry_point,
              "exit_code": result.returncode,
              "inputs_unchanged": unchanged, "child_wall_seconds": time.monotonic() - started,
              "input_hashes": before, "passed": False}
    if result.returncode == 0 and unchanged:
        mountain = spec["family"] == "mountain-rain"
        paths = reference.snapshot_paths(baseline / "native/output")
        candidate = reference.snapshot_paths(output / "native/output")
        if any(set(paths[field]) != set(candidate[field]) for field in paths):
            raise ValueError("saved field timestamps differ")
        observations = []
        for timestamp in sorted(paths["h"]):
            fields = {}
            for field in ("h", "hUx", "hUy"):
                header, observed, valid = reference.read_ascii(candidate[field][timestamp])
                old_header, expected, old_valid = reference.read_ascii(paths[field][timestamp])
                if header != old_header or not valid.all() or not old_valid.all():
                    raise ValueError("ASCII geometry/valid cells differ")
                if field == "h" and np.any(observed < 0):
                    raise ValueError("negative exported depth")
                fields[field] = compare_field(observed, expected, mountain=mountain, depth=field == "h")
            observations.append({"time_s": timestamp, "fields": fields})
        record["observations"] = observations
        if entry_point == "service-noop":
            record["timestep_log_exact"] = (
                (output / "native/output/timestep_log.txt").read_bytes() ==
                (baseline / "native/output/timestep_log.txt").read_bytes())
            record["binary_snapshot_audit"] = json.loads((output / "native/binary-audit.json").read_text())
        if not mountain:
            snapshots, health = reference.read_case_snapshots(output / "native/output", spec)
            record["analytic"] = (reference.flat_metrics if spec["family"] == "flat-rain"
                                  else reference.sheet_metrics)(spec, snapshots)
            record["health"] = health
        record["passed"] = all(item["passed"] for obs in observations for item in obs["fields"].values()) and (
            mountain or record["analytic"]["gate_passed"])
        if entry_point == "service-noop":
            record["passed"] = record["passed"] and record["timestep_log_exact"] and record["binary_snapshot_audit"]["passed"]
    (output / "correspondence.json").write_text(json.dumps(record, indent=2, allow_nan=False) + "\n")
    return record


def main() -> int:
    if len(sys.argv) == 6 and sys.argv[1] == "--child":
        module_spec = importlib.util.spec_from_file_location("flood", Path(sys.argv[2]))
        if module_spec is None or module_spec.loader is None:
            raise ValueError("invalid extension")
        module = importlib.util.module_from_spec(module_spec)
        module_spec.loader.exec_module(module)
        if sys.argv[4] == "service-noop":
            baseline = Path(sys.argv[5])
            expected_paths = reference.snapshot_paths(baseline / "native/output")
            rows = []
            def hook(initial, physical_time, next_dt, output_due, snapshot):
                if initial or output_due:
                    if physical_time not in expected_paths["h"]:
                        raise ValueError("native hook timestamp does not match reference output schedule")
                    header, expected, _ = reference.read_ascii(expected_paths["h"][physical_time])
                    shape = expected.shape
                    h_raw, hu_raw, z_raw = snapshot()
                    h = np.frombuffer(h_raw, dtype="<f4").reshape(shape)[::-1]
                    hu = np.frombuffer(hu_raw, dtype="<f4").reshape(*shape, 2)[::-1]
                    fields = {"h": h, "hUx": hu[:, :, 0], "hUy": hu[:, :, 1]}
                    audit = {}
                    for field, native in fields.items():
                        _, exported, _ = reference.read_ascii(expected_paths[field][physical_time])
                        delta = np.abs(native.astype(np.float64) - exported)
                        # Native binary keeps original float32 precision. The stock
                        # writer rounds to six decimal places; add one native ULP
                        # for decimal-to-float32 representational boundaries.
                        bound = 0.500001e-6 + np.abs(np.spacing(native)).astype(np.float64)
                        audit[field] = {"maximum_absolute": float(delta.max()),
                                        "passed": bool(np.all(delta <= bound))}
                    rows.append({"time_s": physical_time, "fields": audit})
                return {"action": "continue", "rain_override_m_per_s": None}
            result = module.run_service(sys.argv[3], hook, continuous=False, keep_outputs=True)
            expected_times = sorted(expected_paths["h"])
            audit = {"schema": "cubey.fluid25d.native-binary-parity.v1", "observations": rows,
                     "passed": [row["time_s"] for row in rows] == expected_times and
                               all(item["passed"] for row in rows for item in row["fields"].values()),
                     "precision": "native float32, checked against six-decimal ASC quantization plus one ULP"}
            (Path(sys.argv[3]) / "binary-audit.json").write_text(json.dumps(audit, indent=2) + "\n")
        else:
            result = module.run(sys.argv[3])
        return int(result or 0)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--extension", type=Path, required=True)
    parser.add_argument("--baseline-case", type=Path, action="append", required=True)
    parser.add_argument("--protocol", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--entry-point", choices=("stock", "service-noop"), default="stock")
    args = parser.parse_args()
    protocol = json.loads(args.protocol.read_text())
    if protocol.get("schema") != "cubey.fluid25d.source-correspondence.v1" or not protocol.get("frozen_before_candidate_execution"):
        raise ValueError("missing frozen protocol")
    extension = args.extension.resolve(strict=True)
    baselines = [path.resolve(strict=True) for path in args.baseline_case]
    if len({path.name for path in baselines}) != len(baselines):
        raise ValueError("duplicate case name")
    output = args.out.absolute()
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("output must be a fresh leaf beneath an existing directory")
    output = output.parent.resolve() / output.name
    output.mkdir()
    records = []
    for baseline in baselines:
        record = run_case(extension, baseline, output / baseline.name, args.entry_point)
        records.append(record)
        print(json.dumps({"case": record["case"], "passed": record["passed"]}), flush=True)
        if not record["passed"]:
            break
    summary = {"schema": "cubey.fluid25d.source-correspondence-result.v1",
               "extension": str(extension), "extension_sha256": identity(extension),
               "protocol_sha256": identity(args.protocol),
               "entry_point": args.entry_point,
               "passed": len(records) == len(baselines) and all(item["passed"] for item in records),
               "requested_case_count": len(baselines), "executed_case_count": len(records),
               "cases": [{key: value for key, value in item.items() if key not in ("observations", "input_hashes", "analytic", "health")}
                         for item in records]}
    (output / "summary.json").write_text(json.dumps(summary, indent=2, allow_nan=False) + "\n")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
