#!/usr/bin/env python3
"""Validate and present the first captured-water gate; never launch a solver or GUI."""
from __future__ import annotations

import argparse
from array import array
import hashlib
import html
import json
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
PROBE = ROOT / "build/release/projects/fluid/fluid_25d/cubey_project_fluid_25d_terrain_alignment_probe"
TRIALS = ("runtime-stride3", "full-stride1", "lod0-stride3")
GATES = dict(diagnostic_depth_min_m=.05, burial_tolerance_m=.02,
             maximum_buried_fraction=.01, bed_mismatch_tolerance_m=.1,
             bed_mismatch_depth_fraction=.25, maximum_large_bed_mismatch_fraction=.05,
             minimum_inside_disk_coverage_fraction=.9999)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_field(path, checksum, count):
    raw = path.read_bytes()
    if len(raw) != count * 4 or hashlib.sha256(raw).hexdigest() != checksum:
        raise ValueError("changed or malformed diagnostic field: " + str(path))
    values = array("f")
    if values.itemsize != 4:
        raise ValueError("diagnostic consumer requires four-byte float")
    values.frombytes(raw)
    if sys.byteorder != "little":
        values.byteswap()
    return values


def rejected(row):
    stats = row["depth_at_least_0_05m"]
    if (stats["samples"] <= 0 or
            any(not math.isfinite(row[key]) for key in
                ("inside_disk_coverage_fraction", "lod0_vs_saved_source_bed_max_error_m")) or
            any(not math.isfinite(stats[key]) for key in
                ("buried_fraction", "large_bed_mismatch_fraction", "absolute_bed_error_p95_m"))):
        raise ValueError("empty or nonfinite alignment evidence")
    for count, fraction in (("buried_samples", "buried_fraction"),
                            ("large_bed_mismatch_samples", "large_bed_mismatch_fraction")):
        if not 0 <= stats[count] <= stats["samples"] or abs(stats[count] / stats["samples"] - stats[fraction]) > 1e-12:
            raise ValueError("inconsistent alignment counts")
    return (row["inside_disk_coverage_fraction"] < GATES["minimum_inside_disk_coverage_fraction"] or
            row["lod0_vs_saved_source_bed_max_error_m"] > .001 or
            stats["buried_fraction"] > GATES["maximum_buried_fraction"] or
            stats["large_bed_mismatch_fraction"] > GATES["maximum_large_bed_mismatch_fraction"])


def check_fields(directory, row):
    count = row["width"] * row["height"]
    fields = [load_field(directory / name, row["diagnostic_fields"][name], count) for name in
              ("ground.f32", "reference-bed.f32", "reference-depth.f32")]
    covered = samples = buried = large = 0
    errors = []
    for ground, bed, depth in zip(*fields):
        if not math.isfinite(bed) or not math.isfinite(depth) or depth < 0 or math.isinf(ground):
            raise ValueError("invalid diagnostic fields")
        if math.isnan(ground):
            continue  # Explicitly uncovered ground, not a dry hydraulic cell.
        covered += 1
        if depth >= .05:
            samples += 1
            buried += ground > bed + depth + .02
            large += abs(ground - bed) > max(.1, .25 * depth)
            errors.append(abs(ground - bed))
    stats = row["depth_at_least_0_05m"]
    if samples != stats["samples"] or covered != row["covered_inside_samples"] or not samples:
        raise ValueError("stored coverage/sample counts differ from diagnostic fields")
    # Ground maps are F32, while triangle sampling statistics use double.
    if (abs(buried / samples - stats["buried_fraction"]) > .0002 or
            abs(large / samples - stats["large_bed_mismatch_fraction"]) > .0002):
        raise ValueError("stored burial/mismatch statistics differ from diagnostic fields")
    errors.sort()
    if abs(errors[math.ceil(.95 * samples) - 1] - stats["absolute_bed_error_p95_m"]) > .001:
        raise ValueError("stored p95 differs from diagnostic fields")
    return fields


def validate(out):
    protocol = json.loads((out / "protocol.json").read_bytes())
    if (protocol["early_rejection_gates"] != GATES or
            [r["name"] for r in protocol["trials"]] != list(TRIALS)):
        raise ValueError("alignment protocol changed or trials are incomplete")
    rows = []
    shared = None
    for index, name in enumerate(TRIALS):
        row = json.loads((out / name / "alignment.json").read_bytes())
        task = protocol["trials"][index]
        expected_footprint = "lod0" if name == "lod0-stride3" else "runtime"
        identity = {k: row[k] for k in ("probe_sha256", "recording_sha256", "source_sha256",
                    "source_elevation_sha256", "width", "height", "spacing_m", "saved_time_s",
                    "source_focus_xz_m", "outer_radius_m")}
        if (row["width"] != protocol["grid"]["width"] or row["height"] != protocol["grid"]["height"] or
                row["spacing_m"] != protocol["grid"]["spacing_m"] or
                row["outer_radius_m"] != protocol["outer_radius_m"] or
                row["source_focus_xz_m"] != protocol["source_focus_xz_m"] or
                row["render_stride"] != task["render_stride"] or
                row["source_footprint"] != expected_footprint or row["numerical_fields_modified"] or
                row["new_solver_run"] or (shared is not None and identity != shared)):
            raise ValueError("trial identity/mode mismatch")
        shared = identity
        if (row["probe_sha256"] != digest(PROBE) or
                row["source_sha256"] != digest(ROOT / protocol["source"]) or
                row["recording_sha256"] != digest(ROOT / protocol["recording"])):
            raise ValueError("current probe or source/recording manifest differs")
        if rejected(row) != row["early_geometry_rejection"]:
            raise ValueError("receipt mislabels the predeclared gate")
        check_fields(out / name, row)
        rows.append(dict(name=name, **row))
    if any(r["diagnostic_fields"][name] != rows[0]["diagnostic_fields"][name]
           for r in rows for name in ("reference-bed.f32", "reference-depth.f32")):
        raise ValueError("reference fields changed between trials")
    return dict(schema="cubey.terrain.captured-water.alignment-review.v1",
                protocol_sha256=digest(out / "protocol.json"), trials=rows,
                all_trials_rejected=all(r["early_geometry_rejection"] for r in rows),
                source_and_grid_identity_checked=True, implementation_continued=False,
                boundary="CPU exact ground-triangle study, not a new scenic render, live solver or human visual acceptance")


def plots(out, receipt):
    import numpy as np
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch

    fig, axes = plt.subplots(1, 3, figsize=(15, 5), constrained_layout=True)
    labels = ("Runtime stride 3", "Full stride 1", "LOD0 stride 3 control")
    for ax, row, label in zip(axes, receipt["trials"], labels):
        arrays = [np.asarray(v).reshape(row["height"], row["width"])
                  for v in check_fields(out / row["name"], row)]
        ground, bed, depth = arrays
        eligible = np.isfinite(ground) & (depth >= .05)
        buried = eligible & (ground > bed + depth + .02)
        picture = np.full((*depth.shape, 3), (.20, .23, .25), dtype=float)
        picture[~np.isfinite(ground)] = (.10, .12, .14)
        picture[eligible] = (.24, .58, .72)
        picture[buried] = (.88, .48, .20)
        radius_km = row["outer_radius_m"] / 1000
        ax.imshow(picture, origin="lower", extent=(-radius_km, radius_km, -radius_km, radius_km))
        ax.set_title(label + f'\n{100 * row["depth_at_least_0_05m"]["buried_fraction"]:.1f}% buried samples')
        ax.set_xlabel("Local X km")
        ax.set_ylabel("Local Z km")
    fig.legend(handles=[Patch(color=(.24, .58, .72), label="Water at least 5 cm, not buried"),
                        Patch(color=(.88, .48, .20), label="Saved water surface below ground by over 2 cm")],
               loc="outside lower center", ncol=2)
    fig.savefig(out / "alignment-map.png", dpi=120)
    plt.close(fig)


def page(receipt):
    rows = []
    for r in receipt["trials"]:
        s = r["depth_at_least_0_05m"]
        shallow = r["depth_0_05_to_2m"]
        rows.append(f'<tr><td>{html.escape(r["name"])}</td><td>{r["triangles"] / 1e6:.2f} M</td>'
                    f'<td>{s["absolute_bed_error_p95_m"]:.2f} m</td><td>{100*s["buried_fraction"]:.1f}%</td>'
                    f'<td>{100*shallow["buried_fraction"]:.1f}%</td><td>Reject</td></tr>')
    title = "Captured water placement gate"
    if not receipt["all_trials_rejected"]:
        raise ValueError("this negative-result report must not mislabel a passing candidate")
    return f'''<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>{title}</title>
<style>body{{background:#111a21;color:#dce4ea;font:16px/1.6 system-ui;margin:0}}main{{max-width:1100px;margin:auto;padding:32px 24px}}
a{{color:#9bd5ed}}h1,h2{{line-height:1.2}}.lead{{font-size:20px;max-width:850px}}img{{width:100%;display:block}}
figure{{margin:28px 0}}figcaption,.muted{{color:#a1b4c1;font-size:14px}}table{{width:100%;border-collapse:collapse}}
th,td{{padding:10px;text-align:left;border-bottom:1px solid #35434d}}.scroll{{overflow:auto}}code{{overflow-wrap:anywhere}}
.stop{{border-left:4px solid #df8035;padding:12px 20px;background:#202b33}}</style></head><body><main>
<p class="muted">Cubey Terrain foundation · Captured water · 11 October 2026</p><h1>{title}</h1>
<p class="lead">Source identity and coordinates agree. The displayed ground and saved water use different reconstructions.
A direct overlay is not ready: neither full mesh density nor unfiltered source sampling fixes it.</p>
<p class="stop">Stopped at the agreed first gate. No new bake, shared asset promotion, renderer extraction or production appearance change.</p>
<h2>Where saved water would disappear</h2>
<figure><a href="alignment-map.png"><img src="alignment-map.png" alt="Three matched maps of saved water samples, with buried water in orange"></a>
<figcaption>Diagnostic top-down maps, not new scenic renders. Blue and orange both represent the same saved water with reconstructed depth at least 5 cm.
Orange is where the actual Terrain triangle lies above that water surface by more than 2 cm.
Dark outside the circle is uncovered by this explicit 8.192 km-radius ground view; it is not dry terrain.</figcaption></figure>
<h2>Bounded controls</h2><div class="scroll"><table><thead><tr><th>Ground control</th><th>Triangles</th><th>p95 bed mismatch</th>
<th>Buried water samples</th><th>Buried shallow samples</th><th>Gate</th></tr></thead><tbody>{"".join(rows)}</tbody></table></div>
<p>Fractions count spatial samples, not a camera's visible pixels or a count of lost rivers. Main statistics use depth at least 5 cm;
the shallow subset is 5 cm to 2 m. The full-density control costs 7.1 times as many ground triangles, without solving alignment.
The LOD0 control is diagnostic, not a promoted runtime change.</p>
<h2>What this rules out</h2><p>The source sampler agrees with the saved source bed to within 0.49 mm.
All 823592 native grid locations inside the disk find a ground triangle.
There is no axis swap, unexplained vertical offset or missing-center hole in these trials.
The metre-scale mismatch comes from different ground filtering and topology versus Fluid's paired B-spline bed and water.
Numerical solver fields were not changed.</p>
<h2>Retained Fluid reference</h2>
<figure><img src="../../fluid/terrain-presentation-v1-20261011-HPKiTL/final-candidate/mountain-slope-shaded.png"
alt="Previously approved Fluid Mountain stream view"><figcaption>Existing approved Fluid capture, not a new Terrain integration.
It renders ground and water with the same B-spline basis, so their separation remains the displayed water depth.</figcaption></figure>
<h2>Recommended next decision</h2><p>Keep captured water as the direction. Before baking the second state, make a bounded plan for an optional
water-compatible ground patch using the same bed reconstruction and topology as water. Preserve the source, lake levels and hydraulic fields.
Do not lift water globally, flatten terrain or spend on a globally denser backdrop. If a compatible patch would require a terrain rewrite,
retain Fluid as the working consumer and defer the shared Terrain overlay.</p>
<h2>Evidence boundary</h2><p>This is a CPU geometry rejection using the real shared product builder, not rendered acceptance.
Subcell and headless scenic comparisons are still required for any future passing candidate.
No live solver was run. A fresh selected CTest receipt covers the new sampler controls and existing headless GPU tests;
it does not validate a water-enabled Terrain runtime.</p>
<p><a href="protocol.json">Frozen protocol</a> · <a href="review.json">Review receipt</a> · <a href="tests.xml">Selected CTests</a></p>
</main></body></html>'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    receipt = validate(args.output)
    if args.verify:
        saved = json.loads((args.output / "review.json").read_bytes())
        current = dict(receipt, media_sha256=digest(args.output / "alignment-map.png"))
        if saved != current:
            raise ValueError("sealed review differs")
    else:
        if (args.output / "review.json").exists() or (args.output / "index.html").exists():
            raise ValueError("refusing to overwrite published review")
        plots(args.output, receipt)
        receipt["media_sha256"] = digest(args.output / "alignment-map.png")
        (args.output / "review.json").write_text(json.dumps(receipt, indent=2, allow_nan=False) + "\n")
        (args.output / "index.html").write_text(page(receipt))
    print(json.dumps(dict(verified=True, all_trials_rejected=receipt["all_trials_rejected"],
                         implementation_continued=False)))


if __name__ == "__main__":
    main()
