#!/usr/bin/env python3
"""Headless first-use qualification and compact saved-landscape handoff."""
import argparse
import html
import json
from pathlib import Path
import re
import shutil

import review_landscape_hardening_v1 as hard

base = hard.base
ROOT = base.ROOT
PRIOR = ROOT / "outputs/terrain/landscape-hardening-qualified-20261013-m9s7qd"
VIEWS = {
    "overview": ("mountain-local", False, None, True),
    "detail": ("mountain-local", False, 6500, True),
    "hidden": ("mountain-local", False, None, False),
    "detail-hidden": ("mountain-local", False, 6500, False),
    "wide": ("mountain-wide", True, None, True),
    "valley": ("valley-interior-wide", True, None, True),
    "dry": ("mountain-dry", False, None, True),
}


def legacy_inputs():
    cache = ROOT / "build/release/CMakeCache.txt"
    text = cache.read_text()
    paths = set()
    for name in ("CUBEY_TERRAIN_DEFAULT_ASSET_DIR", "CUBEY_TERRAIN_SURFACE_STUDY_ASSET_DIR"):
        match = re.search(r"^" + name + r":PATH=(.+)$", text, re.M)
        if not match:
            raise ValueError("missing compiled legacy source setting: " + name)
        paths.update(p for p in Path(match[1]).rglob("*") if p.is_file())
    # Opening-frame references and checked metadata remain immutable too.
    paths.update(PRIOR / (id + "-parity.png") for id in base.WET)
    paths.add(PRIOR / "mountain-local-local.png")
    return {str(p): base.render.digest(p) for p in sorted(paths)}


def verify(out):
    protocol = hard.verify(out)
    if protocol["first_use_driver_sha256"] != base.render.digest(Path(__file__)):
        raise ValueError("first-use driver changed; retain old evidence")
    if protocol["legacy_inputs"] != legacy_inputs():
        raise ValueError("legacy/reference assets changed")
    if protocol["browser_check_sha256"] != base.render.digest(Path(__file__).with_name("check_landscape_first_use_report.mjs")):
        raise ValueError("browser report checker changed")
    return protocol


def view_command(name):
    id, wide, span, water = VIEWS[name]
    args = base.command(id, wide=wide, water=water)
    if span:
        args += ["--terrain-water-span", str(span)]
    return args


def image(out, name, args):
    path = out / (name + ".png")
    receipt = base.checked(args + ["--frames", "1", "--width", "1280", "--height", "720",
        "--terrain-water-audit", "--output", str(path)], out / (name + ".log"))
    return path, receipt


def capture(out):
    verify(out)
    rows = []
    for view in VIEWS:
        # Reused negative helper's proposed-default frame has this exact name.
        name = "mountain-local-local" if view == "overview" else view
        path, receipt = image(out, name, view_command(view))
        text = (out / (name + ".log")).read_text()
        mode = "Terrain only - no water loaded" if view == "dry" else "Saved water - no solver running"
        if mode not in text or "hydraulic_dispatches=0" not in text:
            raise ValueError("saved/dry mode or immutable GPU audit missing")
        if view != "dry" and "Saved after 12.00 simulated hours at 512 mm/h rain." not in text:
            raise ValueError("capture history label missing")
        rows.append({"view": view, "image": path.name, **receipt})
    opening = hard.exact(out / "mountain-local-local.png", PRIOR / "mountain-local-local.png")
    parity = []
    for id in base.WET:
        path, receipt = image(out, "parity-" + id, base.command(id, locked=True))
        parity.append({"id": id, "difference": hard.exact(path, PRIOR / (id + "-parity.png")), **receipt})
    contrast = base.render.pixel_difference(out / "mountain-local-local.png", out / "hidden.png")
    if not contrast["different_channels"]:
        raise ValueError("water-hidden control did not affect the picture")
    clips = []
    for name, moving in (("motion", True), ("held", False)):
        args = view_command("detail")
        if not moving:
            args += ["--no-terrain-water-animate"]
        receipt = base.checked(args + ["--capture", "video", "--frames", "240", "--fps", "24",
            "--width", "1280", "--height", "720", "--terrain-water-audit", "--output",
            str(out / (name + ".mp4"))], out / (name + ".log"))
        clips.append({"name": name, "moving_surface_only": moving, **receipt})
    base.write(out / "capture.json", {"views": rows, "opening_parity": opening,
        "historical_parity": parity, "water_hidden_difference": contrast, "clips": clips})
    verify(out)


def first_use(out):
    verify(out)
    default = ["rtk", "proxy", str(base.render.APP), "--headless"]
    no_argument, run = image(out, "no-selection", default)
    explicit, control = image(out, "legacy", base.command("legacy"))
    difference = hard.exact(no_argument, explicit)
    if "terrain_landscape_summary:" in (out / "no-selection.log").read_text():
        raise ValueError("default promoted before native review")
    foreign, receipt = image(out, "foreign-cwd", ["rtk", "proxy", "env", "-C", str(out),
        *base.command("mountain-local")])
    foreign_difference = hard.exact(foreign, out / "mountain-local-local.png")
    failures = []
    for name, option in (("legacy-material", ["--terrain-surface-model", "mineral-control"]),
                         ("legacy-light", ["--sun-elevation", "30"])):
        log = out / (name + ".log")
        rejected = base.checked(base.command("mountain-local") + option, log, 1)
        if "legacy terrain options" not in log.read_text() or "NVIDIA GeForce" in log.read_text():
            raise ValueError("unsupported legacy option did not reject before GPU startup")
        failures.append({"case": name, **rejected})
    base.write(out / "first-use.json", {"no_argument_legacy": {"run": run, "control": control,
        "difference": difference}, "foreign_cwd": {"run": receipt, "difference": foreign_difference},
        "unsupported_legacy_controls": failures, "default_promoted": False})
    verify(out)


def performance(out):
    verify(out)
    rows = []
    for id in ("mountain-local", "mountain-wide"):
        for repeat in range(2):
            prefix = out / f"performance-{id}-{repeat}"
            receipt = base.checked(base.command(id) + ["--headless-render-only", "--run-seconds", "12",
                "--render-fps", "60", "--width", "1920", "--height", "1080", "--no-validation",
                "--profile-output", str(prefix), "--profile-warmup-frames", "120",
                "--profile-sample-interval", "1"], prefix.with_suffix(".log"))
            timing = base.render.timings(prefix)
            rows.append({"id": id, "repeat": repeat, **receipt, **timing,
                "pass": timing["whole_gpu_p95_ms"] <= 8 and timing["cpu_service_p95_ms"] <= 4})
            print("timed", id, repeat, timing["whole_gpu_p95_ms"], flush=True)
    base.write(out / "performance.json", {"rows": rows,
        "scope": "Representative 32m/64m, 1080p headless Vulkan; solver stopped; no readback/encoding"})
    verify(out)


def qualify(out):
    protocol = verify(out)
    hard.qualification(out)
    capture = json.loads((out / "capture.json").read_bytes())
    timing = json.loads((out / "performance.json").read_bytes())["rows"]
    first = json.loads((out / "first-use.json").read_bytes())
    life = json.loads((out / "lifecycle.json").read_bytes())
    negative = json.loads((out / "negative.json").read_bytes())
    if {r["view"] for r in capture["views"]} != set(VIEWS) or len(capture["views"]) != len(VIEWS):
        raise ValueError("incomplete capture views")
    if ({r["id"] for r in capture["historical_parity"]} != set(base.WET) or
        len(capture["historical_parity"]) != 4 or any(r["difference"]["max_abs_8bit"] for r in capture["historical_parity"]) or
        capture["opening_parity"]["max_abs_8bit"] or {r["name"] for r in capture["clips"]} != {"motion", "held"}):
        raise ValueError("appearance or motion receipts invalid")
    expected = {(id, repeat) for id in ("mountain-local", "mountain-wide") for repeat in range(2)}
    if len(timing) != 4 or {(r["id"], r["repeat"]) for r in timing} != expected or not all(r["pass"] for r in timing):
        raise ValueError("incomplete or failed representative performance")
    if (first["default_promoted"] or first["no_argument_legacy"]["difference"]["max_abs_8bit"] or
        first["foreign_cwd"]["difference"]["max_abs_8bit"] or len(first["unsupported_legacy_controls"]) != 2 or
        len(negative["failures"]) != 4 or negative["auto_parity"]["max_abs_8bit"] or len(life["applies"]) != 5):
        raise ValueError("first-use/lifecycle/default boundary qualification failed")
    return {"tests": len(protocol["required_tests"]), "hardening_controls": len(hard.CASES),
        "performance": timing, "default_promoted": False, "native_gui_review": "deferred",
        "historical_frames_exact": 4, "opening_frame_exact": True,
        "native_checklist": ["Opening framing and panel readability", "Saved state vs live simulation understood",
            "Site/water selection leaves Showing unchanged until Apply", "Loading and failure feedback",
            "Wet/dry replacement and controls", "Local/Wider framing and camera reset",
            "Water hidden and motion off/on", "Approve or defer automatic prepared-landscape startup"]}


def report(out):
    summary = qualify(out)
    export = out / "export"
    export.mkdir()
    for row in json.loads((out / "capture.json").read_bytes())["views"]:
        shutil.copyfile(out / row["image"], export / row["image"])
    for name in ("motion.mp4", "held.mp4", "protocol.json", "compatibility.json", "capture.json",
                 "first-use.json", "lifecycle.json", "negative.json", "performance.json", "regression-tests.xml"):
        shutil.copyfile(out / name, export / name)
    base.write(export / "qualification.json", summary)
    table = ''.join(f'<tr><td>{html.escape(r["id"])} / {r["repeat"]+1}</td><td>{r["whole_gpu_p95_ms"]:.3f}</td><td>{r["cpu_service_p95_ms"]:.3f}</td></tr>' for r in summary["performance"])
    checklist = ''.join(f'<label><input type="checkbox">{html.escape(item)}</label>' for item in summary["native_checklist"])
    document = '''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Terrain + saved water: first-use review</title>
<style>body{margin:0;background:#151b21;color:#e4eaf0;font:16px/1.5 system-ui}main{max-width:1180px;margin:auto;padding:24px}h1{font-size:2rem}h2{margin-top:2em}a{color:#94d4df}img,video{display:block;width:100%;height:auto}figure{margin:16px 0}figcaption,.muted{color:#bbc6d0}button{background:#263540;color:#edf6fb;border:1px solid #6b879b;border-radius:6px;padding:9px 14px;font:inherit;cursor:pointer}button[aria-pressed=true]{background:#386875}button:disabled{opacity:.5;cursor:default}.controls{display:flex;flex-wrap:wrap;gap:10px;align-items:center}.notice{padding:14px;background:#25343e;border-left:4px solid #85cbd5}.pair{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}table{width:100%;border-collapse:collapse}td,th{padding:8px;text-align:left;border-bottom:1px solid #4a5d6a}details{margin:20px 0}summary{cursor:pointer}label{display:block;margin:8px 0}input{margin-right:8px}pre{overflow:auto;padding:12px;background:#202a32}code{font-size:.9rem}@media(max-width:700px){main{padding:14px}.pair{grid-template-columns:1fr}h1{font-size:1.6rem}}</style></head><body><main>
<h1>Terrain + saved water: first-use review</h1><p class="notice"><strong>Ready for joint GUI review; not promoted.</strong> These are saved hydraulic states, not a running solver. Depth and velocity stay fixed; surface cues can animate. The live Fluid demo remains separate and unchanged.</p>
<h2>1. Inspect the starting scene</h2><p>Original Mountain / 32m remains the candidate. Follow the turquoise tributaries downhill into the broad blue pools. The snow-topped ridge gives terrain scale; a smoother bank is display reconstruction, not extra simulation detail.</p>
<div class="controls"><button data-view="overview" aria-pressed="true" disabled>Opening view</button><button data-view="detail" aria-pressed="false" disabled>Stream/lake detail</button><button data-view="wide" aria-pressed="false" disabled>Wider 64m state</button><button data-view="valley" aria-pressed="false" disabled>Valley stress view</button><button data-view="dry" aria-pressed="false" disabled>Terrain only</button><label><input id="water" type="checkbox" checked disabled>Show water</label></div>
<figure><img id="scene" src="mountain-local-local.png" alt="Original mountain saved-water opening view"><figcaption id="caption">Original mountain: 65.50 km sampled water / 32m cells, with a 131.07 km prepared terrain backdrop. Twelve simulated hours under exaggerated 512 mm/h rain.</figcaption></figure>
<noscript><p>JavaScript is optional. <a href="detail.png">Detail</a> · <a href="hidden.png">Water hidden</a> · <a href="wide.png">Wider state</a> · <a href="valley.png">Valley</a> · <a href="dry.png">Terrain only</a></p></noscript>
<h2>2. Watch surface motion</h2><p>Both ten-second clips hold the same hydraulic state and camera. Left animates saved flow cues; right holds their presentation clock. Neither grows streams or changes water levels.</p><div class="pair"><figure><video id="moving" controls loop muted playsinline preload="metadata" src="motion.mp4"></video><figcaption>Decorative motion on</figcaption></figure><figure><video id="held" controls loop muted playsinline preload="metadata" src="held.mp4"></video><figcaption>Held appearance reference</figcaption></figure></div><button id="play" disabled>Restart and play both</button><p id="play-status" class="muted" aria-live="polite">Native video controls work without JavaScript.</p>
<h2>3. What changed in the GUI</h2><ul><li>Human-readable Showing and Selected labels; selection changes the scene only after Apply.</li><li>Saved-water / terrain-only mode, separate water/backdrop coverage, and capture-history rainfall.</li><li>Framing, Show water, Animate saved flow cues and Reset camera stay prominent.</li><li>Hashes, grid internals, load timings and appearance/coverage options are collapsible.</li><li>Loading identifies the requested landscape; failures keep the previous scene. Upload may briefly pause rendering.</li></ul><p>These descriptions are backed by portable metadata tests and headless logs. They are <strong>not screenshots or acceptance of the native panel layout</strong>.</p>
<h2>4. Joint native review / promotion gate</h2><p>The boxes below are personal review notes only; they do not change files, acknowledge acceptance, or promote defaults.</p>CHECKLIST
<pre><code>rtk proxy build/release/projects/terrain/terrain --terrain-landscape mountain-local</code></pre><p>No unattended GUI was launched. Normal startup remains legacy. After approval, the separate promotion change can make <code>auto</code> the default while retaining explicit legacy options and missing-cache fallback.</p>
<details><summary>Measured qualification and boundaries</summary><p>TESTCOUNT regression groups pass without skips; ten retained hardening controls plus startup/foreign-working-directory and incompatible-control checks pass. The opening frame and all four historical wet frames are pixel-exact. Saved fields are audited unchanged, with zero hydraulic dispatches.</p><p>Representative 32m/64m repeats: 1080p headless Vulkan, RTX 5070 Ti, 120 warmup frames excluded, solver stopped, no encoding/readback. Gates remain whole-scene GPU ≤8ms and CPU service ≤4ms. This is not native monitor FPS or live-solver performance.</p><table><tr><th>Preset / repeat</th><th>GPU p95 ms</th><th>CPU service p95 ms</th></tr>TABLE</table><p>States are independent simulations, not coupled water LOD. Finite terrain/water edges, far-shadow limitations and coarse sheet flooding remain. The 512 mm/h supply is a visual stress storm, not calibrated hydrology or permanent-river evidence.</p><p><a href="qualification.json">Qualification</a> · <a href="protocol.json">Frozen identities</a> · <a href="capture.json">Captures/parity</a> · <a href="first-use.json">First use</a> · <a href="compatibility.json">Hardening</a> · <a href="lifecycle.json">Replacement</a> · <a href="negative.json">Errors/auto</a> · <a href="performance.json">Timings</a> · <a href="regression-tests.xml">Tests</a></p></details>
<script>
const views={overview:['mountain-local-local.png','hidden.png','Original mountain: 65.50 km sampled water / 32m cells, with a 131.07 km prepared terrain backdrop. Twelve simulated hours under exaggerated 512 mm/h rain.'],detail:['detail.png','detail-hidden.png','Same original-mountain state, tighter framing. Hiding water retains the same wet terrain and saved fields.'],wide:['wide.png',null,'Original mountain: independent 131.01 km sampled water / 64m cells. More coverage, coarser local water; not an outer ring around the 32m state.'],valley:['valley.png',null,'Interior valley / 64m: pooling-heavy stress alternative. Inspect coarse flooded areas and finite water/terrain boundaries.'],dry:['dry.png',null,'Original mountain, terrain only. No hydraulic capture or rainfall history is loaded.']};
let current='overview';const water=document.getElementById('water'),scene=document.getElementById('scene'),caption=document.getElementById('caption');
function update(){const v=views[current];water.disabled=!v[1];scene.src=v[water.checked||!v[1]?0:1];scene.alt=current==='dry'?'Terrain-only mountain landscape':current+' landscape '+(water.checked?'water shown':'water hidden');caption.textContent=v[2];document.querySelectorAll('[data-view]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.view===current)));}
document.querySelectorAll('[data-view]').forEach(b=>{b.disabled=false;b.addEventListener('click',()=>{current=b.dataset.view;water.checked=current!=='dry';update();});});water.disabled=false;water.addEventListener('change',update);
const play=document.getElementById('play');play.disabled=false;play.addEventListener('click',async()=>{const clips=[document.getElementById('moving'),document.getElementById('held')];clips.forEach(v=>v.currentTime=0);const results=await Promise.allSettled(clips.map(v=>v.play()));document.getElementById('play-status').textContent=results.every(r=>r.status==='fulfilled')?'Both clips restarted. Fixed hydraulics; appearance motion only.':'Use the individual video controls if this browser cannot play both.';});
</script></main></body></html>'''
    document = document.replace("CHECKLIST", checklist).replace("TESTCOUNT", str(summary["tests"])).replace("TABLE", table)
    (export / "index.html").write_text(document)
    base.write(out / "report-receipt.json", {"summary": summary,
        "export_assets": {p.name: base.render.digest(p) for p in sorted(export.iterdir())},
        "scope": "Browser report, not native GUI acceptance. No default promotion or push."})
    verify(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--phase", required=True, choices=("freeze", "capture", "first_use", "compatibility", "lifecycle", "negative", "performance", "report"))
    args = parser.parse_args()
    out = args.out.resolve()
    if args.phase == "freeze":
        base.prepare.prepare(base.CATALOG, ROOT / "cache/terrain")
        tests = json.loads((PRIOR / "protocol.json").read_bytes())["required_tests"]
        tests += ["terrain_landscape_presentation_cpu", "terrain_landscape_first_use_review_helpers"]
        base.write(out / "protocol.json", {"runtime": base.render.runtime(), "inputs": base.identities(),
            "harness_sha256": base.render.digest(Path(base.__file__)), "driver_sha256": base.render.digest(Path(hard.__file__)),
            "support_inputs": hard.support_inputs(), "first_use_driver_sha256": base.render.digest(Path(__file__)),
            "legacy_inputs": legacy_inputs(), "required_tests": tests, "default_promoted": False,
            "browser_check_sha256": base.render.digest(Path(__file__).with_name("check_landscape_first_use_report.mjs")),
            "scope": "Headless-only first-use readiness; no solver/generation. Native GUI/promotion deferred."})
    else:
        verify(out)
        if args.phase in ("lifecycle", "negative"):
            getattr(base, args.phase)(out)
        elif args.phase == "compatibility":
            hard.compatibility(out)
        else:
            globals()[args.phase](out)
        verify(out)


if __name__ == "__main__":
    main()
