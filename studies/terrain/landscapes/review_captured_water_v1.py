#!/usr/bin/env python3
"""Headless saved-state review; no solver launch, no hydraulic time advance."""
import argparse
import csv
import hashlib
import html
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import time
import threading
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / "build/release/projects/terrain/terrain"
SOURCE = ROOT / "cache/terrain/sources/v1/landscape-variations/temperate-mountain-valley/heightfield.json"
CAPTURE = ROOT / "cache/terrain/water/v1/mountain-rain-fed-1024-16m/water-capture.json"
RECORDING = ROOT / "outputs/fluid/offscreen-perf-20261010-CwWtK3/soak-600s-offscreen/live-0/static-recording/recording.json"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def runtime():
    return {"app": digest(APP), "shaders": {p.name: digest(p) for p in
            sorted((APP.parent / "shaders").glob("*.spv"))}}


def inputs():
    result = {str(p.relative_to(ROOT)): digest(p) for p in CAPTURE.parent.iterdir() if p.is_file()}
    source = json.loads(SOURCE.read_text())
    result[str(SOURCE.relative_to(ROOT))] = digest(SOURCE)
    # Source elevation identity is checked in C++, not inferred from filenames.
    result["source_elevation_sha256"] = source["files"]["elevation"]["sha256"]
    elevation = SOURCE.parent / source["files"]["elevation"]["path"]
    result[str(elevation.relative_to(ROOT))] = digest(elevation)
    companion = SOURCE.parent / "surface-fields.json"
    result[str(companion.relative_to(ROOT))] = digest(companion)
    return result


def run(argv, log, expected_exit=0):
    env = dict(os.environ)
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "CUBEY_PRIVATE_XVFB"):
        env.pop(key, None)
    started = time.monotonic()
    # Linux wait4 reports this run's peak RSS (including the RTK-launched app),
    # rather than the cumulative high-water mark across previous child runs.
    with log.open("x") as output:
        child = subprocess.Popen(argv, cwd=ROOT, env=env, stdout=output,
                                 stderr=subprocess.STDOUT, start_new_session=True)
        def stop_owned_run():
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        deadline = threading.Timer(180, stop_owned_run)
        deadline.start()
        try:
            _, status, usage = os.wait4(child.pid, 0)
        finally:
            deadline.cancel()
        child.returncode = os.waitstatus_to_exitcode(status)
    text = log.read_text()
    if child.returncode != expected_exit or "validation error" in text.lower():
        raise RuntimeError(text[-4000:])
    return {"wall_seconds": time.monotonic() - started, "peak_process_rss_kib": usage.ru_maxrss,
            "process_cpu_seconds": usage.ru_utime + usage.ru_stime, "command": argv,
            "exit_code": child.returncode}


def command(camera, water=True, reference=True):
    # Historical V1 comparisons must not follow subsequently promoted defaults.
    argv = ["rtk", "proxy", str(APP), "--headless", "--terrain-heightfield", str(SOURCE),
            "--terrain-water-capture", str(CAPTURE), "--terrain-water-capture-camera", camera]
    if reference:
        argv += ["--terrain-readability", "current"]
    if not water:
        argv += ["--no-terrain-water-visible"]
    return argv


def pixels(path):
    result = subprocess.run(["rtk", "proxy", "ffmpeg", "-v", "error", "-i", str(path),
                             "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"],
                            capture_output=True, timeout=30)
    if result.returncode:
        raise RuntimeError(result.stderr.decode())
    return result.stdout


def pixel_difference(a, b):
    x, y = pixels(a), pixels(b)
    if len(x) != len(y) or not x:
        raise ValueError("pixel dimensions differ")
    differences = [abs(i-j) for i, j in zip(x, y)]
    return {"mean_abs_8bit": sum(differences)/len(differences),
            "max_abs_8bit": max(differences), "different_channels": sum(d != 0 for d in differences)}


def percentile(values, fraction=.95):
    ordered = sorted(values)
    if not ordered:
        raise ValueError("empty measurement window")
    return ordered[max(0, math.ceil(fraction * len(ordered))-1)]


def timings(prefix):
    with prefix.with_suffix(".passes.csv").open() as source:
        rows = list(csv.DictReader(source))
    whole = [float(r["duration_ms"]) for r in rows if r["kind"] == "gpu" and
             r["label"] == "terrain captured patch whole scene"]
    cpu = [float(r["duration_ms"]) for r in rows if r["kind"] == "cpu" and
           r["label"] == "headless.frame_service"]
    if len(whole) < 100 or len(cpu) < 100:
        raise ValueError("insufficient complete measured frames")
    with prefix.with_suffix(".frames.csv").open() as source:
        frames = list(csv.DictReader(source))
    memory = [float(r["device_local_usage_mib"]) for r in frames
              if r["memory_budget_available"] == "1"]
    return {"whole_gpu_p95_ms": percentile(whole), "cpu_service_p95_ms": percentile(cpu),
            "gpu_samples": len(whole), "cpu_samples": len(cpu),
            "device_local_usage_mib_max": max(memory) if memory else None,
            "render_only_flow_foam_p95_ms": {label: percentile([float(r["duration_ms"]) for r in rows
                if r["kind"] == "gpu" and r["label"] == label]) for label in
                ("fluid_25d scenic flow filter", "fluid_25d scenic whitewater")
                if any(r["kind"] == "gpu" and r["label"] == label for r in rows)}}


def accepted_water_cost(summary):
    """Record an explicit user exception, never rewrite the automated gate."""
    rows = summary.get("performance", [])
    if len(rows) != 6 or summary.get("performance_budget_pass") is not False:
        raise ValueError("six final repeats and an original failed gate are required")
    if any(r["water_gpu_p95_ms"] > 8 or r["cpu_service_p95_ms"] > 4 for r in rows):
        raise ValueError("acceptance covers only the measured incremental water cost")
    misses = [{k: r[k] for k in ("camera", "repeat", "incremental_p95_ms")}
              for r in rows if r["incremental_p95_ms"] > 2]
    if not misses:
        raise ValueError("no incremental water-cost exception to accept")
    return {"status": "user-accepted-v1-with-measured-limitation",
            "original_incremental_gate_ms": 2, "misses": misses,
            "default_promoted": False, "manual_visual_acceptance": "deferred",
            "scope": "opt-in finite rain-fed capture, not backdrop stitching or live simulation"}


def collect_final_evidence(out, summary):
    if summary.get("runtime") != runtime() or summary.get("input_identity") != inputs():
        raise ValueError("report runtime/inputs differ from final captured and measured state")
    for asset in summary.get("assets", []):
        if digest(out / "terrain" / asset["path"]) != asset["sha256"]:
            raise ValueError("final captured image changed")
    if len(summary.get("terrain_fluid_parity", [])) != 3 or any(
            r["max_abs_8bit"] > 1 for r in summary["terrain_fluid_parity"]):
        raise ValueError("Terrain/Fluid parity exceeds the documented one-byte rounding difference")
    if digest(out / "terrain/backdrop-control.png") != digest(out / "terrain/backdrop-resident.png"):
        raise ValueError("resident capture changed the normal backdrop")
    tests = []
    for name in ("final-cpu-tests.xml", "final-gpu-tests.xml", "final-fluid-scenes.xml"):
        suite = ET.parse(out / name).getroot()
        if any(int(suite.get(key, "0")) for key in ("failures", "errors", "skipped", "disabled")):
            raise ValueError("final tests contain failures or unexecuted controls")
        tests.append({"path": name, "tests": int(suite.get("tests")), "failures": 0,
                      "sha256": digest(out / name)})
    gap_text = ''.join(ET.parse(out / "final-gpu-tests.xml").getroot().itertext())
    gap_error = float(re.search(r"max_error_m=([0-9.e+-]+)", gap_text).group(1))
    gap_samples = int(re.search(r"PASS (\d+) GPU samples", gap_text).group(1))
    if gap_error > .001:
        raise ValueError("paired GPU reconstruction exceeds the 1 mm gate")
    fluid = []
    for camera in ("overview", "slope", "basin"):
        old = out / "baseline-fluid" / (camera + ".png")
        new = out / "final-engine-fluid" / (camera + ".png")
        if digest(old) != digest(new):
            raise ValueError("Fluid extraction changed locked-camera pixels")
        fluid.append({"camera": camera, "byte_identical": True, "sha256": digest(new)})
    audit_logs = [p for p in (out / "terrain").glob("*.log") if p.name not in
                  ("backdrop-control.log", "backdrop-resident.log", "source-mismatch.log")]
    for log in audit_logs:
        if "terrain_water_capture_immutable: GPU bed/depth/decoded-velocity exact" not in log.read_text():
            raise ValueError("missing immutable GPU audit: " + log.name)
    mismatch = out / "terrain/source-mismatch.log"
    if not mismatch.exists():
        argv = command("overview")
        argv[argv.index("--terrain-heightfield") + 1] = str(
            ROOT / "tests/assets/terrain/backdrop-smoke/heightfield.json")
        run(argv + ["--frames", "1", "--output", str(out / "terrain/rejected-source.png")],
            mismatch, expected_exit=1)
    rejected = mismatch.read_text()
    if "source identity mismatch" not in rejected or "terrain_water_capture_upload" in rejected or (
            out / "terrain/rejected-source.png").exists():
        raise ValueError("source mismatch did not reject before rendering")
    capture = json.loads(CAPTURE.read_text())
    fields = capture["fields"]
    for field in fields.values():
        if digest(CAPTURE.parent / field["path"]) != field["sha256"]:
            raise ValueError("captured field digest mismatch")
    recording = json.loads(RECORDING.read_text())
    frame = recording["frames"][-1]
    if digest(RECORDING) != capture["provenance"]["recording_sha256"] or (
            digest(RECORDING.parent / frame["path"]) != capture["provenance"]["frame_sha256"]):
        raise ValueError("original recording/frame provenance changed")
    with (RECORDING.parent / frame["path"]).open("rb") as stream:
        original_depth_sha = hashlib.sha256(stream.read(fields["depth_m"]["byte_count"])).hexdigest()
    if digest(RECORDING.parent / recording["bed"]["path"]) != fields["bed_m"]["sha256"] or (
            original_depth_sha != fields["depth_m"]["sha256"]):
        raise ValueError("exported bed/depth are not bit-exact to the held recording")
    startup = []
    for camera in ("overview", "stream", "lake"):
        log = (out / "terrain" / (camera + "-water.log")).read_text()
        startup.append({"camera": camera,
            "load_and_appearance_ms": float(re.search(r"load_and_appearance_ms=([0-9.]+)", log).group(1)),
            "field_upload_ms": float(re.search(r"retained_input_gpu_bytes=\d+ wall_ms=([0-9.]+)", log).group(1)),
            "retained_input_gpu_bytes": int(re.search(r"retained_input_gpu_bytes=(\d+)", log).group(1))})
    return {"tests": tests, "paired_gpu_samples": gap_samples, "paired_gap_max_error_m": gap_error,
            "fluid_extraction": fluid, "immutable_gpu_audit_logs": [p.name for p in sorted(audit_logs)],
            "source_mismatch": {"path": "terrain/source-mismatch.log", "rejected_before_gpu": True},
            "capture_grid": capture["grid"], "capture_state": capture["state"],
            "field_sha256": {key: field["sha256"] for key, field in fields.items()},
            "source_elevation_sha256": capture["source_elevation_sha256"],
            "recording_provenance": capture["provenance"], "export_bed_depth_bit_exact": True,
            "startup": startup,
            "hydraulic_dispatches": 0}


def write_report(out, summary):
    cards = []
    for camera, label in (("overview", "Whole captured patch"), ("stream", "Downhill streams"),
                          ("lake", "Lake and banks")):
        cards.append(f'<section><h2>{label}</h2><div class="pair"><figure><img src="terrain/{camera}-ground.png" alt="{label}: paired ground only"><figcaption>Paired ground only</figcaption></figure><figure><img src="terrain/{camera}-water.png" alt="{label}: captured water enabled"><figcaption>Same ground + captured water</figcaption></figure></div></section>')
    data = summary.get("performance", [])
    table = ''.join(f'<tr><td>{r["camera"]}</td><td>{r["repeat"]}</td><td>{r["ground_gpu_p95_ms"]:.2f}</td><td>{r["water_gpu_p95_ms"]:.2f}</td><td>{r["incremental_p95_ms"]:.2f}</td><td>{r["cpu_service_p95_ms"]:.2f}</td></tr>' for r in data)
    acceptance = summary.get("acceptance", {})
    if acceptance.get("status") == "user-accepted-v1-with-measured-limitation":
        verdict = '<p class="verdict">Accepted as opt-in V1, with the measured water-cost exception. No default promotion; manual visual review remains deferred.</p>'
    else:
        verdict = '<p class="verdict">Automated evidence only; V1 acceptance is not recorded.</p>'
    evidence = summary.get("final_evidence", {})
    hashes = ''.join(f'<p>{html.escape(key)}: <code>{html.escape(value)}</code></p>'
                     for key, value in evidence.get("field_sha256", {}).items())
    tests = ''.join(f'<a href="{t["path"]}">{t["tests"]} passing tests</a> · '
                    for t in evidence.get("tests", []))
    costs = ''
    if data:
        runs = [run for row in data for run in row["runs"]]
        memory = [run["device_local_usage_mib_max"] for run in runs
                  if run.get("device_local_usage_mib_max") is not None]
        flow = [run["render_only_flow_foam_p95_ms"]["fluid_25d scenic flow filter"] for run in runs]
        foam = [run["render_only_flow_foam_p95_ms"]["fluid_25d scenic whitewater"] for run in runs
                if "fluid_25d scenic whitewater" in run["render_only_flow_foam_p95_ms"]]
        costs = f'<p>Measured whole-app device-local usage: {max(memory):.0f} MiB; peak process RSS: {max(r["peak_process_rss_kib"] for r in runs)/1024:.1f} MiB. Retained input fields occupy 28 MiB, not the total scene footprint. Normal Backdrop atmosphere/cloud resources remain resident even though they are not drawn in the patch view. Render-only flow filter p95: {min(flow):.3f}–{max(flow):.3f} ms; whitewater p95: {min(foam):.3f}–{max(foam):.3f} ms. These pass scopes overlap the whole-scene measurement; do not add them again.</p>'
    startup = evidence.get("startup", [])
    if startup:
        costs += f'<p>Three capture startups: source binding + load + appearance {min(s["load_and_appearance_ms"] for s in startup):.0f}–{max(s["load_and_appearance_ms"] for s in startup):.0f} ms; one-time field upload {min(s["field_upload_ms"] for s in startup):.1f}–{max(s["field_upload_ms"] for s in startup):.1f} ms. No per-frame hydraulic-field upload.</p>'
    gate = ''
    if summary.get("performance_budget_pass") is False:
        misses = [row for row in data if row["incremental_p95_ms"] > 2]
        details = ', '.join(f'{r["camera"]} repeat {r["repeat"]}: {r["incremental_p95_ms"]:.3f} ms' for r in misses)
        gate = f'<p>The original strict performance gate remains FAILED: {details}, above the proposed 2.000 ms incremental limit. The user accepted this measured limitation for opt-in V1. Whole-scene GPU (≤8 ms) and CPU service (≤4 ms) gates passed; this does not erase the incremental miss.</p>' if acceptance else f'<p>Original strict performance gate: FAILED ({details}).</p>'
    text = f'''<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Captured water joins the terrain foundation</title>
<style>body{{font:16px/1.55 system-ui;background:#111b24;color:#e5edf3;margin:0}}main{{max-width:1280px;margin:auto;padding:30px}}h1{{font-size:34px;line-height:1.2}}h2{{font-size:22px}}p{{max-width:860px}}.verdict{{border-left:3px solid #8dcefa;padding-left:14px}}code{{overflow-wrap:anywhere}}.pair{{display:grid;grid-template-columns:1fr 1fr;gap:14px}}figure{{margin:0}}img,video{{width:100%;display:block}}figcaption{{color:#b2c3d0}}section{{margin:32px 0}}a{{color:#8dcefa}}td,th{{text-align:left;padding:8px;border-bottom:1px solid #405263}}table{{border-collapse:collapse}}.table-scroll{{overflow-x:auto}}@media(max-width:800px){{.pair{{grid-template-columns:1fr}}}}</style>
<main><p>Terrain foundation · genuine offscreen Vulkan · held rain-fed state</p><h1>One captured state, one shared paired surface</h1>
{verdict}
<p>The existing Mountain rain-fed capture now renders in Terrain through the same ground/water/shadow, optics, daylight and whitewater code used by Fluid. No solver runs here. Surface motion is decorative; bed, depth and velocities stay fixed.</p>
<p>Reading the images: left is the paired ground, right adds the held water. The same rain-fed state is 10.6 simulation hours old, at 512 mm/h demo forcing. The 1024² / 16 m grid draws a 16.368 km cell-center footprint. This is not ordinary weather or a newly simulated lake.</p>
<p>Use Captured patch to inspect water. Backdrop remains the original terrain mode. These surfaces are never overlaid. Water-off retains the same ground and depth-driven wet shading; it does not dry the bed. The patch uses the accepted fixed daylight, not Backdrop's weather clock. Clouds are explicitly off for this first capture view; water does not yet provide cloud-consumable composed depth.</p>
{''.join(cards)}<section><h2>Motion, without hydraulic evolution</h2><div class="pair"><video controls loop muted src="terrain/stream-motion.mp4"></video><video controls loop muted src="terrain/lake-motion.mp4"></video></div><p>Fixed camera, fixed captured fields. Flow-driven flecks and shading animate; the basin does not fill or drain.</p></section>
<section><h2>Checks and limits</h2><p>Fluid extraction: three locked-camera captures remain byte-identical. Terrain differs by at most one 8-bit color value: Fluid's default sRGB attachment and Terrain's explicit UNORM attachment round the same display transform differently. The normal Backdrop control is byte-identical with/without the optional capture. Two retired slots, water off/on, held/resumed motion, resize, repeated patch/backdrop switches and immutable GPU fields pass headlessly.</p><p><a href="terrain/stream-frozen.mp4">Frozen surface control</a> · <a href="summary.json">Evidence summary</a>. This is a bounded patch, not a stitched full-terrain water product, rain-off capture, live simulation or manual GUI acceptance.</p></section>
<section><h2>1080p render-only measurements</h2><p>Starship · NVIDIA GeForce RTX 5070 Ti. Two repetitions per camera, ground only / water on, 10 seconds each with the first 120 frames excluded. All table values are milliseconds. 60 Hz pacing is excluded from CPU service. No PNG readback, encoding or hydraulic work in measurement windows. Increment is the difference between paired run p95 values, not a per-frame synchronized delta.</p><div class="table-scroll"><table><tr><th>Camera</th><th>Repeat</th><th>Ground GPU p95</th><th>Water GPU p95</th><th>Increment</th><th>CPU service p95</th></tr>{table}</table></div>{gate}{costs}</section>
<section><h2>Immutable field receipt</h2><p>{tests}<a href="terrain/source-mismatch.log">Source mismatch rejection</a>. Paired GPU gap: 1280 samples, maximum error {evidence.get("paired_gap_max_error_m", 0)*1000:.3f} mm (≤1 mm gate). GPU readback confirms saved bed/depth/decoded velocity are unchanged; no hydraulic dispatches run.</p>{hashes}</section>
<p><a href="summary.json">Evidence and commands</a> · <a href="/">All Cubey reports</a></p></main>'''
    (out / "index.html").write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--phase", choices=("capture", "performance", "report"), required=True)
    parser.add_argument("--accept-measured-water-cost", action="store_true",
                        help="Record explicit user acceptance of the final water-cost miss; report phase only")
    args = parser.parse_args()
    if args.accept_measured_water_cost and args.phase != "report":
        parser.error("acceptance is report-only and must follow final measurements")
    out = args.output.resolve()
    if out.parent != ROOT / "outputs/terrain" or not out.name.startswith("captured-water-paired-"):
        raise ValueError("owned paired-surface output directory required")
    summary_path = out / "summary.json"
    summary = json.loads(summary_path.read_text()) if summary_path.exists() else {}
    initial_runtime, initial_inputs = runtime(), inputs()
    if args.phase == "capture":
        directory = out / "terrain"
        directory.mkdir()
        assets, parity = [], []
        for camera, fluid in (("overview", "overview"), ("stream", "slope"), ("lake", "basin")):
            for water in (False, True):
                path = directory / (camera + ("-water.png" if water else "-ground.png"))
                result = run(command(camera, water) + ["--frames", "1", "--width", "1280", "--height", "720",
                    "--terrain-water-audit", "--output", str(path)], path.with_suffix(".log"))
                result.update(path=path.name, sha256=digest(path))
                assets.append(result)
                if water:
                    parity.append({"camera": camera, **pixel_difference(path, out / "baseline-fluid" / (fluid + ".png"))})
            animated = directory / (camera + "-phase6.png")
            run(command(camera) + ["--frames", "1", "--width", "1280", "--height", "720",
                "--terrain-water-animation-seconds", "6", "--terrain-water-audit", "--output", str(animated)],
                animated.with_suffix(".log"))
            if digest(animated) == digest(directory / (camera + "-water.png")):
                raise ValueError("render-only animation did not change pixels")
        for camera in ("stream", "lake"):
            path = directory / (camera + "-motion.mp4")
            run(command(camera) + ["--capture", "video", "--frames", "144", "--fps", "24",
                "--width", "1280", "--height", "720", "--terrain-water-audit", "--output", str(path)],
                path.with_suffix(".log"))
        frozen = directory / "stream-frozen.mp4"
        run(command("stream") + ["--capture", "video", "--frames", "48", "--fps", "24",
            "--width", "1280", "--height", "720", "--no-terrain-water-animate",
            "--terrain-water-audit", "--output", str(frozen)], frozen.with_suffix(".log"))
        # Same backdrop path, with the optional capture resident but not drawn.
        for resident in (False, True):
            path = directory / ("backdrop-resident.png" if resident else "backdrop-control.png")
            argv = command("overview") + ["--terrain-water-capture-view", "backdrop"]
            if not resident:
                index = argv.index("--terrain-water-capture")
                del argv[index:index+2]
            run(argv + ["--frames", "1", "--width", "640", "--height", "360", "--output", str(path)],
                path.with_suffix(".log"))
        if digest(directory / "backdrop-resident.png") != digest(directory / "backdrop-control.png"):
            raise ValueError("optional capture changed the normal backdrop pixels")
        transition = run(command("overview") + ["--headless-render-only", "--frames", "8",
            "--render-fps", "0", "--width", "640", "--height", "360",
            "--terrain-water-transition-smoke", "--terrain-water-audit"], directory / "transitions.log")
        text = (directory / "transitions.log").read_text()
        if text.count("terrain_water_transition:") != 8 or "immutable: GPU" not in text:
            raise ValueError("incomplete patch/backdrop transition smoke")
        summary.update(assets=assets, terrain_fluid_parity=parity, backdrop_parity="byte-identical",
                       transitions=transition, runtime=initial_runtime, input_identity=initial_inputs)
    elif args.phase == "performance":
        if summary.get("runtime") != initial_runtime:
            raise ValueError("performance binary differs from capture binary")
        directory = out / "performance"
        directory.mkdir()
        rows = []
        for camera in ("overview", "stream", "lake"):
            for repeat in range(2):
                pair = []
                for water in (False, True):
                    prefix = directory / f'{camera}-{repeat}-{"water" if water else "ground"}'
                    result = run(command(camera, water) + ["--headless-render-only", "--run-seconds", "10",
                        "--render-fps", "60", "--width", "1920", "--height", "1080", "--no-validation",
                        "--profile-output", str(prefix), "--profile-warmup-frames", "120",
                        "--profile-sample-interval", "1"], prefix.with_suffix(".log"))
                    result.update(timings(prefix))
                    pair.append(result)
                    print(f"measured {camera} repeat {repeat} water={water}", flush=True)
                rows.append({"camera": camera, "repeat": repeat,
                    "ground_gpu_p95_ms": pair[0]["whole_gpu_p95_ms"],
                    "water_gpu_p95_ms": pair[1]["whole_gpu_p95_ms"],
                    "incremental_p95_ms": pair[1]["whole_gpu_p95_ms"] - pair[0]["whole_gpu_p95_ms"],
                    "cpu_service_p95_ms": max(p["cpu_service_p95_ms"] for p in pair), "runs": pair})
        summary["performance"] = rows
        summary["performance_budget_pass"] = all(r["water_gpu_p95_ms"] <= 8 and
            r["incremental_p95_ms"] <= 2 and r["cpu_service_p95_ms"] <= 4 for r in rows)
    elif args.phase == "report":
        summary["final_evidence"] = collect_final_evidence(out, summary)
        if args.accept_measured_water_cost:
            summary["acceptance"] = accepted_water_cost(summary)
    if runtime() != initial_runtime or inputs() != initial_inputs:
        raise ValueError("runtime or native inputs changed during study")
    summary_path.write_text(json.dumps(summary, indent=2, allow_nan=False) + "\n")
    write_report(out, summary)
    print(json.dumps({"phase": args.phase, "summary": str(summary_path)}))


if __name__ == "__main__":
    main()
