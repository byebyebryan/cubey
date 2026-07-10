#!/usr/bin/env python3

"""Summarize automated gates for the landscape-evolution review pack."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--review-dir", type=Path, required=True)
    parser.add_argument("--oracle-summary", type=Path)
    parser.add_argument("--seeds", nargs="+", type=int, default=[0, 9012, 12345])
    return parser.parse_args()


def load_json(path: Path) -> dict:
    if not path.is_file():
        raise RuntimeError(f"missing review input: {path}")
    return json.loads(path.read_text())


def gate(name: str, measured: float, limit: float, comparison: str) -> dict:
    if comparison == "eq":
        passed = measured == limit
    elif comparison == "le":
        passed = measured <= limit
    elif comparison == "lt":
        passed = measured < limit
    else:
        raise RuntimeError(f"unknown gate comparison: {comparison}")
    return {
        "name": name,
        "measured": measured,
        "comparison": comparison,
        "limit": limit,
        "pass": passed,
    }


def main() -> int:
    args = parse_args()
    checks: list[dict] = []
    patches: list[dict] = []
    candidate_by_seed: dict[int, dict] = {}
    for seed in args.seeds:
        candidate_path = (
            args.review_dir / "fields" / "candidate" / f"seed-{seed}" / "manifest.json"
        )
        control_path = (
            args.review_dir / "fields" / "control" / f"seed-{seed}" / "manifest.json"
        )
        candidate = load_json(candidate_path)
        control = load_json(control_path)
        candidate_by_seed[seed] = candidate
        metrics = candidate["review_metrics"]
        control_metrics = control["review_metrics"]
        checks.extend(
            [
                gate(
                    f"seed-{seed}.unresolved_process_sinks",
                    float(metrics["process_unresolved_sink_count"]),
                    0.0,
                    "eq",
                ),
                gate(
                    f"seed-{seed}.severe_basin_discontinuity_coverage",
                    float(metrics["process_basin_discontinuity_coverage_gt_100m"]),
                    0.01,
                    "lt",
                ),
                gate(
                    f"seed-{seed}.final_gradient_anisotropy",
                    float(metrics["final_gradient_anisotropy"]),
                    1.5,
                    "le",
                ),
                gate(
                    f"seed-{seed}.deep_fill_non_regression",
                    float(metrics["routing_fill_coverage_gt_50m"]),
                    float(control_metrics["routing_fill_coverage_gt_50m"]) + 1.0e-9,
                    "le",
                ),
            ]
        )
        patches.append(
            {
                "seed": seed,
                "candidate_manifest": str(candidate_path.resolve()),
                "candidate_content_hash": candidate["content_hash"],
                "control_manifest": str(control_path.resolve()),
                "control_content_hash": control["content_hash"],
            }
        )

    oracle_comparison = None
    if args.oracle_summary is not None and args.oracle_summary.is_file():
        oracle = load_json(args.oracle_summary)
        candidate = candidate_by_seed[9012]
        oracle_checks = []
        comparisons = {}
        for field in ("height_m", "slope"):
            comparisons[field] = {}
            for percentile in ("p05", "p50", "p95"):
                expected = float(oracle["fields"][field][percentile])
                actual = float(candidate["fields"][field][percentile])
                relative_error = abs(actual - expected) / max(abs(expected), 1.0e-12)
                comparisons[field][percentile] = {
                    "candidate": actual,
                    "oracle": expected,
                    "relative_error": relative_error,
                }
                oracle_checks.append(
                    gate(
                        f"oracle.{field}.{percentile}.relative_error",
                        relative_error,
                        0.25,
                        "le",
                    )
                )
        checks.extend(oracle_checks)
        oracle_comparison = {
            "summary": str(args.oracle_summary.resolve()),
            "fields": comparisons,
        }

    failures = [check["name"] for check in checks if not check["pass"]]
    result = {
        "schema": "cubey.terrain.landscape-evolution-review.v1",
        "status": "metrics-pass" if not failures else "review-required",
        "automated_gate_count": len(checks),
        "failed_gate_count": len(failures),
        "failed_gates": failures,
        "visual_review_required": [
            "coherent mountain mass and branching ridges",
            "incised connected valleys",
            "no closed contour fins or stepped shoulders",
            "no obvious square-domain watershed frame",
        ],
        "patches": patches,
        "oracle_comparison": oracle_comparison,
        "checks": checks,
    }
    output_path = args.review_dir / "review-summary.json"
    output_path.write_text(json.dumps(result, indent=2) + "\n")
    print(
        f"landscape review: {result['status']} ({len(failures)} failed gates) -> {output_path}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
