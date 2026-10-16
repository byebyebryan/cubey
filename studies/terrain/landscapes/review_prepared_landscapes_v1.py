#!/usr/bin/env python3
"""Bounded headless qualification of prepared landscape selection; no GUI/solver."""
import argparse
import html
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET
import review_captured_water_v1 as render
import review_world_wide_water_v1 as previous
import prepare_terrain_landscapes as prepare

ROOT = render.ROOT
CATALOG = ROOT / "projects/terrain/terrain_landscape_presets.json"
OLD = ROOT / "outputs/terrain/wider-water-world-20261012-gqeluh"
WET = tuple(prepare.WATER_LOCATIONS)
DRY = ("mountain-dry", "mountain-interior-dry", "valley-interior-dry")


def write(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def identities():
    paths = {CATALOG}
    for p in json.loads(CATALOG.read_bytes())["presets"]:
        folder = ROOT / "cache/terrain" / Path(p["site"]["path"]).parent
        paths.update(x for x in folder.rglob("*") if x.is_file())
        if p["water"]:
            folder = ROOT / "cache/terrain" / Path(p["water"]["path"]).parent
            paths.update(x for x in folder.iterdir() if x.is_file())
    return {str(p.relative_to(ROOT)): render.digest(p) for p in sorted(paths)}


def verify(out):
    frozen = json.loads((out / "protocol.json").read_bytes())
    if frozen["runtime"] != render.runtime() or frozen["inputs"] != identities():
        raise ValueError("frozen assets/runtime changed; retain old evidence")
    if frozen["harness_sha256"] != render.digest(Path(__file__)):
        raise ValueError("review harness changed")
    return frozen


def command(id, wide=False, water=True, locked=False):
    args = ["rtk", "proxy", str(render.APP), "--headless", "--terrain-landscape", id]
    if wide:
        args += ["--terrain-water-span", "45000"]
    if not water:
        args += ["--no-terrain-water-visible"]
    if locked:
        sid, candidate = prepare.WATER_LOCATIONS[id]
        height, span = previous.pose(sid)
        args += ["--terrain-water-focus-height", str(height), "--terrain-water-yaw", ".25",
                 "--terrain-water-pitch", "-.4", "--terrain-water-span", "45000" if wide else str(span)]
    return args


def checked(args, log, expected_exit=0):
    receipt = render.run(args, log, expected_exit)
    text = log.read_text()
    if "VUID-" in text or "Validation Error" in text:
        raise ValueError("Vulkan validation failed")
    if expected_exit == 0 and "NVIDIA GeForce RTX 5070 Ti" not in text:
        raise ValueError("hardware Vulkan evidence missing")
    return receipt


def capture(out):
    verify(out)
    rows = []
    for id in (*WET, *DRY):
        for view in ("local", "wide"):
            image = out / f"{id}-{view}.png"
            receipt = checked(command(id, view == "wide") + ["--frames", "1", "--width", "1280",
                "--height", "720", "--terrain-water-audit", "--output", str(image)], image.with_suffix(".log"))
            rows.append({"id": id, "view": view, "image": image.name, **receipt})
            print("captured", id, view, flush=True)
    for id in WET:
        image = out / f"{id}-hidden.png"
        rows.append({"id": id, "view": "hidden", "image": image.name, **checked(
            command(id, water=False) + ["--frames", "1", "--width", "1280", "--height", "720",
            "--output", str(image)], image.with_suffix(".log"))})
    # Exact old framing distinguishes feature integration from appearance changes.
    parity = []
    for id in WET:
        sid, candidate = prepare.WATER_LOCATIONS[id]
        image = out / f"{id}-parity.png"
        receipt = checked(command(id, locked=True) + ["--frames", "1", "--width", "1280",
            "--height", "720", "--output", str(image)], image.with_suffix(".log"))
        ref = OLD / sid / "visual" / candidate / "local-water.png"
        diff = render.pixel_difference(ref, image)
        if diff["max_abs_8bit"]:
            raise ValueError(f"legacy frame changed: {id}: {diff}")
        parity.append({"id": id, "reference": str(ref.relative_to(ROOT)), "difference": diff, **receipt})
    # Surface motion on held fields, not evolving hydraulics.
    clip = out / "mountain-local-motion.mp4"
    video = checked(command("mountain-local") + ["--capture", "video", "--frames", "120",
        "--fps", "24", "--width", "1280", "--height", "720", "--output", str(clip)], out / "motion.log")
    write(out / "capture.json", {"rows": rows, "parity": parity, "video": video})
    verify(out)


def lifecycle(out):
    verify(out)
    sequence = "mountain-wide,mountain-interior-wide,valley-interior-wide,mountain-dry,!unknown,mountain-local,~mountain-wide"
    args = command("mountain-local") + ["--headless-render-only", "--width", "640", "--height", "360",
        "--render-fps", "0", "--frames", "7", "--terrain-water-audit", "--terrain-landscape-smoke-sequence", sequence]
    receipt = checked(args, out / "lifecycle.log")
    text = (out / "lifecycle.log").read_text()
    if len(re.findall("terrain_landscape_applied:", text)) != 5 or "terrain_landscape_rejected:" not in text or "terrain_landscape_target_rebuild:" not in text:
        raise ValueError("incomplete lifecycle transitions")
    if len(re.findall("terrain_water_capture_immutable: GPU", text)) < 7:
        raise ValueError("missing immutable-field checks")
    applies = [{"id": id, "prepare_ms": float(cpu), "install_ms": float(gpu)} for id, cpu, gpu in re.findall(
        r"terrain_landscape_applied: id=(\S+) prepare_ms=(\S+) install_ms=(\S+)", text)]
    failure = checked(command("mountain-dry") + ["--headless-render-only", "--width", "640", "--height", "360",
        "--frames", "1", "--terrain-water-vertex-budget", "2000000", "--terrain-water-audit",
        "--terrain-landscape-smoke-sequence", "!mountain-local"], out / "failed-prepare.log")
    if "showing=mountain-dry reason=fixed topology exceeds vertex budget" not in (out / "failed-prepare.log").read_text():
        raise ValueError("failed preparation did not preserve old scene")
    toggles = checked(command("mountain-wide") + ["--headless-render-only", "--width", "640", "--height", "360",
        "--frames", "8", "--render-fps", "0", "--terrain-water-audit", "--terrain-water-transition-smoke"], out / "toggles.log")
    asynchronous = checked(command("mountain-local") + ["--headless-render-only", "--width", "640", "--height", "360",
        "--run-seconds", "6", "--render-fps", "60", "--terrain-water-audit", "--terrain-landscape-smoke-sequence", "+mountain-wide"], out / "asynchronous-apply.log")
    asynchronous_text = (out / "asynchronous-apply.log").read_text()
    if "terrain_landscape_applied: id=mountain-wide" not in asynchronous_text:
        raise ValueError("asynchronous Apply never installed")
    heap = [float(value) for value in re.findall(r"observed_install_heap_mib=(\S+)", text + asynchronous_text)]
    if len(heap) != 6 or not all(value > 0 for value in heap):
        raise ValueError("GPU install-overlap observations missing")
    write(out / "lifecycle.json", {"sequence": receipt, "applies": applies,
          "failed_prepare_kept_dry": failure, "water_context_backdrop_toggles": toggles,
          "asynchronous_apply": asynchronous, "observed_install_heap_mib_max": max(heap),
          "scope": "Headless same Apply path; target extent rebuild, not native-window resize acceptance"})
    verify(out)


def negative(out):
    verify(out)
    rows = []
    for name, args, reason in (
        ("unknown", command("unknown"), "unknown terrain landscape preset"),
        ("missing", command("mountain-local") + ["--terrain-landscape-cache-root", str(out / "absent-cache")], "not installed"),
        ("source-conflict", command("mountain-local") + ["--terrain-heightfield", "manual.json"], "owns its site/source"),
        ("gui-smoke", ["rtk", "proxy", str(render.APP), "--terrain-landscape-smoke-sequence", "mountain-local"], "requires headless")):
        receipt = checked(args, out / f"negative-{name}.log", 1)
        if reason not in (out / f"negative-{name}.log").read_text():
            raise ValueError("wrong failure gate")
        rows.append({"case": name, **receipt})
    # auto is an explicit candidate; canonical startup remains legacy.
    image = out / "auto.png"
    automatic = checked(command("auto") + ["--frames", "1", "--width", "1280", "--height", "720",
        "--output", str(image)], out / "auto.log")
    diff = render.pixel_difference(image, out / "mountain-local-local.png")
    if diff["max_abs_8bit"]:
        raise ValueError("auto differs from proposed default")
    write(out / "negative.json", {"failures": rows, "auto": automatic, "auto_parity": diff})
    verify(out)


def performance(out):
    verify(out)
    rows = []
    for id in WET:
        for repeat in range(2):
            prefix = out / f"performance-{id}-{repeat}"
            receipt = checked(command(id) + ["--headless-render-only", "--run-seconds", "12",
                "--render-fps", "60", "--width", "1920", "--height", "1080", "--no-validation",
                "--profile-output", str(prefix), "--profile-warmup-frames", "120",
                "--profile-sample-interval", "1"], prefix.with_suffix(".log"))
            timing = render.timings(prefix)
            rows.append({"id": id, "repeat": repeat, **receipt, **timing,
                "pass": timing["whole_gpu_p95_ms"] <= 8 and timing["cpu_service_p95_ms"] <= 4})
            print("timed", id, repeat, timing["whole_gpu_p95_ms"], flush=True)
    write(out / "performance.json", {"rows": rows, "scope": "1080p headless Vulkan, 120 warmup frames, solver stopped; no encoding/readback"})
    verify(out)


def report(out):
    verify(out)
    capture = json.loads((out / "capture.json").read_bytes())
    performance = json.loads((out / "performance.json").read_bytes())
    life = json.loads((out / "lifecycle.json").read_bytes())
    negative = json.loads((out / "negative.json").read_bytes())
    expected = {(id, repeat) for id in WET for repeat in range(2)}
    rows = performance["rows"]
    if len(rows) != len(expected) or {(r["id"], r["repeat"]) for r in rows} != expected or not all(r["pass"] for r in rows):
        raise ValueError("incomplete or failed performance qualification")
    required_views = {(id, view) for id in (*WET, *DRY) for view in ("local", "wide")} | {(id, "hidden") for id in WET}
    if (len(capture["rows"]) != len(required_views) or
        {(r["id"],r["view"]) for r in capture["rows"]} != required_views or
        len(capture["parity"]) != 4 or {r["id"] for r in capture["parity"]} != set(WET) or
        len(negative["failures"]) != 4):
        raise ValueError("incomplete visual/negative receipts")
    tests = ET.parse(out / "regression-tests.xml").getroot().findall(".//testcase")
    if not tests or any(t.find("failure") is not None or t.find("skipped") is not None or t.find("error") is not None for t in tests):
        raise ValueError("regression qualification failed/skipped")
    cards = []
    for id in WET:
        cards.append(f'<section><h2>{html.escape(id)}</h2><div class="pair"><figure><img src="{id}-local.png"><figcaption>Local detail</figcaption></figure><figure><img src="{id}-wide.png"><figcaption>Wider framing; finite coverage remains visible</figcaption></figure></div><details><summary>Same scene, water hidden</summary><img src="{id}-hidden.png"></details></section>')
    table = ''.join(f'<tr><td>{r["id"]} / {r["repeat"]+1}</td><td>{r["whole_gpu_p95_ms"]:.3f}</td><td>{r["cpu_service_p95_ms"]:.3f}</td><td>{r["peak_process_rss_kib"]/1024:.0f}</td></tr>' for r in rows)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Prepared terrain landscapes V1</title><style>body{{background:#15191e;color:#e1e6ec;font:16px system-ui;margin:0}}main{{max-width:1300px;margin:auto;padding:24px}}section{{margin:36px 0}}.pair{{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}}figure{{margin:0}}img,video{{width:100%;height:auto}}a{{color:#91cbd8}}table{{border-collapse:collapse;width:100%}}td,th{{padding:8px;text-align:left;border-bottom:1px solid #46505b}}@media(max-width:750px){{.pair{{grid-template-columns:1fr}}}}</style></head><body><main>
<h1>Prepared terrain landscapes</h1><p>Shared pinned catalog, Terrain selection + Apply/reset, four wet presets and three terrain-only choices. No solver or producer runs in Terrain. Defaults have not been promoted; this is the proposed mountain/local starting experience.</p>
<p>Water is a held twelve-hour state under deliberately exaggerated 512 mm/h rain. Surface motion is decorative; changing resolution/extent selects a different complete saved state, not a coupled LOD simulation.</p>
<section><h2>Proposed default: original mountain, 32m water</h2><video controls loop muted preload="metadata" src="mountain-local-motion.mp4"></video><p>Motion on immutable depth/velocity fields, not advancing hydraulics.</p></section>
{''.join(cards)}<details><summary>Terrain-only alternatives</summary>{''.join(f'<h3>{id}</h3><img src="{id}-local.png">' for id in DRY)}</details>
<section><h2>Integration checks</h2><p>{len(tests)} regression groups passed. All four old locked water frames remain pixel-exact. Auto selects the proposed default exactly when explicitly requested. Missing/conflicting/unknown selections fail before GPU startup. Headless replacement tests retain the old scene after preparation failure, rebuild targets, and shut down with a pending load. An asynchronous Apply also installs while headless frames continue, using the actual submitted retirement ticket.</p><p>Measured Apply preparation/upload:</p><ul>{''.join(f'<li>{r["id"]}: {r["prepare_ms"]:.0f} + {r["install_ms"]:.0f} ms</li>' for r in life["applies"])}</ul><p>Lifecycle run peak RSS: {life["sequence"]["peak_process_rss_kib"]/1024:.0f} MiB. Highest GPU heap observation immediately after installation, before retiring the old resident: {life["observed_install_heap_mib_max"]:.0f} MiB. This is an overlap observation, not continuous peak instrumentation. Temporary old/new overlap is retained, not hidden.</p></section>
<section><h2>Render-only performance</h2><p>1080p headless Vulkan on RTX 5070 Ti; solver stopped, no video/readback, 120 warmup frames excluded. Gates: whole GPU scene ≤8ms; CPU service ≤4ms.</p><table><tr><th>Preset / repeat</th><th>GPU p95 ms</th><th>CPU p95 ms</th><th>Peak RSS MiB</th></tr>{table}</table></section>
<section><h2>Boundaries and handoff</h2><p>No-argument startup is still legacy. Try <code>--terrain-landscape mountain-local</code>, <code>mountain-wide</code>, or explicit <code>auto</code>. Site and water selectors are visible in the native GUI, but native GUI/layout acceptance is deferred; headless tests exercise their shared loading path.</p><p>Finite edges, unqualified far shadows, bright reflective films and excessive localized storm pools remain. This loop does not change materials, solver math, generated terrain, live Fluid defaults, or add sparse/free-flight streaming. Final default promotion waits for user review.</p><p><a href="protocol.json">Frozen protocol</a> · <a href="capture.json">Visual/parity</a> · <a href="lifecycle.json">Lifecycle</a> · <a href="negative.json">Failures/auto</a> · <a href="performance.json">Performance</a> · <a href="regression-tests.xml">Tests</a> · <a href="../wider-water-world-20261012-gqeluh/">Previous coverage/stress study</a></p></section>
</main></body></html>'''
    (out / "index.html").write_text(document)
    write(out / "report-receipt.json", {"tests": len(tests), "parity_exact": 4, "performance_pass": 8,
        "default_promoted": False, "native_gui_review": "deferred", "runtime": render.runtime()})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--phase", required=True, choices=("freeze", "capture", "lifecycle", "negative", "performance", "report"))
    args = parser.parse_args()
    out = args.out.resolve()
    if args.phase == "freeze":
        prepare.prepare(CATALOG, ROOT / "cache/terrain")
        write(out / "protocol.json", {"runtime": render.runtime(), "inputs": identities(),
            "harness_sha256": render.digest(Path(__file__)), "default_promoted": False,
            "scope": "Headless-only; no producer/solver work. Native GUI/manual promotion deferred."})
    else:
        globals()[args.phase](out)


if __name__ == "__main__":
    main()
