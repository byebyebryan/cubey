#!/usr/bin/env python3
"""Derive bounded study statistics without editing any retained trial evidence."""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import statistics

GPU_PASS = "gpu:fluid_25d native presentation total"
FIELD_AGE = "fluid_25d.external.publication_freshness_s"
FOAM_AGE = "fluid_25d.external.whitewater_source_age_s"


def spread(values):
    values = [v for v in values if v is not None]
    return {"n": len(values), "median": statistics.median(values),
            "min": min(values), "max": max(values)} if values else {"n": 0}


def measures(row):
    profile = row.get("profile") or {}
    metrics = profile.get("metrics", {})
    return {"pace_x": row.get("physical_wall_ratio"),
            "frame_p95_ms": profile.get("frame_ms", {}).get("p95"),
            "gpu_p95_ms": profile.get("passes_ms", {}).get(GPU_PASS, {}).get("p95"),
            "field_age_p95_s": metrics.get(FIELD_AGE, {}).get("p95"),
            "whitewater_age_p95_s": metrics.get(FOAM_AGE, {}).get("p95"),
            "classification_p95_ms": metrics.get("fluid_25d.whitewater.classification_cpu_ms", {}).get("p95"),
            "warm_ack_p95_ms": (row.get("warm_ack_ms") or {}).get("p95")}


def resource_trend(row):
    samples = row.get("samples", [])
    duration = row.get("measurement_wall_s", 0)
    # Skip the first minute of a soak; growth during dry startup is not a leak.
    early = [s for s in samples if 60 <= s["wall_s"] <= duration * .3]
    late = [s for s in samples if s["wall_s"] >= duration * .8]
    result = {"scope": "sampled owned-process RSS; early window excludes first minute",
              "session_bytes_after_shutdown": row.get("session_bytes")}
    for key in ("worker_rss_mib", "viewer_rss_mib", "publication_age_s", "next_dt_s"):
        first = spread([s.get(key) for s in early])
        last = spread([s.get(key) for s in late])
        result[key] = {"early": first, "late": last,
                       "median_change": last["median"] - first["median"] if first["n"] and last["n"] else None,
                       "peak": max((s[key] for s in samples if key in s), default=None)}
    return result


def display_age_trend(directory, row):
    path = directory / "viewer-profile.metrics.csv"
    if not path.exists():
        return {"evidence": "missing"}
    start = row.get("measurement_start_unix_s", 0)
    duration = row.get("measurement_wall_s", 0)
    by_frame = {}
    with path.open() as stream:
        for item in csv.DictReader(stream):
            name = item["category"] + "." + item["name"]
            if name in (FIELD_AGE, FOAM_AGE, "fluid_25d.study.command_recorded_unix_s"):
                by_frame.setdefault(item["frame_index"], {})[name] = float(item["value"])
    result = {}
    for key in (FIELD_AGE, FOAM_AGE):
        first, last = [], []
        for values in by_frame.values():
            elapsed = values.get("fluid_25d.study.command_recorded_unix_s", 0) - start
            if key not in values:
                continue
            if 60 <= elapsed <= duration * .3:
                first.append(values[key])
            if duration * .8 <= elapsed <= duration:
                last.append(values[key])
        result[key] = {"early": spread(first), "late": spread(last)}
    return result


def analyze(root):
    rows, trials, groups, soaks = [], [], {}, {}
    for path in sorted(root.glob("*/**/result.json")):
        if path.parent.parent.name == "smoke":
            continue
        row = json.loads(path.read_bytes())
        if "case" not in row or "mode" not in row:
            continue
        relative = str(path.parent.relative_to(root))
        rows.append(row)
        phase = relative.split("/")[0]
        values = measures(row)
        groups.setdefault((phase, row["case"], row["mode"]), []).append(values)
        spec = row.get("identity", {}).get("spec", {})
        width, height = spec.get("cols"), spec.get("rows")
        subdivision = row.get("surface_subdivision", 2)
        geometry = 2 * (width - 1) * (height - 1) * subdivision ** 2 if width and height else None
        telemetry = (row.get("worker_telemetry") or {}).get("stats", {})
        trials.append({"directory": relative, "case": row["case"], "mode": row["mode"],
                       "status": row["status"], "errors": row.get("errors", []), **values,
                       "triangles_per_display_layer_derived": geometry,
                       "triangle_scope": "formula from grid and subdivision; host counter is unsupported, not zero actual geometry",
                       "worker_statistics_including_startup_and_post_window": {
                           key: item | {"mean": item["sum"] / item["count"] if item["count"] else None}
                           for key, item in telemetry.items()}})
        if phase.startswith("soak-"):
            soaks[relative] = {"resources": resource_trend(row),
                               "display_age": display_age_trend(path.parent, row)}
    phases = [{"phase": phase, "case": case, "mode": mode,
               "measures": {key: spread([item[key] for item in values]) for key in values[0]}}
              for (phase, case, mode), values in groups.items()]
    result = {"schema": "cubey.fluid25d.live-performance-analysis.v1",
              "scope": "ranges of per-run summaries, not pooled frames or confidence intervals; sampled private X11, not scanout",
              "trials": trials, "phases": phases, "soaks": soaks}
    (root / "analysis.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--study", required=True, type=Path)
    args = p.parse_args()
    result = analyze(args.study.resolve(strict=True))
    print(json.dumps({"trials": len(result["trials"]), "phases": len(result["phases"]), "soaks": len(result["soaks"])}))


if __name__ == "__main__":
    main()
