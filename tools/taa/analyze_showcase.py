#!/usr/bin/env python3
"""Measure matched observatory captures without confusing real motion with flicker."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess


def gray(path, crop):
    x, y, width, height = crop
    return subprocess.check_output([
        "ffmpeg", "-v", "error", "-i", str(path), "-vf",
        f"crop={width}:{height}:{x}:{y},format=gray", "-f", "rawvideo", "-"])


def rms(values):
    values = list(values)
    return math.sqrt(sum(value * value for value in values) / len(values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--captures", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.captures
    receipts = {mode: json.loads((root / f"{mode}.json").read_text())
                for mode in ("ss1", "taa", "ss2")}
    for mode, receipt in receipts.items():
        if len(receipt["actions"]) != 128:
            raise SystemExit(f"{mode} has an incomplete action replay")
        for a, b in zip(receipts["ss1"]["actions"], receipt["actions"]):
            if any(a[key] != b[key] for key in ("camera", "geometry_hash", "appearance_hash")):
                raise SystemExit("comparison camera or scene identity differs")
    crops = {"dome": (760, 85, 355, 350), "rails_steps": (925, 425, 290, 340)}
    quality = {}
    for name, crop in crops.items():
        reference = gray(root / "ss2.frame-64.png", crop)
        settled = {}
        for mode in ("ss1", "taa"):
            data = gray(root / f"{mode}.frame-64.png", crop)
            error = rms(a - b for a, b in zip(data, reference))
            settled[mode] = {"display_luma_rmse_vs_ss2": error,
                             "psnr_db": 20 * math.log10(255 / error) if error else None}
        previous = {}
        changes = {mode: [] for mode in ("ss1", "taa")}
        for frame in range(65, 81):
            reference = gray(root / f"ss2.frame-{frame}.png", crop)
            for mode in ("ss1", "taa"):
                data = gray(root / f"{mode}.frame-{frame}.png", crop)
                residual = [int(a) - int(b) for a, b in zip(data, reference)]
                if mode in previous:
                    changes[mode].append(rms(a - b for a, b in zip(residual, previous[mode])))
                previous[mode] = residual
        quality[name] = {"crop_xywh": crop, "stationary_age64": settled,
                         "orbit_residual_change_rms": {
                             mode: statistics.mean(values) for mode, values in changes.items()}}
    costs = {}
    for mode in ("ss1", "taa", "ss2"):
        path = root / f"cost-{mode}.passes.csv"
        if not path.exists():
            continue
        spans = {}
        by_frame = {}
        for row in csv.DictReader(path.open()):
            if row["kind"] != "gpu" or int(row["frame_index"]) < 24:
                continue
            label, value = row["label"], float(row["duration_ms"])
            spans.setdefault(label, []).append(value)
            if label in ("taa.motion", "taa.resolve"):
                by_frame.setdefault(row["frame_index"], 0.0)
                by_frame[row["frame_index"]] += value
        if by_frame:
            spans["taa.motion_plus_resolve"] = list(by_frame.values())
        costs[mode] = {label: {"samples": len(values), "p50_ms": statistics.median(values),
                               "p95_ms": sorted(values)[math.ceil(.95 * len(values)) - 1]}
                       for label, values in spans.items()}
    result = {"matched_frames": 128, "quality": quality, "cost": costs,
        "quality_scope": "8-bit displayed luma. SS2 is a spatial reference, not ground truth. "
                         "Orbit metric compares changes in error against each matching SS2 pose; "
                         "it is not raw interframe difference. Human blur/ghosting review remains required.",
        "cost_scope": "serial 1080p render-only, validation off, no encoding/readback, first 24 frames excluded",
        "identities": {mode: hashlib.sha256((root / f"{mode}.json").read_bytes()).hexdigest()
                       for mode in receipts}}
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"quality": quality, "cost": costs}, indent=2))


if __name__ == "__main__":
    main()
