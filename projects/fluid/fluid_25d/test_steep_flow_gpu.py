#!/usr/bin/env python3
"""Synthetic full-renderer controls. No archive, terrain model or solver required."""

import argparse
from pathlib import Path
from unittest.mock import patch

import run_native_shoreline_raster_v1 as fixtures
import run_native_presentation_v1 as ref
from test_scenic_water_gpu import runtime_identity
from scenic_flow_reference import RETAINED_FLOW_CUES


def run(out, app, variant="rapid"):
    ref.reserve_directory(out)
    runtime = runtime_identity(app)
    off = out / "off.json"
    ref.write_json_exclusive(off, {"schema": "cubey.fluid25d.scenic-material.v2", **RETAINED_FLOW_CUES})
    on = out / "on.json"
    controls = {"water_rapid_strength": 1, "water_rapid_scale_m": 192} if variant == "rapid" else {
        "water_cascade_strength": 1, "water_landing_strength": 1}
    ref.write_json_exclusive(on, {"schema": "cubey.fluid25d.scenic-material.v2",
                                  **RETAINED_FLOW_CUES, **controls})
    landing = out / "landing.json"
    ref.write_json_exclusive(landing, {"schema": "cubey.fluid25d.scenic-material.v2",
                                      **RETAINED_FLOW_CUES,
                                      "water_landing_strength": 1})
    rows = []
    writer = fixtures._write_asc

    def render(case, manifest, enabled, view, tuning=on):
        label = case + ("-on-" if enabled else "-off-") + view
        image = out / (label + ".png")
        cmd = ["rtk", "proxy", str(app), "--headless", "--width", "384", "--height", "288",
               "--fluid25d-recording", str(manifest), "--fluid25d-recording-time-seconds", "1",
               "--fluid25d-recording-gpu-validation", "--fluid25d-native-presentation", "scenic",
               "--fluid25d-scenic-material", "macro", "--fluid25d-scenic-water-view", view,
               "--fluid25d-render-height-scale", "1", "--output", str(image)]
        cmd += ["--fluid25d-scenic-tuning", str(tuning if enabled else off)]
        completed, receipt = ref.run_logged(cmd, out / "logs", label, timeout=90,
                                            expected_presentation="scenic")
        ref.verify_capture_log(completed.stdout + completed.stderr, True)
        if "VUID-" in completed.stderr or "Validation Error" in completed.stderr:
            raise ValueError("Vulkan validation error")
        if runtime != runtime_identity(app):
            raise ValueError("runtime changed during controls")
        rows.append({"case": case, "enabled": enabled, "view": view, "path": image.name,
                     "sha256": ref.sha256_file(image), "receipt": receipt})
        print("Rendered " + label, flush=True)
        return ref.sha256_file(image)

    cases = [
        ("dry", 1, 0, -3, 0),
        ("calm-flat", 0, 0.25, 0, 0),
        ("stationary-steep", 1, 0.25, 0, 0),
        ("cross-slope", 1, 0.25, 0, 3),
        ("uphill", 1, 0.25, 3, 0),
        ("downhill", 1, 0.25, -3, 0),
    ]
    if variant == "cascade":
        cases += [("flat-fast", 0, 0.25, -3, 0), ("landing", 1, 0.25, -3, 0)]
    for case, grade, depth, ux, uz in cases:
        bed = [grade * (i % fixtures.COLS) * 10 for i in range(fixtures.COLS * fixtures.ROWS)]
        if case == "landing":
            bed = [max(0, i % fixtures.COLS - 16) * 10 for i in range(len(bed))]
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
        before = render(case, manifest, False, "shaded")
        after = render(case, manifest, True, "shaded", landing if case == "landing" else on)
        if (before == after) != (case not in ("downhill", "landing")):
            raise ValueError("rapid activation/isolation failed: " + case)
        if case == "downhill":
            for view in ("coverage", "depth-bands", "direct-only"):
                if render(case, manifest, False, view) != render(case, manifest, True, view):
                    raise ValueError("rapid material changed retained diagnostic: " + view)
            if variant == "cascade":
                if before != render("steady-grade-landing", manifest, True, "shaded", landing):
                    raise ValueError("landing foam activated on a steady inclined plane")
        if pins != ref.recording_tree_fingerprint(manifest.parent):
            raise ValueError("synthetic numerical input changed")
    ref.write_json_exclusive(out / "manifest.json", {
        "runtime": runtime, "assets": rows, "script_sha256": ref.sha256_file(Path(__file__)),
        "scope": "synthetic analytic renderer controls, not a solver or real-terrain test",
        "variant": variant, "inactive_shaded_exact": 5 if variant == "rapid" else 7,
        "active_shaded_changes": 1 if variant == "rapid" else 2, "diagnostics_exact": 3})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--app", type=Path, default=ref.APP)
    parser.add_argument("--variant", choices=("rapid", "cascade"), default="rapid")
    args = parser.parse_args()
    if args.out:
        run(args.out.resolve(), args.app.resolve(), args.variant)
    else:
        import tempfile
        with tempfile.TemporaryDirectory(prefix="cubey-steep-flow-") as temp:
            run(Path(temp) / "controls", args.app.resolve(), args.variant)
