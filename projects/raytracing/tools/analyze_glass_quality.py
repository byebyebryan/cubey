# /// script
# dependencies = ["numpy==2.4.6", "pillow==12.0.0"]
# ///
"""Analyze raw Q0 captures and phase-separated native telemetry without tuning images."""

import argparse
import csv
import json
from pathlib import Path

import numpy as np
from PIL import Image


def erode(mask, iterations=2):
    result = mask.copy()
    for _ in range(iterations):
        p = np.pad(result, 1, constant_values=False)
        result = p[1:-1, 1:-1] & p[:-2, 1:-1] & p[2:, 1:-1] & p[1:-1, :-2] & p[1:-1, 2:]
    return result


def dilate(mask, iterations=2):
    result = mask.copy()
    for _ in range(iterations):
        p = np.pad(result, 1, constant_values=False)
        result = p[1:-1, 1:-1] | p[:-2, 1:-1] | p[2:, 1:-1] | p[1:-1, :-2] | p[1:-1, 2:]
    return result


def summary(values):
    values = np.asarray(values, dtype=float)
    assert values.size and np.isfinite(values).all()
    return {
        "count": int(values.size),
        "p50": float(np.median(values)),
        "p95": float(np.percentile(values, 95)),
        "maximum": float(values.max()),
    }


def errors(candidate, a, b, mask):
    assert candidate.shape == a.shape == b.shape and mask.shape == a.shape[:2]
    assert (
        mask.any()
        and np.isfinite(candidate).all()
        and np.isfinite(a).all()
        and np.isfinite(b).all()
    )
    reference = (a[mask].astype(float) + b[mask].astype(float)) * 0.5
    difference = candidate[mask].astype(float) - reference
    mse = float(np.mean(difference**2))
    pair_mse = float(np.mean((a[mask].astype(float) - b[mask].astype(float)) ** 2))
    # Two independent equal-SPP seeds estimate noise in their mean. This is a
    # moment estimate, not a confidence bound or a proof for heavy-tailed paths.
    floor = pair_mse * 0.25
    return {
        "pixels": int(mask.sum()),
        "rmse": mse**0.5,
        "reference_pair_rmse": pair_mse**0.5,
        "reference_mean_noise_estimate": floor**0.5,
        "candidate_noise_estimate": max(0, mse - floor) ** 0.5,
        "reference_rms": float(np.mean(reference**2) ** 0.5),
        "absolute_channel_error_p95": float(np.percentile(np.abs(difference), 95)),
        "absolute_channel_error_p99": float(np.percentile(np.abs(difference), 99)),
        "absolute_channel_error_p999": float(np.percentile(np.abs(difference), 99.9)),
        "maximum_absolute_channel_error": float(np.abs(difference).max()),
    }


def masks_from_labels(path, width, height):
    labels = np.fromfile(path, dtype="<f4").reshape(540, 960, 4, 4)[:, :, 0, 1]
    # Nearest-neighbor upsampling is a labeled primary proxy, not coverage.
    labels = np.array(
        Image.fromarray(labels).resize((width, height), Image.Resampling.NEAREST)
    )
    dragon = labels == 0
    slab = labels == 1
    y, x = np.nonzero(dragon)
    assert len(x) > 0
    low, high = int(y.min()), int(y.max()) + 1
    left, right = int(x.min()), int(x.max()) + 1
    yy, xx = np.indices(labels.shape)
    body = erode(dragon, 2)
    head = (yy < low + 0.35 * (high - low)) & (xx < left + 0.7 * (right - left))
    lower = yy > low + 0.68 * (high - low)
    return {
        "foreground": dragon | slab,
        "dragon_interior": body,
        "head_thin_features": dragon & head,
        "lower_body_slab_crop": body & lower,
        "primary_slab_interior": erode(slab, 2),
        "dragon_silhouette_band": dilate(dragon, 2) & ~body,
    }


def read_hdr(path, width=960, height=540):
    return np.fromfile(path, dtype="<f4").reshape(height, width, 4)[:, :, :3]


def read_display(path):
    with Image.open(path) as image:
        return np.asarray(image.convert("RGB"), dtype=float)


def analyze_captures(directory):
    suites = {}
    for suite in ["pilot", "fullhd", "ablations", "deep", "extended"]:
        root = directory / suite
        if not (root / "suite.json").exists():
            continue
        receipt = json.loads((root / "suite.json").read_text())
        results = {}
        for view in receipt["views"]:
            if suite in ["pilot", "fullhd"]:
                sample = next(c for c in receipt["calls"] if c["mode"] == "hdr")
                w, h = sample["width"], sample["height"]
                masks = masks_from_labels(root / (view + "-primary-labels.bin"), w, h)
                a = read_hdr(root / f"{view}-4096-s19.bin", w, h)
                b = read_hdr(root / f"{view}-4096-s43.bin", w, h)
                pa = read_display(root / f"{view}-4096-s19.png")
                pb = read_display(root / f"{view}-4096-s43.png")
                comparisons = {}
                for s in [32, 128, 512, 2048]:
                    hdr = read_hdr(root / f"{view}-{s}-s7.bin", w, h)
                    display = read_display(root / f"{view}-{s}-s7.png")
                    comparisons[str(s)] = {
                        name: {
                            "hdr": errors(hdr, a, b, mask),
                            "display_rgb8": errors(display, pa, pb, mask),
                        }
                        for name, mask in masks.items()
                        if mask.any()
                    }
                results[view] = {
                    "width": w,
                    "height": h,
                    "comparisons": comparisons,
                    "reference_seeds": [19, 43],
                    "reference_samples_per_seed": 4096,
                    "region_pixels": {
                        name: int(mask.sum()) for name, mask in masks.items()
                    },
                }
                for name, mask in masks.items():
                    Image.fromarray((mask * 255).astype("uint8")).save(
                        root / f"{view}-mask-{name}.png"
                    )
            elif suite == "extended":
                masks = masks_from_labels(
                    directory / "pilot" / f"{view}-primary-labels.bin", 960, 540
                )
                a = read_hdr(root / f"{view}-8192-s19.bin")
                b = read_hdr(root / f"{view}-8192-s43.bin")
                for s in [32, 128, 512, 2048]:
                    candidate = read_hdr(directory / "pilot" / f"{view}-{s}-s7.bin")
                    results[view + "-" + str(s)] = {
                        name: errors(candidate, a, b, mask)
                        for name, mask in masks.items()
                        if mask.any()
                    }
            elif suite == "deep":
                difference = read_hdr(root / f"{view}-budget-default.bin").astype(
                    float
                ) - read_hdr(root / f"{view}-budget-deep.bin").astype(float)
                results[view] = {
                    "whole_image_hdr_rmse": float(np.mean(difference**2) ** 0.5),
                    "maximum_absolute_channel_error": float(np.abs(difference).max()),
                }
                assert results[view]["whole_image_hdr_rmse"] <= 1e-5, results[view]
            else:
                masks = masks_from_labels(
                    directory / "pilot" / f"{view}-primary-labels.bin", 960, 540
                )
                for lighting in ["combined", "sky", "sun"]:
                    a = read_hdr(root / f"{view}-{lighting}-2048-s19.bin")
                    b = read_hdr(root / f"{view}-{lighting}-2048-s43.bin")
                    candidate = read_hdr(root / f"{view}-{lighting}-128-s7.bin")
                    results[view + "-" + lighting] = {
                        name: errors(candidate, a, b, mask)
                        for name, mask in masks.items()
                        if mask.any()
                    }
        suites[suite] = results
    return suites


def analyze_native(root):
    metrics = json.loads((root / "metrics.json").read_text())
    events = json.loads((root / "events.json").read_text())
    native = metrics["native_frames"]
    journal = {f["frame"]: f for f in metrics["sampling_frames"]}
    assert (
        len(journal) == len(native)
        and metrics["dropped_gpu_samples"] == 0
        and metrics["dropped_native_frames"] == 0
    )
    assert all(
        f["samples_after"] == journal[f["frame"]]["samples_after"]
        and f["epoch"] == journal[f["frame"]]["epoch"]
        for f in native
    )
    by_host = {f["host_frame"]: f for f in native}
    with (root / "host.passes.csv").open() as stream:
        spans = list(csv.DictReader(stream))
    # The host's frame clock and host profile frame indices should be identical
    # here (no swapchain recreation); reject mismatches rather than guess.
    recorded_ids = {
        int(s["frame_index"]) for s in spans if s["label"] == "host.record_frame"
    }
    assert recorded_ids == set(by_host), "host/app frame identity mismatch"
    gpu = {f["frame"]: {} for f in native}
    for s in metrics["gpu_samples"]:
        gpu[s["frame"]][s["pass"]] = s["milliseconds"]
    for f in native:
        assert "raytracing.trace" in gpu[f["frame"]], f["frame"]
        assert (
            f["update_steady_ns"]
            <= f["record_begin_steady_ns"]
            <= f["record_end_steady_ns"]
        )
    phase_results = {}
    for phase in events["phases"]:
        if phase["name"] == "warmup":
            continue
        frames = [
            f
            for f in native
            if phase["begin_ns"] <= f["update_steady_ns"] < phase["end_ns"]
        ]
        groups = {}
        for name, select in [
            ("reset", lambda j: j["new_samples"] > 0 and j["samples_before"] == 0),
            (
                "accumulating",
                lambda j: j["new_samples"] > 0 and j["samples_before"] > 0,
            ),
            ("settled", lambda j: j["new_samples"] == 0),
        ]:
            selected = [f for f in frames if select(journal[f["frame"]])]
            if not selected:
                continue
            ids = {f["host_frame"] for f in selected}
            cpu = {
                label: summary(
                    [
                        float(s["duration_ms"])
                        for s in spans
                        if int(s["frame_index"]) in ids
                        and s["kind"] == "cpu"
                        and s["label"] == label
                    ]
                )
                for label in [
                    "host.frame_service",
                    "host.acquire_image",
                    "host.queue_submit",
                    "host.present",
                ]
                if any(
                    int(s["frame_index"]) in ids
                    and s["kind"] == "cpu"
                    and s["label"] == label
                    for s in spans
                )
            }
            groups[name] = {
                "frames": len(selected),
                "host_delta_ms": summary([f["host_delta_ms"] for f in selected]),
                "cpu_record_ms": summary([f["cpu_record_ms"] for f in selected]),
                "cpu_spans_ms": cpu,
                "gpu_trace_ms": summary(
                    [gpu[f["frame"]]["raytracing.trace"] for f in selected]
                ),
                "sample_counts": summary([f["samples_after"] for f in selected]),
            }
        settled = {}
        if phase["name"].startswith("settle"):
            first = next(f for f in native if f["epoch"] == frames[-1]["epoch"])
            for target in [32, 128, 512]:
                reached = next(
                    (f for f in frames if f["samples_after"] >= target), None
                )
                settled[str(target)] = (
                    None
                    if reached is None
                    else (reached["record_end_steady_ns"] - first["update_steady_ns"])
                    / 1e6
                )
        moving = [
            f
            for f in frames
            if f.get("input", {}).get("left_down")
            and (f["input"]["drag_dx"] != 0 or f["input"]["drag_dy"] != 0)
        ]
        motion = (
            {
                "frames": len(moving),
                "samples_after": summary([f["samples_after"] for f in moving]),
            }
            if moving
            else None
        )
        if moving:
            assert all(
                journal[f["frame"]]["new_samples"] == 1 and f["samples_after"] == 1
                for f in moving
            )
        phase_results[phase["name"]] = {
            "groups": groups,
            "settle_recorded_ms": settled,
            "motion_receipts": motion,
            "first_camera": {
                key: frames[0][key] for key in ["yaw", "pitch", "distance"]
            },
            "last_camera": {
                key: frames[-1][key] for key in ["yaw", "pitch", "distance"]
            },
        }
    return {
        "recording": events["recording"],
        "validation": events["validation"],
        "width": events["width"],
        "height": events["height"],
        "phases": phase_results,
        "camera_rejections": metrics["optical_camera_rejected_updates"],
        "history_epochs": metrics["history_epochs"],
        "as_builds": metrics["as_builds"],
        "resident_requested_bytes": metrics["resident_project_requested_bytes"],
        "peak_requested_bytes": metrics["peak_simultaneous_project_requested_bytes"],
        "scope": "Native Xvfb. Settle times end at CPU command recording; GPU execution/presentation follow. host.present is vkQueuePresentKHR CPU call duration, not display latency.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument(
        "--native",
        nargs="+",
        help="Explicit final native runs; omit to analyze every passed run",
    )
    args = parser.parse_args()
    rejected = [
        json.loads(p.read_text())
        for p in (args.input / "fullhd").glob("*.receipt.json")
    ]
    failed = (args.input / "fullhd-failure.json").exists() or any(
        r.get("transport_failure") for r in rejected
    )
    result = {
        "captures": analyze_captures(args.input),
        "native": {},
        "fullhd_quality_gate": {
            "state": "failed" if failed else "not measured",
            "reference_suite_completed": (args.input / "fullhd/suite.json").exists(),
            "reason": "invalid-medium event; partial captures are not qualified high-SPP references"
            if failed
            else None,
        },
    }
    if not failed and result["fullhd_quality_gate"]["reference_suite_completed"]:
        result["fullhd_quality_gate"]["state"] = "clean transport status"
    roots = (
        [args.input / name for name in args.native]
        if args.native
        else sorted(args.input.glob("native-*"))
    )
    for root in roots:
        if (root / "receipt.json").exists():
            result["native"][root.name] = analyze_native(root)
    (args.input / "analysis.json").write_text(json.dumps(result, indent=2) + "\n")
    print("Q0 analysis written:", args.input / "analysis.json")


if __name__ == "__main__":
    main()
