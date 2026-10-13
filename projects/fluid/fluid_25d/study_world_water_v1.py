#!/usr/bin/env python3
"""Bounded prepared-world rainfall integration; no generation/install/GUI.

Reuses case preparation, native snapshots, managed controls and water export.
Run phases explicitly. Old cases/captures and prepared worlds stay immutable.
"""
import argparse
import html
import json
from pathlib import Path
import subprocess
import shutil
import sys

import run_terrain_rain_demo as terrain
from terrain_water_sites import WORLD_CATALOGUE, load_sites
sha256 = terrain.demo.ref.sha256_file

ROOT = terrain.demo.ref.ROOT
TERRAIN_APP = ROOT / "build/release/projects/terrain/terrain"
EXPORT_APP = ROOT / "build/release/projects/fluid/fluid_25d/cubey_project_fluid_25d_export_water_capture"


def write(path, doc):
    path.write_text(json.dumps(doc, indent=2, sort_keys=True, allow_nan=False) + "\n")


def protected_inputs():
    paths = [WORLD_CATALOGUE, terrain.DEFAULT_CATALOGUE, terrain.demo.EXTENSION,
             ROOT / "cache/terrain/water/v1/mountain-rain-fed-1024-16m/water-capture.json"]
    for recipe in load_sites(WORLD_CATALOGUE).values():
        folder = (ROOT / recipe["manifest"]).parent
        paths += [p for p in folder.iterdir() if p.is_file() and p.suffix in (".json", ".f32")]
    return {str(p.resolve()): sha256(p) for p in paths}


def runtime():
    paths = [terrain.RELEASE_APP, TERRAIN_APP, EXPORT_APP]
    for app in (terrain.RELEASE_APP, TERRAIN_APP):
        paths += sorted((app.parent / "shaders").glob("*.spv"))
    return {str(p): sha256(p) for p in paths}


def verify(out):
    frozen = json.loads((out / "frozen.json").read_bytes())
    if frozen["protected_inputs"] != protected_inputs() or frozen["runtime"] != runtime():
        raise ValueError("frozen sources/runtime changed; retain evidence, do not mix versions")
    return frozen


def capture(out):
    import survey_terrain_sites_v1 as survey
    from run_offscreen_perf_v2 import environment
    for sid in load_sites(WORLD_CATALOGUE):
        name = f"{sid}-patch-1024-16m"
        case, leaf = out / "cases" / name, out / "runs" / name
        survey.seal_recording(case, leaf)
        recording = leaf / "recording/recording.json"
        spec = terrain.case_identity(case)["spec"]
        result = json.loads((leaf / "result.json").read_bytes())
        if not result["reached_target"]:
            raise ValueError("cannot accept an incomplete hydraulic run")
        target = out / "visual" / sid
        target.mkdir(parents=True)
        commands = []
        # Existing helper produces audited overview/flow PNGs and formation clips.
        survey.capture(out, "patch-1024-16m", [sid])
        export = ["rtk", "proxy", str(EXPORT_APP), str(recording),
                  spec["terrain_source_identity"]["manifest_path"], str(target / "capture")]
        with (target / "export.log").open("x") as log:
            subprocess.run(export, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
        commands.append(export)
        base = ["rtk", "proxy", str(TERRAIN_APP), "--headless", "--frames", "1",
                "--width", "1280", "--height", "720", "--terrain-heightfield",
                spec["terrain_source_identity"]["manifest_path"], "--terrain-water-capture",
                str(target / "capture/water-capture.json"), "--terrain-water-capture-camera",
                "landscape", "--terrain-water-context"]
        for ground in (False, True):
            command = base + (["--no-terrain-water-visible"] if ground else []) + [
                "--output", str(target / ("terrain-ground.png" if ground else "terrain-water.png"))]
            survey.run_capture(command, target / ("terrain-ground.log" if ground else "terrain-water.log"),
                               environment("offscreen"))
            commands.append(command)
        # Explicit same-state, same-framing context-off control.
        command = terrain.viewer_command(case, recording, "World context control", live=False,
            start=result["last_physical_s"], app=terrain.RELEASE_APP, width=1280, height=720)
        command += ["--headless", "--fluid25d-native-terrain-context", "off",
                    "--fluid25d-recording-gpu-validation", "--output", str(target / "fluid-context-off.png")]
        survey.run_capture(command, target / "fluid-context-off.log", environment("offscreen"))
        commands.append(command)
        write(target / "receipt.json", {"commands": commands,
            "artifacts": {str(p.relative_to(target)): sha256(p) for p in target.rglob("*") if p.is_file()},
            "capture_time_s": result["last_physical_s"], "native_gui_review": False})
    survey.capture_motion(out, WORLD_CATALOGUE)


def lifecycle(out):
    entries = terrain.catalogue(out, load_sites(WORLD_CATALOGUE))
    if len(entries) != 15 or not all(e["available"] for e in entries):
        raise ValueError("all fifteen world cases must be admitted")
    actions = []
    for e in entries:
        actions += [{"kind": "switch", "case_id": e["id"], "after_s": .1,
                     "rain_mm_per_hour": 512, "pacing": 60},
                    {"kind": "resume", "after_s": .1}, {"kind": "pause", "after_s": .2}]
    actions += [{"kind": "switch", "case_id": "mountain-patch-1024-16m", "after_s": .1,
                 "rain_mm_per_hour": 512, "pacing": 60}]
    actions += [{"kind": "reset", "after_s": .1} for _ in range(5)]
    write(out / "switch-actions.json", {"schema": terrain.managed.SCHEMA, "actions": actions})
    command = ["rtk", "proxy", sys.executable, str(terrain.HERE / "validate_terrain_demo_v1.py"),
               "--study", str(out), "--out", str(out / "lifecycle"), "--script", str(out / "switch-actions.json"),
               "--seconds", "600", "--site-catalogue", str(WORLD_CATALOGUE)]
    with (out / "lifecycle-controller.log").open("x") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=800)


def performance(out):
    """Serial isolated runs; never overlap pilots, regression GPUs or encoders."""
    target = out / "performance"
    target.mkdir()
    for sid in load_sites(WORLD_CATALOGUE):
        name = f"{sid}-patch-1024-16m"
        recording = out / "runs" / name / "recording/recording.json"
        if not recording.is_file():
            raise ValueError("developed-state controls need completed confirmations")
        # Held-state plus concurrent live: separate costs, same accepted presentation.
        command = ["rtk", "proxy", sys.executable, str(terrain.HERE / "run_offscreen_perf_v2.py"),
                   "--study", str(out), "--case", name, "--out", str(target / sid),
                   "--host", "offscreen", "--modes", "renderer-only", "live", "--recording", str(recording),
                   "--wall", "90", "--warmup", "20", "--prewarm", "43200", "--prewarm-timeout", "1200",
                   "--repeats", "2", "--render-fps", "60", "--surface", "correlated", "--shadow-projection", "scene"]
        with (target / f"{sid}.log").open("x") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=3000)
    # Most demanding site's measured combined GPU p95 chooses the soak, not aesthetics.
    results = {sid: json.loads((target / sid / "results.json").read_bytes())["trials"]
               for sid in load_sites(WORLD_CATALOGUE)}
    def cost(sid):
        return max(r["profile"]["passes_ms"]["gpu:fluid_25d native presentation total"]["p95"]
                   for r in results[sid] if r["mode"] == "live")
    worst = max(results, key=cost)
    command = ["rtk", "proxy", sys.executable, str(terrain.HERE / "run_offscreen_perf_v2.py"),
               "--study", str(out), "--case", f"{worst}-patch-1024-16m", "--out", str(target / "soak"),
               "--host", "offscreen", "--modes", "live", "--wall", "600", "--warmup", "20",
               "--prewarm", "21600", "--prewarm-timeout", "1200", "--repeats", "1", "--render-fps", "60",
               "--surface", "correlated", "--shadow-projection", "scene"]
    with (target / "soak.log").open("x") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=1900)
    write(target / "soak-selection.json", {"site":worst, "policy":"maximum repeated combined GPU p95"})


def interrupted_loading(out, attempt=0):
    actions = {"schema":terrain.managed.SCHEMA, "actions":[
        {"kind":"switch", "case_id":"mountain-patch-2048-8m", "after_s":.1,
         "rain_mm_per_hour":512, "pacing":60}]}
    script = out / "interrupt-actions.json"
    if not script.exists():
        write(script, actions)
    elif json.loads(script.read_bytes()) != actions:
        raise ValueError("interruption script changed")
    suffix = f"-repeat-{attempt}" if attempt else ""
    target = out / ("interrupted-loading" + suffix)
    command = ["rtk", "proxy", sys.executable, str(terrain.HERE / "validate_terrain_demo_v1.py"),
               "--study", str(out), "--out", str(target), "--script", str(out / "interrupt-actions.json"),
               "--seconds", "2", "--warmup", "0", "--site-catalogue", str(WORLD_CATALOGUE)]
    with (out / f"interrupt-controller{suffix}.log").open("x") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=200)
    receipt = json.loads((target / "validation.json").read_bytes())
    raw = (target / "trial/viewer.log").read_text(errors="replace")
    if (result.returncode == 0 or "before all actions were acknowledged" not in raw.lower() or
            any(c.get("active_group_remaining") != [] for c in receipt["cleanup"]) or
            "VUID-" in raw or "Validation Error" in raw):
        raise ValueError("interrupted-loading expected failure/clean shutdown not demonstrated")
    write(out / "interrupted-loading-receipt.json", {"expected_incomplete_attempt":True,
          "clean_owned_shutdown":True, "not_a_successful_switch":True, "command":command})


def publish_captures(out):
    """Publish opt-in content-identified neutral assets; never overwrite a bundle."""
    assets = []
    for sid, recipe in load_sites(WORLD_CATALOGUE).items():
        source = out / "visual" / sid / "capture"
        manifest = source / "water-capture.json"
        identity = sha256(manifest)
        destination = ROOT / "cache/terrain/water/v1/world-sites" / f"{sid}-{identity[:16]}"
        expected = {p.name:sha256(p) for p in source.iterdir() if p.is_file()}
        if destination.exists():
            if destination.is_symlink() or {p.name:sha256(p) for p in destination.iterdir() if p.is_file()} != expected:
                raise ValueError("published captured-water bundle changed")
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(source, destination)
        assets.append({"site":sid, "manifest":str(destination / "water-capture.json"),
                       "sha256":identity, "heightfield":str(ROOT / recipe["manifest"]),
                       "opt_in":True, "finite_water_coverage":True})
    write(out / "consumer-assets.json", {"assets":assets})


def report(out):
    frozen = verify(out)
    if len(json.loads((out / "field-audit.json").read_bytes())["cases"]) != 6:
        raise ValueError("all three pilots and confirmations require field audits")
    if json.loads((out / "lifecycle/validation.json").read_bytes())["errors"]:
        raise ValueError("managed lifecycle validation failed")
    from review_offscreen_perf_v2 import measure
    performance_rows = []
    gates = []
    for path in sorted((out / "performance").glob("*/results.json")):
        for r in json.loads(path.read_bytes())["trials"]:
            m = measure(r)
            passed = (r["status"] == "completed" and m["gpu_p95_ms"] is not None and
                      m["gpu_p95_ms"] <= 8 and m["service_p95_ms"] is not None and m["service_p95_ms"] <= 4)
            if r["mode"] == "live":
                passed = passed and m["pace_x"] >= 54 and m["advancing_age_p95_s"] <= 1
            gates.append({"trial":str(path.parent.relative_to(out))+f'/{r["mode"]}-{r["repeat"]}',
                          "passed":passed, "measures":m})
            fmt = lambda value: "—" if value is None else f"{value:.3f}"
            performance_rows.append(f'<tr><td>{html.escape(path.parent.name)} · {r["mode"]} · {r["repeat"]}</td>'
                                    f'<td>{fmt(m["pace_x"])}</td><td>{fmt(m["gpu_p95_ms"])}</td>'
                                    f'<td>{fmt(m["service_p95_ms"])}</td><td>{fmt(m["advancing_age_p95_s"])}</td>'
                                    f'<td>{"pass" if passed else "measured limitation"}</td></tr>')
    write(out / "performance-gates.json", {"trials":gates, "all_pass":len(gates)==13 and all(g["passed"] for g in gates),
          "scope":"Headless Vulkan, no readback/encoding; live CUDA work shares GPU. Not displayed monitor FPS."})
    rows, cards = [], []
    for sid, recipe in load_sites(WORLD_CATALOGUE).items():
        name = f"{sid}-patch-1024-16m"
        result = json.loads((out / "runs" / name / "result.json").read_bytes())
        m = result["final"]
        rows.append(f'<tr><td>{html.escape(recipe["label"])}</td><td>{result["last_physical_s"]/3600:.2f} h</td>'
                    f'<td>{m["water_volume_m3"]/1e6:.2f} million m³</td><td>{m["wet_speed_p95_m_per_s"]:.2f} m/s</td></tr>')
        cards.append(f'<section><h2>{html.escape(recipe["label"])}</h2><div class="pair">'
                     f'<figure><img src="visual/{sid}/terrain-ground.png"><figcaption>Same paired terrain; water hidden</figcaption></figure>'
                     f'<figure><img src="visual/{sid}/terrain-water.png"><figcaption>Rain-on capture + matching wider ground</figcaption></figure></div>'
                     f'<p>Live-region coverage is 16.384 km square. The wider ground is not hydraulically modelled.</p>'
                     f'<div class="pair"><figure><video controls muted loop src="runs/{name}/overview.mp4"></video><figcaption>12-hour formation compressed; actual recorded native fields</figcaption></figure>'
                     f'<figure><video controls muted loop src="runs/{name}/late-flow.mp4"></video><figcaption>Late flow at 12×; foam/trails are render cues</figcaption></figure></div>'
                     f'<details><summary>Domain and flow diagnostics</summary><div class="pair"><img src="runs/{name}/flow.png">'
                     f'<img src="visual/{sid}/fluid-context-off.png"></div></details></section>')
    page = '<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>World Terrain Water V1</title>'
    page += '<style>body{background:#17212a;color:#e4edf2;font:16px/1.6 system-ui;margin:0}main{max-width:1280px;margin:auto;padding:24px}.pair{display:grid;grid-template-columns:1fr 1fr;gap:16px}figure{margin:0}img,video{width:100%;display:block}section{margin:36px 0}a{color:#94d2e8}th,td{text-align:left;padding:8px;border-bottom:1px solid #45505c}@media(max-width:760px){.pair{grid-template-columns:1fr}}</style><main><a href="/">All reports</a><h1>World Terrain Water V1</h1>'
    page += '<p>One shared prepared world, three immutable sites, the existing external solver and shared Terrain/Fluid renderer. Uniform 512 mm/h demonstration rain, dry start, open perimeter; not ordinary rainfall or calibrated hydrology.</p>'
    page += '<p>The atlas locates sites, native 30 m terrain supplies elevations, the default 1k/16 m grid simulates only the central region, and exterior terrain supplies context. Captured views animate appearance, not hydraulic state. No terrain generation, solver port, GUI launch or default replacement.</p>'
    page += '<p><a href="/terrain/readability-20261012-a1/">Shared world atlas and approved material study</a>. The existing six-site collection remains the default; this is a separate prepared-world collection.</p>'
    page += '<p><strong>Important boundary:</strong> wider ground does not continue the water. Pools and streams meeting the simulation perimeter are truncated observations, not complete lakes or evidence that the exterior is dry. The valley case deliberately retains this limitation instead of hiding its wet rectangle.</p>'
    page += '<table><tr><th>Site</th><th>Captured time</th><th>Stored water</th><th>Wet speed P95</th></tr>'+''.join(rows)+'</table>'+''.join(cards)
    page += '<section><h2>Current live and held-state costs</h2><p>Two isolated 90-second repeats per site, 1080p/60Hz, 20s warmup excluded. Live cases are preconditioned to twelve physical rain-on hours. A separate ten-minute live soak begins at six hours to remain inside the finite twenty-hour worker horizon. Held states and live states are not an isolated bridge-overhead comparison.</p><div style="overflow:auto"><table><tr><th>Trial</th><th>Physical/wall ×</th><th>GPU p95 ms</th><th>CPU service p95 ms</th><th>Advancing-state age p95 s</th><th>Target</th></tr>'+''.join(performance_rows)+'</table></div><p>Targets: GPU ≤8ms, CPU service ≤4ms, live pace ≥54×, advancing-state age ≤1s. Paced loop intervals include deliberate waits and are reported in raw receipts. <a href="performance-gates.json">All measured repeats</a>.</p></section>'
    page += '<p><a href="field-audit.json">Field sanity audit</a> · <a href="frozen.json">Frozen inputs/runtime</a> · <a href="lifecycle/validation.json">Selection/reset checks</a>. Native GUI and human acceptance remain separate.</p></main>'
    (out / "index.html").write_text(page)
    write(out / "report-receipt.json", {"page_sha256": sha256(out / "index.html"), "runtime": frozen["runtime"]})


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("phase", choices=("freeze", "pilot", "confirm", "audit", "capture", "lifecycle", "interrupt", "performance", "publish", "report"))
    p.add_argument("out", type=Path)
    p.add_argument("--attempt", type=int, choices=(0, 1, 2), default=0,
                   help="explicit fresh interrupted-loading repeat; previous attempts retained")
    a = p.parse_args()
    out = a.out.resolve(strict=True)
    if out.parent != terrain.demo.ref.OUTPUT_ROOT.resolve() or not out.name.startswith("world-water-") or a.out.is_symlink():
        p.error("existing fresh owned outputs/fluid/world-water-* root required")
    if a.phase == "freeze":
        if (out / "frozen.json").exists():
            raise ValueError("freeze already exists")
        write(out / "frozen.json", {"protected_inputs": protected_inputs(), "runtime": runtime(),
            "forcing": {"rain_mm_per_hour":512, "manning_n":.05, "boundary":"fall", "dry_start":True},
            "scope":"No source mutation, generator, solver change, GUI or push"})
        return
    verify(out)
    if a.phase in ("pilot", "confirm"):
        import survey_terrain_sites_v1 as survey
        survey.run(out, WORLD_CATALOGUE, "all", "patch-512-32m" if a.phase == "pilot" else "patch-1024-16m",
                   21600 if a.phase == "pilot" else 43200, 900 if a.phase == "pilot" else 1800)
    elif a.phase == "audit":
        import survey_terrain_sites_v1 as survey
        survey.audit(out)
    elif a.phase == "capture":
        capture(out)
    elif a.phase == "lifecycle":
        lifecycle(out)
    elif a.phase == "performance":
        performance(out)
    elif a.phase == "interrupt":
        interrupted_loading(out, a.attempt)
    elif a.phase == "publish":
        publish_captures(out)
    else:
        report(out)
    verify(out)


if __name__ == "__main__":
    main()
