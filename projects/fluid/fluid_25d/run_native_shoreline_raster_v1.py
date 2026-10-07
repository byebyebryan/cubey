#!/usr/bin/env python3
"""Rasterized shoreline counterexamples, paired with native GPU sampler controls.

Synthetic saved fields only: this is a renderer test, not solver/hydrology
validation. A harness PASS includes correctly detecting known candidate failures;
it must never be mistaken for a reconstruction-promotion PASS.
"""
from __future__ import annotations

import argparse
from collections import deque
import json
from pathlib import Path
import subprocess
import tempfile

from convert_synxflow_recording_v1 import convert_case
from test_convert_synxflow_recording_v1 import _make_case, _write_asc
import run_native_presentation_v1 as reference

WIDTH, HEIGHT = 768, 512
COLS, ROWS = 32, 20
MODES = (("triangular", 1), ("bilinear", 1), ("bspline", 2), ("bspline", 4))
CASES = ("positive-film-gap", "one-cell-stream", "junction", "recession-early",
         "recession-late", "partial-dry-lake", "fully-wet-lake")


def fields(case: str) -> tuple[list[float], list[float]]:
    if case not in CASES:
        raise ValueError("unknown shoreline fixture")
    bed = [0.0] * (COLS * ROWS)
    h = [0.001 if case == "positive-film-gap" else 0.0] * len(bed)
    for y in range(ROWS):
        for x in range(COLS):
            i = y * COLS + x
            if case == "positive-film-gap" and 3 <= y <= 16 and x in (10, 12):
                h[i] = 0.1
            elif case == "one-cell-stream" and 3 <= y <= 16 and x == 10:
                h[i] = 0.08
            elif case == "junction" and ((6 <= x <= 22 and y == 10) or
                                          (4 <= y <= 10 and x == 14)):
                h[i] = 0.08
            elif case.startswith("recession-") and 6 <= y <= 13 and 12 <= x <= 15:
                edge = min(x - 12, 15 - x, y - 6, 13 - y)
                h[i] = (0.04 + 0.015 * edge) * (0.6 if case.endswith("late") else 1.0)
            elif case == "partial-dry-lake":
                v = (y - 9.5) / 9.5
                bed[i] = 0.36 + 0.015 * (x - 15.5) + 0.01 * v * v
                h[i] = max(0.36 - bed[i], 0.0)
            elif case == "fully-wet-lake":
                u, v = 2 * x / (COLS - 1) - 1, 2 * y / (ROWS - 1) - 1
                bed[i] = 0.12 * u * u + 0.07 * v * v + 0.025 * u * v
                h[i] = 0.5 - bed[i]
    return bed, h


def fixture(root: Path, case: str) -> Path:
    """Use the existing ASC adapter, with honest synthetic provenance."""
    source = _make_case(root)
    bed, h = fields(case)
    spec = json.loads((source / "case-spec.json").read_text())
    spec.update(cols=COLS, rows=ROWS, name="synthetic-raster-" + case,
                duration_s=2, output_interval_s=1, rain_rate_m_per_s=0.0)
    (source / "case-spec.json").write_text(json.dumps(spec))
    _write_asc(source / "DEM.asc", bed, COLS, ROWS)
    # Native serialization is bottom-row-first; preserve the adapter's reversal.
    rows = [f"{y * COLS + x} {bed[(ROWS-1-y)*COLS+x]:.6g}"
            for y in range(ROWS) for x in range(COLS)]
    boundary = [f"{y*COLS+x} 2 0 0" for y in range(ROWS) for x in range(COLS)
                if x in (0, COLS-1) or y in (0, ROWS-1)]
    z = source / "native/input/field/z.dat"
    z.write_text("\n".join(["$Element Number", str(COLS*ROWS), "$Element_id  Value",
                             *rows, "$Boundary Numbers", str(len(boundary)),
                             "$Element_id  Value", *boundary, ""]))
    protocol = json.loads((source / "case-protocol.json").read_text())
    protocol.update(case=spec, input_dem_ascii_sha256=reference.sha256_file(source / "DEM.asc"))
    # Required schema hash fields refer to actual synthetic sources here, not
    # copied Terrain Diffusion identities or pretend native solver evidence.
    source_digest = reference.sha256_file(source / "DEM.asc")
    protocol["terrain_source_identity"] = {
        "kind": "synthetic-renderer-control", "name": case,
        "scope": "analytic fields; no terrain diffusion or native solver was run",
        "crop_cell_size_m": 10, "crop_xzwh": [0,0,COLS,ROWS],
        "elevation_dtype": "synthetic ASCII", "elevation_shape": [ROWS,COLS],
        "physical_extent_m": [COLS*10,ROWS*10],
        "row_orientation": "world-z-positive row-major",
        "transform": "identity; synthetic analytic fields",
        "transformed_crop_dtype": "synthetic ASCII", "transformed_crop_shape": [ROWS,COLS],
        "transformed_halo_shape": [ROWS,COLS],
        **{key: source_digest for key in ("elevation_sha256", "fixture_sha256",
                                         "manifest_sha256", "transformed_crop_sha256",
                                         "transformed_halo_sha256")},
    }
    (source / "case-protocol.json").write_text(json.dumps(protocol))
    result = json.loads((source / "case-result.json").read_text())
    result["case"] = spec
    result["input_provenance"].update(
        case_dem_ascii_sha256=reference.sha256_file(source / "DEM.asc"),
        case_source_bed_semantics="synthetic analytic renderer fixture, not a simulation",
        native_input_audit_before_solver={"z_element_count": COLS*ROWS,
                                          "z_field_sha256": reference.sha256_file(z)})
    (source / "case-result.json").write_text(json.dumps(result))
    for t in range(3):
        for field, values in (("h", h), ("hUx", [0.0]*len(h)), ("hUy", [0.0]*len(h))):
            _write_asc(source / f"native/output/{field}_{t}.asc", values, COLS, ROWS)
    return convert_case(source, root / "recording")


def band_mask(path: Path) -> bytearray:
    result = subprocess.run(["rtk", "proxy", "ffmpeg", "-v", "error", "-i", str(path),
                             "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"],
                            capture_output=True, check=True, timeout=30)
    raw = result.stdout
    if len(raw) != WIDTH * HEIGHT * 3:
        raise ValueError("unexpected raster dimensions")
    # Opaque contours: sRGB cyan/blue are >=26mm; exclude gray film,
    # yellow/magenta lower bands and terrain. No subjective opacity cutoff.
    return bytearray(r < 100 and b > 200
                     for r, b in zip(raw[0::3], raw[2::3], strict=True))


def component_sizes(mask: bytes | bytearray, width: int, height: int) -> list[int]:
    if len(mask) != width * height:
        raise ValueError("mask dimensions differ")
    seen = bytearray(len(mask))
    sizes = []
    for seed, active in enumerate(mask):
        if not active or seen[seed]:
            continue
        seen[seed] = 1
        pending = deque([seed])
        size = 0
        while pending:
            i = pending.popleft()
            size += 1
            x, y = i % width, i // width
            for j in ((i-1 if x else -1), (i+1 if x+1 < width else -1),
                      (i-width if y else -1), (i+width if y+1 < height else -1)):
                if j >= 0 and mask[j] and not seen[j]:
                    seen[j] = 1
                    pending.append(j)
        sizes.append(size)
    return sorted(sizes, reverse=True)


def run(target: Path, out: Path) -> dict:
    reference.reserve_directory(out)
    rows, masks = [], {}
    controls = None
    for case in CASES:
        manifest = fixture(out / "fixtures" / case, case)
        for sampling, subdivision in MODES:
            name = sampling + (f"-{subdivision}x" if sampling == "bspline" else "")
            image = out / "media" / f"{case}-{name}.png"
            image.parent.mkdir(exist_ok=True)
            command = ["rtk", "proxy", str(target.resolve()), "--headless", "--width", str(WIDTH),
                       "--height", str(HEIGHT), "--fluid25d-recording", str(manifest),
                       "--fluid25d-recording-time-seconds", "1", "--fluid25d-native-presentation", "readable",
                       "--fluid25d-native-surface-highlights", "off", "--fluid25d-native-water-sampling", sampling,
                       "--fluid25d-native-water-debug", "contours", "--fluid25d-recording-gpu-validation",
                       "--output", str(image)]
            if sampling == "bspline":
                command += ["--fluid25d-native-surface-subdivision", str(subdivision)]
            result, receipt = reference.run_logged(command, out / "logs", f"{case}-{name}", timeout=60)
            if "fluid_25d_recording_upload: PASS" not in result.stdout:
                raise ValueError("missing immutable GPU upload validation")
            if sampling != "triangular":
                if "fluid_25d_bank_controls: PASS" not in result.stdout:
                    raise ValueError("missing paired numerical sampler controls")
                if controls is None:
                    controls = next(json.loads(line.split(": ", 1)[1])
                                    for line in result.stdout.splitlines()
                                    if line.startswith("fluid_25d_bank_controls_report: "))
            mask = band_mask(image)
            masks[(case, name)] = mask
            sizes = component_sizes(mask, WIDTH, HEIGHT)
            # Report every component; acceptance uses substantial components
            # (>=16 screen pixels) to exclude isolated raster-edge speckles.
            rows.append({"case": case, "mode": name, "path": str(image.relative_to(out)),
                         "sha256": reference.sha256_file(image), "command": receipt,
                         "at_least_26mm_pixels": sum(mask), "component_sizes_pixels": sizes,
                         "substantial_components": sum(size >= 16 for size in sizes)})
    checks = []
    for sampling, subdivision in MODES:
        name = sampling + (f"-{subdivision}x" if sampling == "bspline" else "")
        by_case = {r["case"]: r for r in rows if r["mode"] == name}
        gap = by_case["positive-film-gap"]["substantial_components"]
        subset = sum(late and not early for late, early in
                     zip(masks[("recession-late", name)], masks[("recession-early", name)], strict=True))
        checks.append({"mode": name, "positive_film_gap_components": gap,
                       "positive_film_gap_preserved": gap == 2,
                       "one_cell_stream_connected": by_case["one-cell-stream"]["substantial_components"] == 1,
                       "junction_connected": by_case["junction"]["substantial_components"] == 1,
                       "recession_new_above_band_pixels": subset,
                       "recession_no_new_connections": subset == 0,
                       "partial_dry_lake_visible": by_case["partial-dry-lake"]["at_least_26mm_pixels"] > 0,
                       "partial_dry_stage_scope": "raster presence only; precision comes from paired GPU controls"})
    # Known failure is a successful detection, not a passed product gate.
    reference_check = checks[0]
    if not reference_check["positive_film_gap_preserved"]:
        raise ValueError("triangular positive-film-gap reference was not separated")
    for check in checks:
        if not check["one_cell_stream_connected"] or not check["junction_connected"]:
            raise ValueError("stream/junction raster lost connectivity")
        if not check["recession_no_new_connections"]:
            raise ValueError("recession invented above-band raster pixels")
        if not check["partial_dry_lake_visible"]:
            raise ValueError("partial-dry lake disappeared from the raster")
    if checks[-1]["positive_film_gap_preserved"]:
        raise ValueError("known B-spline positive-film-gap counterexample was not detected")
    if not controls or not controls["bspline"]["positive_film_gap_counterexample"]["bridge_observed"]:
        raise ValueError("paired numerical controls did not report known film-gap failure")
    if controls["bspline"]["partial_dry_lake_counterexample"]["max_stage_deviation_m"] <= 0.000002:
        raise ValueError("known partial-dry lake stage counterexample was not detected")
    report = {"schema": "cubey.fluid25d.shoreline_raster.v1", "harness_status": "pass",
              "candidate_promotion_status": "rejected-known-topology-and-partial-dry-stage-failures",
              "visibility_band_m": 0.026, "component_connectivity": "four-neighbor",
              "minimum_substantial_component_pixels": 16,
              "scope": "fixed shared oblique camera, opaque depth-band raster visibility after terrain depth test; not physical area, solver proof or a numeric lake-stage measurement",
              "executable_sha256": reference.sha256_file(target),
              "compiled_shaders": {p.name: reference.sha256_file(p)
                                   for p in sorted((target.resolve().parent/"shaders").glob("*.spv"))},
              "checks": checks, "assets": rows, "paired_numerical_controls": controls}
    reference.write_json_exclusive(out / "report.json", report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    try:
        if args.out:
            report = run(args.target, args.out)
        else:
            with tempfile.TemporaryDirectory(prefix="cubey-shoreline-raster-") as temporary:
                report = run(args.target, Path(temporary) / "evidence")
        print(json.dumps({k: v for k, v in report.items() if k != "assets"}, indent=2))
    except RuntimeError as error:
        if any(s in str(error) for s in ("no Vulkan physical devices found", "vkEnumeratePhysicalDevices",
                                         "no Vulkan device with required queues")):
            return 77
        raise
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
