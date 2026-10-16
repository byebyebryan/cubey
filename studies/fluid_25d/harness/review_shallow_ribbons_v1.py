#!/usr/bin/env python3
"""Frozen replay comparison of existing film tuning and opt-in depth coverage."""

from __future__ import annotations

import argparse
import html
import json
import xml.etree.ElementTree as ET
from pathlib import Path

import review_water_agitation_v1 as previous

ref = previous.ref
SCENES = previous.SCENES[:3]
VARIANTS = {
    "reference": {},
    "ground-30": {"film_end_m": 0.30, "film_ground_mix": 1},
    "ground-60": {"film_end_m": 0.60, "film_ground_mix": 1},
    "fade-30": {
        "water_shallow_coverage_strength": 1,
        "water_shallow_coverage_end_m": 0.30,
    },
    "fade-60": {
        "water_shallow_coverage_strength": 1,
        "water_shallow_coverage_end_m": 0.60,
    },
    "both-30": {
        "film_end_m": 0.30,
        "film_ground_mix": 1,
        "water_shallow_coverage_strength": 1,
        "water_shallow_coverage_end_m": 0.30,
    },
}
TITLES = {
    "reference": "Current default",
    "ground-30": "Wet-ground blend to 30 cm",
    "ground-60": "Wet-ground blend to 60 cm (stronger)",
    "fade-30": "Coverage fade to 30 cm",
    "fade-60": "Coverage fade to 60 cm (stronger)",
    "both-30": "Both at 30 cm",
}


def capture(out, variant, label):
    phase = out / label
    ref.reserve_directory(phase)
    runtime = previous.runtime_identity(ref.APP)
    inputs = previous.inputs()
    sources = previous.sources()
    sources[str(Path(__file__).resolve().relative_to(ref.ROOT))] = ref.sha256_file(
        Path(__file__)
    )
    tuning = phase / "tuning.json"
    ref.write_json_exclusive(
        tuning, {"schema": "cubey.fluid25d.scenic-material.v2", **VARIANTS[variant]}
    )
    rows = []
    for scene in SCENES:
        image = phase / (scene[0] + ".png")
        completed, receipt = ref.run_logged(
            previous.command(scene, image, tuning),
            phase / "logs",
            scene[0],
            timeout=180,
            expected_presentation="scenic",
        )
        log = completed.stdout + completed.stderr
        ref.verify_capture_log(log, True)
        if "VUID-" in log or "Validation Error" in log:
            raise ValueError("Vulkan validation error")
        rows.append(
            {
                "scene": scene[0],
                "saved_time_s": scene[2],
                "path": image.name,
                "sha256": ref.sha256_file(image),
                "receipt": receipt,
            }
        )
        print("Captured " + label + "/" + image.name, flush=True)
    current_sources = previous.sources()
    current_sources[str(Path(__file__).resolve().relative_to(ref.ROOT))] = (
        ref.sha256_file(Path(__file__))
    )
    if (
        runtime != previous.runtime_identity(ref.APP)
        or inputs != previous.inputs()
        or sources != current_sources
    ):
        raise ValueError("runtime, sources or native inputs changed during capture")
    ref.write_json_exclusive(
        phase / "manifest.json",
        {
            "runtime": runtime,
            "inputs": inputs,
            "sources": sources,
            "variant": variant,
            "tuning": VARIANTS[variant],
            "assets": rows,
            "scope": "held replay; render-only artist tuning; zero hydraulic dispatches",
        },
    )


def report(out):
    phases = {
        name: json.loads((out / name / "manifest.json").read_text())
        for name in ("prechange", *VARIANTS)
    }
    reference = phases["reference"]
    hashes = lambda phase: {row["scene"]: row["sha256"] for row in phase["assets"]}
    if hashes(phases["prechange"]) != hashes(reference):
        raise ValueError("disabled/default appearance differs from pre-change build")
    for name, phase in phases.items():
        if phase["inputs"] != reference["inputs"]:
            raise ValueError("comparison native inputs differ")
        if name != "prechange" and (
            phase["runtime"] != reference["runtime"]
            or phase["sources"] != reference["sources"]
        ):
            raise ValueError("comparison runtime or sources differ")
        for row in phase["assets"]:
            if ref.sha256_file(out / name / row["path"]) != row["sha256"]:
                raise ValueError("media differs from its capture receipt")
    verdict_path = out / "verdict.json"
    verdict = json.loads(verdict_path.read_text()) if verdict_path.exists() else None
    verdict_html = ""
    if verdict:
        verdict_html = (
            "<section><h2>Review verdict</h2><p>"
            + html.escape(verdict["summary"])
            + "</p><p><strong>Recommendation:</strong> "
            + html.escape(verdict["recommendation"])
            + '</p><p><a href="RESULTS.md">Detailed review and evidence boundaries</a></p></section>'
        )
    checks = {}
    for receipt_name in ("tests.xml", "gpu-recheck.xml"):
        receipt_path = out / receipt_name
        if receipt_path.exists():
            for test in ET.parse(receipt_path).getroot().iter("testcase"):
                checks[test.get("name")] = not any(
                    test.find(tag) is not None
                    for tag in ("failure", "error", "skipped")
                )
    if checks and not all(checks.values()):
        raise ValueError("latest regression receipts contain failures or skips")
    validation_html = (
        f"<p>{len(checks)} distinct regression checks passed in their latest runs. "
        '<a href="tests.xml">Initial checks</a> · '
        '<a href="gpu-recheck.xml">Source-frozen GPU repeat</a>. '
        "The initial source-seal rejection and its repeat are retained, not overwritten.</p>"
        if checks
        else "<p>Regression receipts have not been added yet.</p>"
    )
    options = "".join(
        f'<option value="{name}">{html.escape(title)}</option>'
        for name, title in TITLES.items()
        if name != "reference"
    )
    cards = "".join(
        f"<section><h2>{scene[0].title()}</h2><p>Same saved water at {scene[2]:.0f}s. "
        "Left: current default. Right: selected experiment.</p>"
        f'<label>Experiment <select data-scene="{scene[0]}">{options}</select></label>'
        f'<div class="wipe"><img class="after" src="ground-30/{scene[0]}.png">'
        f'<img class="before" src="reference/{scene[0]}.png"></div>'
        '<input aria-label="Before-after divider" type="range" min="0" max="100" value="50">'
        "</section>"
        for scene in SCENES
    )
    rows = "".join(
        f'<tr><td>{html.escape(TITLES[name])}</td><td><a href="{name}/tuning.json">Tuning JSON</a></td>'
        f'<td><a href="{name}/manifest.json">Capture receipts</a></td></tr>'
        for name in VARIANTS
    )
    page = (
        '<!doctype html><html lang="en"><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1">'
        "<title>Fluid 2.5D · Simple ribbon treatments</title><style>"
        "body{max-width:1200px;margin:auto;padding:24px;background:#111820;color:#dee6ec;font:16px/1.5 system-ui}"
        "a{color:#91d5ff}section{margin:28px 0;padding:16px;background:#1c2631;border-radius:10px}"
        ".wipe{position:relative;margin-top:12px}.wipe img{width:100%;display:block}"
        ".before{position:absolute;inset:0;clip-path:inset(0 50% 0 0)}input{width:100%}"
        "select{font:inherit;max-width:100%}td{padding:8px}h1,h2{line-height:1.2}</style>"
        '<nav><a href="/">All reports</a></nav><h1>Simple ribbon treatments</h1>'
        "<p>No new streams, noise masks or geometry. These experiments only make shallow water "
        "less visually dominant over existing wet terrain. Simulation, rain, lighting and agitation strengths stay fixed.</p>"
        + verdict_html
        + "<p>Start with <strong>Streams</strong>: compare ground blend against coverage fade, then both. "
        "Use <strong>Lake</strong> to check shallow shoreline changes; deeper interiors should retain their appearance. "
        "The 60 cm versions are stronger probes, not physical thresholds or new defaults.</p>"
        + cards
        + "<h2>Reproduce and validate</h2><table>"
        + rows
        + "</table>"
        + validation_html
        + "<p>Default/disabled output exactly matches three pre-change captures. "
        "All variants share compiled shaders, executable and native recordings. "
        "Native upload validation passes with zero hydraulic dispatches. This is a frozen rendering "
        "comparison, not a simulation result or proof that small channels formed.</p>"
        "<p>Coverage fade affects shaded water and its coverage diagnostic only; raw depth, film-weight "
        "and lighting-component diagnostics retain their underlying coverage. There is no time-dependent mask.</p>"
        "<p>Implementation: two material values packed into existing reserved uniform slots, "
        "one smoothstep/mix in the existing water pass. Ground-only variants use existing film controls, "
        "including their existing roughness and fine-detail depth gates; they are not isolated color-only changes.</p>"
        '<script>for(const s of document.querySelectorAll("select"))s.onchange=()=>'
        '{s.closest("section").querySelector(".after").src=s.value+"/"+s.dataset.scene+".png"};'
        'for(const r of document.querySelectorAll("input"))r.oninput=()=>'
        '{r.previousElementSibling.querySelector(".before").style.clipPath='
        "`inset(0 ${100-r.value}% 0 0)`};</script></html>"
    )
    ref.write_text_exclusive(out / "index.html", page)
    ref.write_json_exclusive(
        out / "comparison.json",
        {
            "default_matches_prechange": True,
            "native_inputs_unchanged": True,
            "runtime": reference["runtime"],
            "variants": {name: phases[name]["tuning"] for name in VARIANTS},
            "default_changed": False,
            "verdict": verdict,
            "latest_regression_checks": checks,
            "scope": "frozen rendering comparison; no solver or visual acceptance claim",
        },
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("capture", "report"))
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--variant", choices=VARIANTS, default="reference")
    parser.add_argument("--label")
    args = parser.parse_args()
    out = args.out.resolve()
    if (
        out.parent != ref.OUTPUT_ROOT
        or not out.name.startswith("shallow-ribbon-")
        or args.out.is_symlink()
    ):
        parser.error("use a shallow-ribbon-* leaf under outputs/fluid")
    label = args.label or args.variant
    if label not in ("prechange", *VARIANTS):
        parser.error("unknown capture phase")
    if args.action == "capture":
        capture(out, args.variant, label)
    else:
        report(out)


if __name__ == "__main__":
    main()
