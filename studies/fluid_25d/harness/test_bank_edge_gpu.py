#!/usr/bin/env python3
"""Synthetic renderer edge locality and inactive controls; no archive or solver."""
import argparse
from pathlib import Path
import tempfile
from unittest.mock import patch

import run_native_shoreline_raster_v1 as fixtures
import run_native_presentation_v1 as ref
from scenic_flow_reference import RETAINED_FLOW_CUES
from test_scenic_water_gpu import runtime_identity


def run(out, app):
    ref.reserve_directory(out)
    runtime = runtime_identity(app)
    quiet = {**RETAINED_FLOW_CUES, "water_normal_strength": 0, "water_ripple_strength": 0,
             "water_flow_agitation": 0, "water_rain_agitation": 0}
    tunings = {}
    for variant, values in {
        "off": {"water_bank_irregularity_m": 0, "water_bank_motion_m": 0},
        "static": {"water_bank_irregularity_m": 3, "water_bank_motion_m": 0},
        "moving": {"water_bank_irregularity_m": 3, "water_bank_motion_m": 1},
        "bold-static": {"water_bank_irregularity_m": 24, "water_bank_motion_m": 0,
                        "water_bank_scale_m": 128, "water_bank_band_m": 64},
        "bold-moving": {"water_bank_irregularity_m": 24, "water_bank_motion_m": 12,
                        "water_bank_scale_m": 128, "water_bank_band_m": 64},
    }.items():
        tunings[variant] = out / (variant + ".json")
        ref.write_json_exclusive(tunings[variant], {"schema": "cubey.fluid25d.scenic-material.v2", **quiet, **values})
    rows = []
    writer = fixtures._write_asc

    def render(case, manifest, variant, view="shaded"):
        label = f"{case}-{variant}-{view}"
        image = out / (label + ".png")
        cmd = ["rtk", "proxy", str(app), "--headless", "--width", "384", "--height", "288",
               "--fluid25d-recording", str(manifest), "--fluid25d-recording-time-seconds", "1",
               "--fluid25d-recording-gpu-validation", "--fluid25d-native-presentation", "scenic",
               "--fluid25d-scenic-material", "macro", "--fluid25d-scenic-water-view", view,
               "--fluid25d-native-bank-view", "bspline-2x", "--fluid25d-render-height-scale", "1",
               "--fluid25d-scenic-tuning", str(tunings[variant]), "--output", str(image)]
        completed, receipt = ref.run_logged(cmd, out / "logs", label, timeout=90, expected_presentation="scenic")
        log = completed.stdout + completed.stderr
        ref.verify_capture_log(log, True)
        if "VUID-" in log or "Validation Error" in log or runtime != runtime_identity(app):
            raise ValueError("validation/runtime control failed")
        rows.append({"case": case, "variant": variant, "view": view, "path": image.name,
                     "sha256": ref.sha256_file(image), "receipt": receipt})
        print("Rendered " + label, flush=True)
        return ref.sha256_file(image)

    for case, peak, ux, uz in (
        ("dry", 0, 0, 0), ("thin-sheet", .06, 0, 3), ("all-wet", .4, 0, 3),
        ("stationary-channel", .4, 0, 0), ("moving-channel", .4, 0, 3),
        ("rain-film-channel", .4, 0, 3),
    ):
        bed = [0.] * (fixtures.COLS * fixtures.ROWS)
        film = .02 if case == "rain-film-channel" else 0
        h = [peak if case == "all-wet" or 8 <= i % fixtures.COLS <= 23 else film for i in range(len(bed))]

        def momentum_writer(path, values, cols, rows):
            if path.name.startswith("hUx_"):
                values = [v * ux for v in h]
            elif path.name.startswith("hUy_"):
                values = [v * uz for v in h]
            writer(path, values, cols, rows)

        with patch.object(fixtures, "fields", return_value=(bed, h)), \
                patch.object(fixtures, "_write_asc", side_effect=momentum_writer):
            manifest = fixtures.fixture(out / "fixtures" / case, "fully-wet-lake")
        pins = ref.recording_tree_fingerprint(manifest.parent)
        off, fixed, moving = (render(case, manifest, v) for v in ("off", "static", "moving"))
        active = case in ("stationary-channel", "moving-channel", "rain-film-channel")
        if (off != fixed) != active:
            raise ValueError("edge locality failed: " + case)
        if case == "stationary-channel" and fixed != moving:
            raise ValueError("stationary water must not acquire bank motion")
        if not active and off != moving:
            raise ValueError("dry/film/all-wet control changed")
        bold_fixed, bold_moving = (render(case, manifest, v) for v in ("bold-static", "bold-moving"))
        if (off != bold_fixed) != active or (not active and off != bold_moving):
            raise ValueError("pronounced edge locality failed: " + case)
        if case == "stationary-channel" and bold_fixed != bold_moving:
            raise ValueError("stationary water must not acquire pronounced bank motion")
        if case == "moving-channel":
            for view in ("coverage", "depth-bands", "direct-only"):
                if render(case, manifest, "off", view) != render(case, manifest, "moving", view):
                    raise ValueError("bank effect changed retained component: " + view)
            if off != render(case, manifest, "moving", "no-bank-edge"):
                raise ValueError("full-shading ablation did not restore the off path")
        if pins != ref.recording_tree_fingerprint(manifest.parent):
            raise ValueError("recording input changed")
    ref.write_json_exclusive(out / "manifest.json", {
        "runtime": runtime, "assets": rows, "hydraulic_dispatches": 0,
        "scope": "Synthetic saved-field renderer controls, not owner visual acceptance or calibrated hydrology"})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--app", type=Path, default=ref.APP)
    args = parser.parse_args()
    if args.out:
        run(args.out.resolve(), args.app.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="cubey-bank-edge-") as temp:
            run(Path(temp) / "controls", args.app.resolve())
