#!/usr/bin/env python3
"""Headless review of coherent wider world captures; no solver launch."""
import argparse
import html
import json
from pathlib import Path
import re
import sys

import review_captured_water_v1 as render
import study_world_wide_water_v1 as study
import run_mountain_rain_demo as owned

OLD = study.ROOT / "outputs/fluid/world-water-20261012-Ck824g"


def verify(out):
    study.verify(out)
    frozen = json.loads((out / "render-runtime.json").read_bytes())
    if frozen["runtime"] != render.runtime():
        raise ValueError("frozen renderer changed; retain old evidence")
    return frozen


def pose(sid):
    log = (OLD / "visual" / sid / "terrain-water.log").read_text()
    match = re.search(r"terrain_water_capture_camera: landscape target=\([^,]+,([^,]+),[^)]+\) span=(\S+)", log)
    if not match:
        raise ValueError("missing retained reference camera")
    return float(match[1]), float(match[2])


def command(out, sid, candidate, view="local", water=True, yaw=.25):
    p = study.bake.check_frozen(out / sid)
    center = next(iter(p["grids"].values()))["center_xz_m"]
    height, span = pose(sid)
    if view == "overview":
        span = 45000
    args = ["rtk", "proxy", str(render.APP), "--headless", "--terrain-water-capture-camera", "landscape",
            "--terrain-water-focus-x", str(center[0]),
            "--terrain-water-focus-z", str(center[1]),
            "--terrain-water-focus-height", str(height), "--terrain-water-span", str(span),
            "--terrain-water-yaw", str(yaw), "--terrain-water-pitch", "-0.4"]
    if candidate == "core-16m":
        capture = OLD / "visual" / sid / "capture/water-capture.json"
        args += ["--terrain-heightfield", p["source"], "--terrain-water-capture", str(capture),
                 "--terrain-water-context"]
    else:
        args += ["--terrain-site", p["terrain_site"]["manifest_path"], "--terrain-site-water",
                 str(out / sid / "captures" / candidate / "water-binding.json"),
                 "--terrain-water-vertex-budget", "4500000"]
    if not water:
        args += ["--no-terrain-water-visible"]
    return args


def input_identity(out, sid, candidate):
    folder = (OLD / "visual" / sid / "capture" if candidate == "core-16m" else
              out / sid / "captures" / candidate)
    return {p.name:render.digest(p) for p in sorted(folder.iterdir()) if p.is_file()}


def capture(out, sid, candidate):
    verify(out)
    identity = input_identity(out, sid, candidate)
    folder = out / sid / "visual" / candidate
    folder.mkdir(parents=True)
    receipts = []
    for view in ("local", "overview"):
        for water in (False, True):
            name = view + ("-water" if water else "-ground")
            image = folder / (name + ".png")
            args = command(out, sid, candidate, view, water) + ["--frames", "1", "--width", "1280",
                "--height", "720", "--terrain-water-audit", "--output", str(image)]
            receipt = render.run(args, folder / (name + ".log"))
            log = (folder / (name + ".log")).read_text()
            if ("terrain_water_capture_immutable: GPU bed/depth/decoded-velocity exact" not in log or
                    "NVIDIA GeForce RTX 5070 Ti" not in log or "VUID-" in log):
                raise ValueError("immutable field/device/validation receipt missing")
            receipt.update(image_sha256=render.digest(image), image=str(image.relative_to(out)))
            receipts.append(receipt)
    # Actual held-state surface animation; never labelled advancing hydraulics.
    video = folder / "surface-motion.mp4"
    args = command(out, sid, candidate) + ["--capture", "video", "--frames", "120", "--fps", "24",
        "--width", "1280", "--height", "720", "--terrain-water-audit", "--output", str(video)]
    receipts.append({**render.run(args, folder / "surface-motion.log"),
                     "video":str(video.relative_to(out)), "sha256":render.digest(video)})
    if input_identity(out, sid, candidate) != identity:
        raise ValueError("capture fields changed during visual review")
    study.bake.write(folder / "receipt.json", {"assets":receipts, "runtime":render.runtime(), "inputs":identity,
        "scope":"Same locked local camera as retained core; overview 45 km. Surface motion on saved fields."})
    verify(out)


def performance(out, sid, candidate):
    verify(out)
    identity = input_identity(out, sid, candidate)
    for leaf in out.glob("*/runs/*"):
        result = leaf / "result.json"
        receipt = json.loads(result.read_bytes()) if result.exists() else {}
        cleanup = receipt.get("cleanup", {})
        pid = cleanup.get("pid")
        if not pid or cleanup.get("active_group_remaining") != [] or owned.active_owned_group(pid):
            raise ValueError("owned hydraulic worker still active; no mixed workload measurements")
    folder = out / sid / "performance" / candidate
    folder.mkdir(parents=True)
    rows = []
    for view in ("local", "overview"):
        for repeat in range(2):
            runs = {}
            for water in (False, True):
                name = f'{view}-{repeat}-{"water" if water else "ground"}'
                prefix = folder / name
                args = command(out, sid, candidate, view, water) + ["--headless-render-only",
                    "--run-seconds", "12", "--render-fps", "60", "--width", "1920", "--height", "1080",
                    "--no-validation", "--profile-output", str(prefix), "--profile-warmup-frames", "120",
                    "--profile-sample-interval", "1"]
                runs["water" if water else "ground"] = {**render.run(args, prefix.with_suffix(".log")),
                                                         **render.timings(prefix)}
                log = prefix.with_suffix(".log").read_text()
                if ("NVIDIA GeForce RTX 5070 Ti" not in log or "headless_render_only_ready:" not in log or
                        "VUID-" in log):
                    raise ValueError("timing device/headless/validation receipt failed")
            wet, dry = runs["water"], runs["ground"]
            rows.append({"view":view, "repeat":repeat, "runs":runs,
                "whole_gpu_p95_ms":wet["whole_gpu_p95_ms"], "cpu_service_p95_ms":wet["cpu_service_p95_ms"],
                "water_increment_p95_difference_ms":wet["whole_gpu_p95_ms"]-dry["whole_gpu_p95_ms"],
                "pass":wet["whole_gpu_p95_ms"]<=8 and wet["cpu_service_p95_ms"]<=4})
            study.bake.write(folder / "results.json", {"rows":rows, "complete":len(rows)==4,
                "runtime":render.runtime(), "inputs":identity,
                "scope":"Render only, solver stopped; differences are not pass attribution"})
    if input_identity(out, sid, candidate) != identity:
        raise ValueError("capture fields changed during timing")
    verify(out)


def lifecycle(out, sid, candidate):
    verify(out)
    folder = out / sid / "visual" / candidate
    args = command(out, sid, candidate) + ["--headless-render-only", "--frames", "8", "--render-fps", "0",
        "--width", "640", "--height", "360", "--terrain-water-transition-smoke", "--terrain-water-audit"]
    receipt = render.run(args, folder / "lifecycle.log")
    log = (folder / "lifecycle.log").read_text()
    if "terrain_water_capture_immutable: GPU bed/depth/decoded-velocity exact" not in log or "VUID-" in log:
        raise ValueError("wider capture lifecycle field/validation failed")
    study.bake.write(folder / "lifecycle.json", receipt)
    verify(out)


def negative(out):
    verify(out)
    folder = out / "negative"
    folder.mkdir()
    sites = study.load_sites(study.WORLD_CATALOGUE)
    binding = out / "mountain/captures/wide-64m/water-binding.json"
    changed = json.loads(binding.read_bytes())
    changed["world_id"] = "0"*64
    study.bake.write(folder / "changed-world.json", changed)
    rows = []
    for name, sid, attachment in (("wrong-site", "mountain-interior", binding),
                                   ("wrong-world", "mountain", folder / "changed-world.json")):
        args = ["rtk", "proxy", str(render.APP), "--headless", "--frames", "1",
                "--terrain-site", str(study.ROOT / sites[sid]["terrain_site"]),
                "--terrain-site-water", str(attachment)]
        rows.append(render.run(args, folder / (name + ".log"), expected_exit=1))
        text = (folder / (name + ".log")).read_text()
        if "site-water binding source/site/world identity mismatch" not in text:
            raise ValueError("mismatched binding did not fail at its intended admission gate")
    study.bake.write(folder / "receipt.json", {"rows":rows, "scope":"Explicit admission failures; no fallback asset or GPU simulation"})
    verify(out)


def orbit(out, sid, candidate):
    verify(out)
    folder = out / sid / "orbit" / candidate
    folder.mkdir(parents=True)
    rows = []
    for i in range(8):
        args = command(out, sid, candidate, yaw=i*3.141592653589793/4) + ["--frames", "1",
            "--width", "960", "--height", "540", "--terrain-water-audit", "--output", str(folder / f"{i}.png")]
        rows.append(render.run(args, folder / f"{i}.log"))
    study.bake.write(folder / "receipt.json", {"rows":rows,
        "scope":"Eight fixed-centre headings, bounded local framing. No free-flight/horizon acceptance."})
    verify(out)


def complete_performance(result):
    rows = result["rows"]
    expected = {(view, repeat) for view in ("local", "overview") for repeat in range(2)}
    return (result.get("complete") is True and len(rows)==4 and
            {(r["view"],r["repeat"]) for r in rows}==expected and all(r["pass"] for r in rows))


def cache_reload(out):
    verify(out)
    folder = out / "cache-reload"
    folder.mkdir()
    rows = []
    for sid, candidate in (("mountain","wide-64m"),("mountain-interior","wide-64m"),
                            ("valley-interior","wide-64m"),("mountain","fallback-32m")):
        published = json.loads((out/sid/f"published-{candidate}.json").read_bytes())
        args = command(out,sid,candidate)
        args[args.index("--terrain-site-water")+1] = published["water_binding"]
        image = folder/f"{sid}-{candidate}.png"
        args += ["--frames","1","--width","1280","--height","720","--terrain-water-audit","--output",str(image)]
        receipt = render.run(args,image.with_suffix(".log"))
        difference = render.pixel_difference(image,out/sid/"visual"/candidate/"local-water.png")
        if difference["max_abs_8bit"]:
            raise ValueError("published cache reload changed pixels")
        rows.append({"site":sid,"candidate":candidate,"pixel_difference":difference,**receipt})
    study.bake.write(folder/"receipt.json", {"rows":rows,"all_exact":True})
    verify(out)


def report(out):
    verify(out)
    cards, bake_rows, perf_rows = [], [], []
    all_pass, slots = True, 0
    for sid, recipe in study.load_sites(study.WORLD_CATALOGUE).items():
        visual = out / sid / "visual/wide-64m/receipt.json"
        prefix = f"{sid}/visual/"
        images = (f'<div class="pair"><figure><img src="{prefix}core-16m/local-water.png" '
                  f'alt="Retained core water"><figcaption>16 m / 16 km water; ground beyond it</figcaption></figure>'
                  f'<figure><img src="{prefix}wide-64m/local-water.png" alt="Wider water">'
                  f'<figcaption>64 m / 131 km coherent captured water; same local camera</figcaption></figure></div>'
                  f'<figure><img src="{prefix}wide-64m/overview-water.png" alt="Wider overview">'
                  f'<figcaption>45 km overview framing: outer coverage remains finite</figcaption></figure>'
                  f'<video controls muted loop preload="metadata" src="{prefix}wide-64m/surface-motion.mp4"></video>'
                  '<p>Held bed/depth/velocity. This clip animates surface cues, not hydraulic time.</p>'
                  if visual.exists() else '<p>Full-resolution capture pending; no visual acceptance yet.</p>')
        cards.append(f'<section id="{sid}"><h2>{html.escape(recipe["label"])}</h2>{images}'
                     f'<details><summary>128 m pilot and ground control</summary><div class="pair">'
                     f'<img src="{prefix}pilot-128m/local-water.png" alt="Coarse pilot">'
                     f'<img src="{prefix}pilot-128m/overview-water.png" alt="Coarse overview">'
                     + (f'<img src="{prefix}wide-64m/local-ground.png" alt="Wider local ground control">'
                        f'<img src="{prefix}wide-64m/overview-ground.png" alt="Wider overview ground control">'
                        if visual.exists() else '') + '</div>'
                     '</details>'
                     + (f'<details><summary>Retained orbit stress views — not full-orbit visual acceptance</summary>'
                        f'<div class="pair"><img src="{sid}/orbit/wide-64m/0.png" alt="Orbit direction zero">'
                        f'<img src="{sid}/orbit/wide-64m/4.png" alt="Sun-facing orbit direction"></div>'
                        '<p>Finite outer terrain remains exposed. Some sun-facing headings produce bright '
                        'reflective sheets on widespread shallow floodwater; primary-view success does not '
                        'qualify every heading or free flight.</p></details>'
                        if (out/sid/"orbit/wide-64m/receipt.json").exists() else '')
                     + '</section>')
        for path in sorted((out / sid / "runs").glob("*/result.json")):
            result = json.loads(path.read_bytes())
            milestone = result["milestones"][-1] if result["milestones"] else None
            if milestone:
                stats = milestone["stats"]
                status = result["status"] if "cleanup" in result else "running / incomplete"
                wall = f'{result["total_wall_s"]/60:.2f} min' if "total_wall_s" in result else "pending"
                bake_rows.append(f'<tr><td>{sid}</td><td>{path.parent.name}</td><td>{status}</td>'
                    f'<td>{milestone["physical_s"]/3600:.2f} h</td><td>{wall}</td>'
                    f'<td>{stats["water_volume_m3"]/1e9:.2f} billion m³</td><td>{stats["max_depth_m"]:.1f} m</td></tr>')
        perf = out / sid / "performance/wide-64m/results.json"
        if perf.exists():
            result = json.loads(perf.read_bytes())
            all_pass = all_pass and complete_performance(result)
            if result["runtime"] != render.runtime() or result["inputs"] != input_identity(out,sid,"wide-64m"):
                raise ValueError("timing runtime/input identity changed")
            for row in result["rows"]:
                slots += 1
                all_pass = all_pass and row["pass"]
                perf_rows.append(f'<tr><td>{sid} · {row["view"]} · {row["repeat"]}</td>'
                    f'<td>{row["whole_gpu_p95_ms"]:.3f}</td><td>{row["cpu_service_p95_ms"]:.3f}</td>'
                    f'<td>{row["water_increment_p95_difference_ms"]:.3f}</td>'
                    f'<td>{"pass" if row["pass"] else "measured limitation"}</td></tr>')
        else:
            all_pass = False
    fallback = out / "mountain/visual/fallback-32m/receipt.json"
    if fallback.exists():
        cards.append('<section><h2>Finer, smaller-coverage alternative</h2><div class="pair">'
            '<img src="mountain/visual/wide-64m/local-water.png" alt="64m full coverage">'
            '<img src="mountain/visual/fallback-32m/local-water.png" alt="32m middle coverage"></div>'
            '<p>64 m / 131 km versus 32 m / 65 km. Both resolution and catchment extent change; '
            'this is not a resolution-only accuracy comparison or a spliced LOD simulation.</p></section>')
        path = out/"mountain/performance/fallback-32m/results.json"
        if path.exists():
            result = json.loads(path.read_bytes())
            if result["runtime"] != render.runtime() or result["inputs"] != input_identity(out,"mountain","fallback-32m"):
                raise ValueError("fallback timing runtime/input identity changed")
            cards.append('<section><h2>Finer alternative render costs</h2><div class="scroll"><table>'
                '<tr><th>View/repeat</th><th>GPU p95 ms</th><th>CPU service p95 ms</th><th>Target</th></tr>'
                + ''.join(f'<tr><td>{r["view"]} · {r["repeat"]}</td><td>{r["whole_gpu_p95_ms"]:.3f}</td>'
                          f'<td>{r["cpu_service_p95_ms"]:.3f}</td><td>{"pass" if r["pass"] else "measured limitation"}</td></tr>'
                          for r in result["rows"])
                + '</table></div><p>Additional matched water/ground controls, separate from the twelve full-coverage rows.</p></section>')
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>World water — wider coverage</title><style>body{margin:0;background:#15212b;color:#e8eef2;font:16px/1.6 system-ui}main{max-width:1280px;margin:auto;padding:24px}a{color:#9dd9e7}.pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}img,video{width:100%;display:block}figure{margin:16px 0}section{margin:40px 0}figcaption{color:#b7c9d3}th,td{text-align:left;padding:8px;border-bottom:1px solid #485763}.scroll{overflow:auto}@media(max-width:760px){.pair{grid-template-columns:1fr}}</style><main><a href="/">All reports</a><h1>Wider water on the prepared world</h1>
<p>Existing SynxFlow bake + solver-neutral export + shared fixed paired Terrain/water renderer. No solver port, new terrain, live-core splice or default promotion.</p>
<p>The earlier wider-water approach now accepts the three pinned world sites. Nominal 131.072 km water coverage uses 1k/128 m pilots and 2k/64 m confirmations. Actual sampled spans are 130.944 and 131.008 km. The draw mesh remains at most 64 m outside its fine region, with an explicit 4.5M vertex budget.</p>
<p><strong>How to read:</strong> comparisons lock the original local camera. Wider water removes the inner 16 km cut, not the finite outer terrain/water boundary. The numerical bed is sampled at the solver spacing; finer draw geometry cannot recover sub-grid terrain or stream detail. Each site is an independent coherent saved state, not one shared global water solution.</p>
<p>Dry start, continuous 512 mm/h rain, Manning 0.05, open perimeter, twelve physical hours. This is exaggerated impervious-terrain flooding, not ordinary rainfall, equilibrium or permanent lakes. Very deep localized basins and broad sheet flooding are retained rather than hidden.</p>'''
    page += '<nav>'+ ' · '.join(f'<a href="#{sid}">{html.escape(r["label"])}</a>' for sid,r in study.load_sites(study.WORLD_CATALOGUE).items())+'</nav>'
    page += ''.join(cards)
    page += '<section><h2>Observed bake results</h2><div class="scroll"><table><tr><th>Site</th><th>Grid</th><th>Status</th><th>Physical time</th><th>Observed wall</th><th>Stored water</th><th>Maximum depth</th></tr>'+''.join(bake_rows)+'</table></div><p>Wall time includes startup/teardown and concurrent correctness/capture/preparation work. Workers request a 300× pace cap; these are not isolated maximum-throughput or live-render benchmarks.</p></section>'
    page += f'<section><h2>Render-only costs</h2><p>{slots}/12 required water-on rows available. Qualification: {"all proposed targets pass" if slots==12 and all_pass else "pending or measured limitation"}. Each row has a separate ground-only control; two repeats per view at 1080p/60 Hz, 120 warmup frames excluded. Solver stopped, no encoding/readback. Targets: scene GPU p95 ≤8 ms, CPU service p95 ≤4 ms.</p><div class="scroll"><table><tr><th>Site/view/repeat</th><th>GPU p95 ms</th><th>CPU service p95 ms</th><th>Water/ground p95 difference</th><th>Target</th></tr>'+''.join(perf_rows)+'</table></div><p>Independent percentile differences are not GPU pass attribution or monitor FPS. Peak heap/RSS, exact commands and timing CSVs remain in the per-site receipts.</p></section>'
    audit_link = '<a href="field-audit.json">Final field audit</a>' if (out/"field-audit.json").exists() else 'Final field audit pending'
    page += '<p><a href="study.json">Pinned recipes</a> · <a href="geometry.json">CPU topology preflight</a> · <a href="render-runtime.json">Frozen renderer</a> · <a href="regression-tests.xml">31 regressions</a> · '+audit_link+' · <a href="negative/receipt.json">Rejected mismatched bindings</a>. Native GUI and human/default acceptance remain separate. Current live Fluid demo is unchanged.</p></main></html>'
    (out / "index.html").write_text(page)
    study.bake.write(out / "report-receipt.json", {"page_sha256":render.digest(out / "index.html"),
        "runtime":render.runtime(), "performance_rows":slots, "all_required_perf_pass":slots==12 and all_pass,
        "native_gui_accepted":False, "defaults_promoted":False})
    verify(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("freeze", "capture", "performance", "lifecycle", "negative", "orbit", "report", "cache-reload"))
    parser.add_argument("out", type=Path)
    parser.add_argument("--site", choices=tuple(study.load_sites(study.WORLD_CATALOGUE)), default="mountain")
    parser.add_argument("--candidate", choices=tuple(study.CANDIDATES)+("core-16m",), default="pilot-128m")
    args = parser.parse_args()
    out = study.owned_root(args.out)
    if args.phase == "freeze":
        study.verify(out)
        if (out / "render-runtime.json").exists():
            raise ValueError("renderer already frozen")
        study.bake.write(out / "render-runtime.json", {"runtime":render.runtime(),
            "scope":"Frozen render runtime only; no solver/native performance claim"})
    elif args.phase == "capture":
        capture(out, args.site, args.candidate)
    elif args.phase == "lifecycle":
        lifecycle(out, args.site, args.candidate)
    elif args.phase == "negative":
        negative(out)
    elif args.phase == "orbit":
        orbit(out, args.site, args.candidate)
    elif args.phase == "report":
        report(out)
    elif args.phase == "cache-reload":
        cache_reload(out)
    else:
        performance(out, args.site, args.candidate)


if __name__ == "__main__":
    main()
