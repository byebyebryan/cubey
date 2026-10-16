#!/usr/bin/env python3
"""Synthetic whitewater renderer isolation; no external recording or solver."""
import argparse
from pathlib import Path
import tempfile
from unittest.mock import patch

import run_native_shoreline_raster_v1 as fixtures
import run_native_presentation_v1 as ref
from test_scenic_water_gpu import runtime_identity
from scenic_flow_reference import RETAINED_FLOW_CUES


def run(out, app):
    ref.reserve_directory(out)
    runtime = runtime_identity(app)
    writer = fixtures._write_asc
    rows = []
    tunings = {}
    for name, values in {
        "off": {}, "flecks": {"water_whitewater_strength": 1, "water_whitewater_speed": 2},
        "network": {"water_stream_foam_strength": 1},
        "patches": {"water_stream_foam_strength": 1, "water_stream_foam_patchiness": 1},
        "combined": {"water_stream_foam_strength": 1, "water_stream_foam_patchiness": 1,
                     "water_stream_foam_brightness": .4},
        "disabled-patches": {"water_stream_foam_patchiness": 1,
                             "water_stream_foam_brightness": .4},
    }.items():
        path = out / (name + ".json")
        ref.write_json_exclusive(path, {"schema": "cubey.fluid25d.scenic-material.v2",
                                        **RETAINED_FLOW_CUES, **values})
        tunings[name] = path

    def render(case, manifest, variant, view="shaded", bank="reference"):
        label = f"{case}-{variant}-{view}-{bank}"
        image = out / (label + ".png")
        cmd = ["rtk", "proxy", str(app), "--headless", "--width", "384", "--height", "288",
               "--fluid25d-recording", str(manifest), "--fluid25d-recording-time-seconds", "1",
               "--fluid25d-recording-gpu-validation", "--fluid25d-native-presentation", "scenic",
               "--fluid25d-scenic-material", "macro", "--fluid25d-scenic-water-view", view,
               "--fluid25d-native-bank-view", bank, "--fluid25d-render-height-scale", "1",
               "--fluid25d-scenic-tuning", str(tunings[variant]), "--output", str(image)]
        completed, receipt = ref.run_logged(cmd, out / "logs", label, timeout=90,
                                            expected_presentation="scenic")
        log = completed.stdout + completed.stderr
        ref.verify_capture_log(log, True)
        if "VUID-" in log or "Validation Error" in log:
            raise ValueError("Vulkan validation error")
        if runtime != runtime_identity(app):
            raise ValueError("runtime changed during controls")
        digest = ref.sha256_file(image)
        rows.append({"case": case, "variant": variant, "view": view, "bank": bank,
                     "path": image.name, "sha256": digest, "receipt": receipt})
        print("Rendered " + label, flush=True)
        return digest

    cases = [
        ("dry", 1, 0, -3, 0), ("calm-lake", 0, .25, 0, 0),
        ("stationary-steep", 1, .25, 0, 0), ("uphill", 1, .25, 3, 0),
        ("cross-slope", 1, .25, 0, 3), ("thin-film", 1, .06, -3, 0),
        ("downhill", 1, .25, -3, 0), ("flat-fast", 0, .25, -3, 0),
    ]
    for case, grade, depth, ux, uz in cases:
        bed = [grade * (i % fixtures.COLS) * 10 for i in range(fixtures.COLS * fixtures.ROWS)]
        h = [depth] * len(bed)

        def momentum_writer(path, values, cols, rows):
            if path.name.startswith("hUx_"):
                values = [depth * ux] * len(values)
            elif path.name.startswith("hUy_"):
                values = [depth * uz] * len(values)
            writer(path, values, cols, rows)

        with patch.object(fixtures, "fields", return_value=(bed, h)), \
                patch.object(fixtures, "_write_asc", side_effect=momentum_writer):
            manifest = fixtures.fixture(out / "fixtures" / case, "fully-wet-lake")
        pins = ref.recording_tree_fingerprint(manifest.parent)
        before = render(case, manifest, "off")
        if before != render(case, manifest, "disabled-patches"):
            raise ValueError("zero foam strength must ignore patchiness/brightness: " + case)
        if (before != render(case, manifest, "flecks")) != (case == "downhill"):
            raise ValueError("fleck steep/speed/depth isolation failed: " + case)
        # The gentler branch intentionally permits fast cross-slope or uphill
        # surface flow. It is not a downhill classifier or physical foam model.
        active = case in ("uphill", "cross-slope", "flat-fast")
        for variant in ("network", "patches", "combined"):
            if (before != render(case, manifest, variant)) != active:
                raise ValueError("gentler-stream activation failed: " + case + "/" + variant)
        if case == "flat-fast":
            for view in ("coverage", "depth-bands", "direct-only"):
                if render(case, manifest, "off", view) != render(case, manifest, "combined", view):
                    raise ValueError("stream foam changed diagnostic: " + view)
        if case == "downhill":
            for view in ("coverage", "depth-bands", "direct-only"):
                if render(case, manifest, "off", view) != render(case, manifest, "flecks", view):
                    raise ValueError("whitewater changed diagnostic: " + view)
            if render(case, manifest, "off", bank="bspline-2x") == \
                    render(case, manifest, "flecks", bank="bspline-2x"):
                raise ValueError("B-spline secondary layer absent")
        if pins != ref.recording_tree_fingerprint(manifest.parent):
            raise ValueError("synthetic numerical inputs changed")
    ref.write_json_exclusive(out / "manifest.json", {
        "runtime": runtime, "assets": rows, "script_sha256": ref.sha256_file(Path(__file__)),
        "scope": "synthetic full renderer controls, not hydrology or live GUI acceptance",
        "fleck_inactive_exact": 7, "fleck_active_changes": 1,
        "stream_inactive_exact": 15, "stream_active_changes": 9,
        "disabled_patch_settings_exact": 8,
        "diagnostics_exact": 6, "bspline_active_changes": 1})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--app", type=Path, default=ref.APP)
    args = parser.parse_args()
    if args.out:
        run(args.out.resolve(), args.app.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="cubey-whitewater-") as temp:
            run(Path(temp) / "controls", args.app.resolve())
