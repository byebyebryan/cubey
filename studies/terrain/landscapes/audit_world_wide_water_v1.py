#!/usr/bin/env python3
"""Reuse native field/hash/rain-supply sanity checks; not a flux ledger."""
import argparse
import json
from pathlib import Path
import sys

import study_world_wide_water_v1 as study


def audit(out):
    import numpy as np
    from run_live_perf_terrain_v1 import field_stats
    rows = []
    study.verify(out)
    for path in sorted(out.glob("*/runs/*/result.json")):
        result = json.loads(path.read_bytes())
        if result["status"] != "completed" or result["errors"]:
            raise ValueError("incomplete/failed bake is not accepted")
        if (not result["completion"]["inputs_unchanged"] or
                not result["completion"]["reference_inputs_unchanged"] or
                result["cleanup"]["active_group_remaining"] != []):
            raise ValueError("native source/cleanup receipt failed")
        final_stats = None
        for milestone in result["milestones"]:
            recording = Path(milestone["recording"])
            doc = json.loads(recording.read_bytes())
            grid = doc["grid"]
            count = grid["width"] * grid["height"]
            for field in (doc["bed"], doc["source_bed"]):
                if study.bake.digest(recording.parent / field["path"]) != field["sha256"]:
                    raise ValueError("recording bed changed")
            for frame in doc["frames"]:
                payload = recording.parent / frame["path"]
                if study.bake.digest(payload) != frame["sha256"] or payload.stat().st_size != count*12:
                    raise ValueError("recording field hash/count mismatch")
                raw = payload.read_bytes()
                stats = field_stats(raw, grid["cell_size_m"])
                supplied = count * grid["cell_size_m"]**2 * doc["protocol"]["rain_rate_m_per_s"] * frame["time_s"]
                if stats["water_volume_m3"] > supplied*1.02+1:
                    raise ValueError("water volume exceeds supplied rain sanity bound")
                h, qx, qz = np.frombuffer(raw, dtype="<f4").reshape(3, -1)
                wet = h > .05
                speed = np.hypot(qx[wet].astype(float), qz[wet].astype(float))/h[wet]
                stats.update(wet_area_km2=float(np.count_nonzero(wet)*grid["cell_size_m"]**2/1e6),
                             wet_speed_p95_m_per_s=float(np.percentile(speed,95)) if len(speed) else 0,
                             supplied_rain_m3=supplied)
                final_stats = stats
        sid = path.parents[2].name
        candidate = path.parent.name
        export_log = out / sid / f"export-{candidate}.log"
        if not export_log.exists() or "PASS bit-exact bed/depth and checked decoded velocity" not in export_log.read_text():
            raise ValueError("neutral export parity not demonstrated")
        rows.append({"site":sid, "candidate":candidate, "status":"pass", "milestones":len(result["milestones"]),
            "physical_s":result["milestones"][-1]["physical_s"], "observed_wall_s":result["total_wall_s"],
            "stats":final_stats, "result_sha256":study.bake.digest(path)})
    if not rows:
        raise ValueError("no wider bakes to audit")
    study.bake.write(out / "field-audit.json", {"cases":rows,
        "scope":"Finite/nonnegative fields, dry momentum, hashes, rain-supply sanity and exact neutral export. Not flux conservation or calibrated hydrology."})
    study.verify(out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    audit(study.owned_root(parser.parse_args().out))
