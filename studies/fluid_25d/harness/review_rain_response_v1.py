#!/usr/bin/env python3
"""Changing-field rain-response clips, checked timeline and compact HTML export.

Hardware headless replay captures only, separate from live throughput trials.
No native imports, terrain changes, physical interpolation or held-state motion
passed off as hydraulic evolution. Published states are ~one minute apart.
"""
from __future__ import annotations

import argparse
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

import run_terrain_rain_demo as demo
from review_terrain_presentation_v1 import run_capture
from run_rain_response_v1 import write


def read(path):
    return json.loads(Path(path).read_bytes())


def verify(source, case):
    source = source.resolve(strict=True)
    result = read(source / "result.json")
    manifest = read(source / "recording/recording.json")
    if not result["passed"] or not result["immutable_case_unchanged"]:
        raise ValueError("failed hydraulic study")
    if manifest["provenance"].get("capture_schema") != "cubey.fluid25d.external-binary-capture.v1":
        raise ValueError("missing sealed binary capture provenance")
    identity = demo.case_identity(case)
    if read(source / "protocol.json")["identity"] != identity:
        raise ValueError("prepared case changed")
    if len(manifest["frames"]) != len(manifest["provenance"]["captures"]):
        raise ValueError("frame/capture count differs")
    for frame, capture in zip(manifest["frames"], manifest["provenance"]["captures"]):
        path = source / "recording" / frame["path"]
        if path.parent != source / "recording" or path.is_symlink():
            raise ValueError("unsafe recorded frame")
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != frame["sha256"]:
            raise ValueError("recorded hydraulic frame changed")
        header = capture["frame"]
        packet = struct.pack("<8sQQd", b"CBWTRV1\0", header["reset_generation"],
                             header["sequence"], frame["time_s"]) + raw
        if (header["session_id"] != result["session_id"] or header["reset_generation"] != 1 or
                capture["physical_time_s"] != frame["time_s"] or
                hashlib.sha256(packet).hexdigest() != capture["packet_sha256"]):
            raise ValueError("recorded frame is not its exact published packet")
    for key, expected in (("bed", identity["spec"]["terrain_source_identity"]["solver_bed_sha256"]),
                          ("source_bed", identity["spec"]["terrain_source_identity"]["source_bed_sha256"])):
        payload = source / "recording" / manifest[key]["path"]
        if demo.runtime.sha256_file(payload) != expected or manifest[key]["sha256"] != expected:
            raise ValueError("recorded bed changed")
    return result, manifest


def closest(frames, target):
    selected = min(frames, key=lambda row: abs(row["time_s"]-target))
    if abs(selected["time_s"]-target) > 75:
        raise ValueError("missing bounded observation checkpoint")
    return selected


def command(case, recording, camera, output, time_s=0, video=False):
    args = demo.viewer_command(case, recording, "Rain-response recorded review", 1280, 720,
        live=False, start=time_s, camera=camera, app=demo.RELEASE_APP)
    args[args.index("--frames")+1] = "360" if video else "1"
    args += ["--headless", "--fluid25d-recording-gpu-validation", "--output", str(output)]
    if video:
        args += ["--capture", "video", "--fps", "12",
                 "--fluid25d-recording-frame-interval-seconds", "60"]
    return args


def capture(a):
    case = (a.study / "cases/mountain-patch-1024-16m").resolve(strict=True)
    off_result, off = verify(a.off, case)
    on_result, on = verify(a.on, case)
    if off_result["rain_off_observed_s"] is None or on_result["rain_off_observed_s"] is not None:
        raise ValueError("need rain-off and continuous-rain controls")
    out = demo.runtime.fresh_output(a.out)
    out.mkdir()
    frozen = {"app_sha256": demo.runtime.sha256_file(demo.RELEASE_APP),
        "case": demo.case_identity(case), "off_recording_sha256": demo.runtime.sha256_file(a.off / "recording/recording.json"),
        "on_recording_sha256": demo.runtime.sha256_file(a.on / "recording/recording.json"),
        "shaders": {p.name: demo.runtime.sha256_file(p) for p in sorted((demo.RELEASE_APP.parent / "shaders").glob("*.spv"))},
        "harness_sha256": demo.runtime.sha256_file(Path(__file__)),
        "scope": "recorded field evolution, 720x timelapse; fields held between actual saves; no live-performance claim"}
    write(out / "capture-protocol.json", frozen)
    rows = []
    for label, source, manifest in (("off", a.off, off), ("on", a.on, on)):
        cameras = ("overview", "runoff", "collection") if label == "off" else ("collection",)
        for camera in cameras:
            name = f"{label}-{camera}"
            path = out / (name + ".mp4")
            args = command(case, source / "recording/recording.json", camera, path, video=True)
            receipt = run_capture(args, out / (name + ".log"), timeout=180)
            raw = (out / (name + ".log")).read_text()
            times = [float(v) for v in re.findall(r"fluid_25d_recording_capture:.*saved_s=(\S+)", raw)]
            if len(times) != 360 or len(set(times)) < 100 or any(b < c for c, b in zip(times, times[1:])):
                raise ValueError("clip does not contain advancing hydraulic fields")
            probe = subprocess.run(["rtk", "proxy", "ffprobe", "-v", "error", "-count_frames",
                "-select_streams", "v:0", "-show_entries", "stream=nb_read_frames,width,height,r_frame_rate",
                "-of", "json", str(path)], check=True, capture_output=True, text=True, timeout=30)
            stream = json.loads(probe.stdout)["streams"][0]
            if stream.get("nb_read_frames") != "360" or (stream["width"], stream["height"]) != (1280, 720):
                raise ValueError("encoded clip is incomplete")
            rows.append({"kind": "video", "case": label, "camera": camera, "path": path.name,
                "unique_hydraulic_states": len(set(times)), "first_saved_s": times[0], "last_saved_s": times[-1],
                "sha256": demo.runtime.sha256_file(path), "probe": stream, **receipt})
            print("captured", name, "changing states", len(set(times)), flush=True)
    for label, source, manifest in (("off", a.off, off), ("on", a.on, on)):
        for target in ((0, 1800, 7200, 10800, 21600) if label == "off" else (7200, 21600)):
            cameras = ("runoff", "collection") if label == "off" else ("collection",)
            frame = closest(manifest["frames"], target)
            for camera in cameras:
                name = f"{label}-{camera}-{target}"
                path = out / (name + ".png")
                receipt = run_capture(command(case, source / "recording/recording.json", camera, path,
                    frame["time_s"]), out / (name + ".log"))
                rows.append({"kind": "image", "case": label, "camera": camera,
                    "target_s": target, "saved_s": frame["time_s"], "path": path.name,
                    "sha256": demo.runtime.sha256_file(path), **receipt})
                print("captured", name, flush=True)
    if demo.runtime.sha256_file(demo.RELEASE_APP) != frozen["app_sha256"]:
        raise ValueError("renderer changed during capture matrix")
    write(out / "captures.json", rows)


def qualified_trial(path, app_hash, throughput=False):
    result = read(path / "validation.json")
    if result["errors"] or result["returncode"] or not result["headless_only"] or result["app_sha256"] != app_hash:
        raise ValueError("failed or mismatched headless trial: " + str(path))
    if any(item["active_group_remaining"] for item in result["cleanup"]):
        raise ValueError("owned cleanup failed")
    if throughput and not result["uninterrupted_throughput_trial"]:
        raise ValueError("mixed control trial cannot establish performance")
    return result


def guide_receipts(full, override):
    events = full["events"]
    completed = [e for e in events if e["event"] == "guide-complete-running"]
    if len(completed) != 1 or completed[0]["physical_s"] < 21600 or completed[0]["rain_mm_per_hour"] != 0:
        raise ValueError("guide completion not qualified")
    if sum(e["event"] == "guide-applied" for e in events) != 9:
        raise ValueError("guide did not finish all acknowledged milestones")
    identity = {(e["session_id"], e["solver_bed_sha256"]) for e in events}
    if len(identity) != 1:
        raise ValueError("guide reset or changed numerical identity")
    if not any(r.get("lifecycle") == "running" and r.get("physical_s", 0) > completed[0]["physical_s"]+30
               for r in full["samples"]):
        raise ValueError("solver did not continue advancing after guide completion")
    manual = override["events"]
    if (sum(e["event"] == "guide-started" for e in manual) != 2 or
        sum(e["event"] == "guide-cancelled" for e in manual) != 2 or
        any(e["event"] in ("guide-camera", "guide-complete-running") for e in manual)):
        raise ValueError("manual solver/camera override was not retained")
    return completed[0]


def report(a):
    out = a.out.resolve(strict=True)
    frozen = read(out / "capture-protocol.json")
    case = (a.study / "cases/mountain-patch-1024-16m").resolve(strict=True)
    off, _ = verify(a.off, case)
    on, _ = verify(a.on, case)
    full = qualified_trial(a.guide, frozen["app_sha256"])
    override = qualified_trial(a.override, frozen["app_sha256"])
    perf = qualified_trial(a.perf, frozen["app_sha256"], True)
    completed = guide_receipts(full, override)
    for row in read(out / "captures.json"):
        if demo.runtime.sha256_file(out / row["path"]) != row["sha256"]:
            raise ValueError("review media changed")
    gpu = perf["passes_ms"]["gpu:fluid_25d native presentation total"]["p95"]
    cpu = perf["passes_ms"]["cpu:headless.frame_service"]["p95"]
    age = perf["metrics"]["fluid_25d.external.advancing_state_age_s"]["p95"]
    pace = perf["physical_wall_ratio"]
    if gpu > 8 or cpu > 4 or age > 1 or pace < 54:
        raise ValueError("headless live performance budget missed")
    start = closest(off["frames"], off["rain_off_observed_s"])
    last = off["frames"][-1]
    peak = max((f for f in off["frames"] if f["time_s"] >= off["rain_off_observed_s"]),
               key=lambda f: f["collection_volume_m3"])
    rows = [{"target_s": t, "off": closest(off["frames"], t), "on": closest(on["frames"], t)}
            for t in (1800, 3600, 7200, 10800, 21600)]
    storage_loss = 100 * (1-last["water_volume_m3"]/start["water_volume_m3"])
    stream_loss = 100 * (1-last["runoff_volume_m3"]/start["runoff_volume_m3"])
    pool_gain = 100 * (peak["collection_volume_m3"]/start["collection_volume_m3"]-1)
    evidence = {"rain_off_s": off["rain_off_observed_s"], "storage_loss_percent": storage_loss,
        "runoff_roi_loss_percent": stream_loss, "collection_post_off_peak_gain_percent": pool_gain,
        "collection_post_off_peak_s": peak["time_s"], "matched_checkpoints": rows,
        "whole_map_wet_area_km2": [start["wet_km2"], last["wet_km2"]],
        "performance": {"gpu_p95_ms": gpu, "cpu_p95_ms": cpu, "field_age_p95_s": age, "pace": pace},
        "guide_completion": completed, "native_gui_review": "deferred", "app_sha256": frozen["app_sha256"],
        "scope": "exact snapshots and hardware headless checks; demonstration rainfall, not calibrated hydrology"}
    export = out / "export"
    export.mkdir()
    for row in read(out / "captures.json"):
        shutil.copy2(out / row["path"], export / row["path"])
    for name in ("capture-protocol.json", "captures.json"):
        shutil.copy2(out / name, export / name)
    for name, path in (("guide-validation.json", a.guide), ("override-validation.json", a.override),
                       ("performance-validation.json", a.perf)):
        shutil.copy2(path / "validation.json", export / name)
    write(export / "evidence.json", evidence)
    write(export / "timeline.json", {"off": off["frames"], "on": on["frames"], "off_s": off["rain_off_observed_s"]})
    table = "".join(f"<tr><td>{r['target_s']/3600:g} h</td><td>{r['off']['water_volume_m3']/1e6:.2f}</td>"
        f"<td>{r['on']['water_volume_m3']/1e6:.2f}</td><td>{r['off']['collection_volume_m3']/1e6:.2f}</td>"
        f"<td>{r['on']['collection_volume_m3']/1e6:.2f}</td></tr>" for r in rows)
    page = f"""<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Fluid 2.5D — rain becomes runoff</title><style>
body{{margin:0;background:#111820;color:#e6edf2;font:16px/1.55 system-ui}}main{{max-width:1180px;margin:auto;padding:32px}}
h1,h2{{line-height:1.2}}a{{color:#85d4ff}}.grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:20px}}
section,article{{background:#1b2630;padding:20px;border-radius:10px;margin:20px 0}}video,img{{width:100%;display:block;border-radius:6px}}
.muted{{color:#b7c6d2}}table{{border-collapse:collapse;width:100%;font-size:14px}}td,th{{padding:8px;text-align:left;border-bottom:1px solid #42505b}}
button{{padding:9px 16px;background:#244b63;color:white;border:1px solid #85d4ff;border-radius:5px;cursor:pointer}}
canvas{{width:100%;height:280px}}code{{overflow-wrap:anywhere}}.scroll{{overflow:auto}}details{{margin:16px 0}}
</style><main><nav><a href="/reports/#project=cubey">Cubey reports</a></nav>
<h1>Rain becomes runoff, then drains</h1><p>The same unmodified Mountain patch: <strong>1024² / 16m / 16.384km</strong>.
Rain supplies the whole map. There is no point source or authored drain; water can leave at the outer boundary.</p>
<p><strong>Verdict:</strong> the current setup has a clear hydraulic story. After rain-off, whole-map storage fell
<strong>{storage_loss:.1f}%</strong> and the fixed runoff observation region lost <strong>{stream_loss:.1f}%</strong> of its water.
The collection region initially gained another <strong>{pool_gain:.1f}%</strong> from arriving upstream water before slowly receding.</p>
<section><h2>Start here: downhill response</h2><p>Watch connected routes appear, then smaller routes retreat after the two-hour rain shutoff.
<strong>0–10 seconds: rain on. At 10 seconds: rain off. 10–30 seconds: continuing drainage.</strong></p>
<video controls preload="metadata" poster="off-runoff-7200.png" src="off-runoff.mp4"></video>
<p class="muted">Recorded changing-field timelapse: 30 seconds represents six physical hours (720×). This is not a live-FPS recording.
Exact fields are held between snapshots roughly 60–70 simulated seconds apart. Rain streaks, dots and foam remain decorative.</p></section>
<section><h2>Why a basin can still fill after rain stops</h2><p>Identical camera and timeline. Left: rainfall stops at two hours. Right: rainfall continues at 512 mm/h.
Water already on the slopes continues arriving after shutoff; local growth is compatible with whole-map drainage.</p>
<button id="compare">Play / restart both</button><div class="grid"><article><h3>Rain off after 2h</h3>
<video id="off" controls preload="metadata" poster="off-collection-21600.png" src="off-collection.mp4"></video></article>
<article><h3>Rain continues</h3><video id="on" controls preload="metadata" poster="on-collection-21600.png" src="on-collection.mp4"></video></article></div>
<p class="muted">Matched requested times; actual saved checkpoints differ by less than 75 physical seconds. No state interpolation or hydraulic amplification.</p></section>
<section><h2>Read the measured response</h2><p>Stored volume, in millions of cubic metres. Collection uses a fixed approximately 2km-wide region around the existing basin camera—not an invented basin boundary.</p>
<canvas id="plot" width="1120" height="280" aria-label="Stored water over simulation hours"></canvas><p id="plot-legend" class="muted"></p>
<div class="scroll"><table><thead><tr><th>Physical time</th><th>Whole map: off</th><th>Whole map: on</th><th>Collection: off</th><th>Collection: on</th></tr></thead><tbody>{table}</tbody></table></div></section>
<details><summary>Whole patch and still checkpoints</summary><video controls preload="metadata" src="off-overview.mp4"></video>
<div class="grid"><article><h3>Dry start</h3><img loading="lazy" src="off-collection-0.png" alt="Dry basin before rain"></article>
<article><h3>After 30 physical minutes</h3><img loading="lazy" src="off-collection-1800.png" alt="Basin gathering runoff"></article>
<article><h3>Rain-off boundary, about 2h</h3><img loading="lazy" src="off-collection-7200.png" alt="Basin at rain shutoff"></article>
<article><h3>Four hours after shutoff</h3><img loading="lazy" src="off-collection-21600.png" alt="Remaining basin water after drainage"></article></div></details>
<section><h2>Live demonstration in Cubey</h2><p>Launch the existing Terrain Water demo, keep Mountain / Default 1k selected, and choose
<strong>Run rain-response demonstration</strong>. It starts only from dry/paused, acknowledges 512 mm/h and 60×,
then changes the existing observation cameras at physical-time milestones. Rain stops after two hours; the guide finishes at six hours
<strong>without pausing the solver</strong>.</p><p>Pause, reset, rain/pace changes, camera selection or orbit/zoom leave the guide.
The guide is optional; ordinary startup remains dry/paused. Reset clears water and lets you repeat.</p>
<p>Fresh uninterrupted 1080p headless live check: GPU p95 <strong>{gpu:.2f}ms</strong>, CPU service p95 <strong>{cpu:.2f}ms</strong>,
field age p95 <strong>{age:.2f}s</strong>, achieved pace <strong>{pace:.1f}×</strong>. No capture, encoding or observer archive in that performance run.</p>
<p class="muted">Headless controls and exact GPU imports are qualified. Native GUI layout/interaction review remains deferred.
512 mm/h is deliberately exaggerated demo rainfall; this is not a calibrated weather model, a quiet-lake certificate or a conservation audit.</p></section>
<details><summary>Evidence and provenance</summary><p><a href="evidence.json">Measured verdict and checks</a> · <a href="timeline.json">All saved-state metrics</a> ·
<a href="captures.json">Capture receipts</a> · <a href="capture-protocol.json">Frozen renderer and inputs</a> ·
<a href="guide-validation.json">Guide completion</a> · <a href="override-validation.json">Manual override/reset</a> · <a href="performance-validation.json">Uninterrupted live performance</a></p>
<p>Rain-off applied at {off['rain_off_observed_s']:.3f}s. Basin-region post-off maximum at {peak['time_s']/3600:.3f}h.
Fields and beds retain their native values; renderer {html.escape(frozen['app_sha256'])}.</p>
<p><a href="/reports/3b1edf62bd0def56df30a7d9d49af487ff291fc9527eab38d4961150b58e89e1/">Earlier patch-only review</a> ·
<a href="/reports/a9e51f964348adf9f3963655b4dfe06061b3fc0c88c4717e436d537c9bc76bb0/">Historical environment study (cloud integration since removed)</a></p></details>
<script>
const off=document.querySelector('#off'),on=document.querySelector('#on');
document.querySelector('#compare').onclick=async()=>{{off.pause();on.pause();off.currentTime=on.currentTime=0;
try{{await Promise.all([off.play(),on.play()]);}}catch(e){{document.querySelector('#compare').textContent='Use the video play controls';}}}};
fetch('timeline.json').then(r=>r.json()).then(d=>{{const c=document.querySelector('#plot'),g=c.getContext('2d');
const series=[['Whole map: off',d.off,'water_volume_m3','#70c8ff'],['Whole map: on',d.on,'water_volume_m3','#e1ba73'],
['Collection: off',d.off,'collection_volume_m3','#66e2b4'],['Collection: on',d.on,'collection_volume_m3','#cc9bdb']];
const max=Math.max(...series.flatMap(s=>s[1].map(f=>f[s[2]]/1e6)))*1.05;
g.fillStyle='#b7c6d2';g.font='14px system-ui';for(let i=0;i<=6;i++){{let x=60+i*170;g.fillText(i+'h',x-7,270);}}
for(let i=0;i<=4;i++){{let y=240-i*55;g.strokeStyle='#34434e';g.beginPath();g.moveTo(60,y);g.lineTo(1080,y);g.stroke();g.fillText((max*i/4).toFixed(0),8,y+5);}}
g.strokeStyle='#9ba7ad';g.setLineDash([5,5]);g.beginPath();g.moveTo(60+d.off_s/3600*170,20);g.lineTo(60+d.off_s/3600*170,240);g.stroke();g.setLineDash([]);
for(const [label,frames,key,color]of series){{g.strokeStyle=color;g.lineWidth=2;g.beginPath();frames.forEach((f,i)=>{{let x=60+f.time_s/3600*170,y=240-f[key]/1e6/max*220;i?g.lineTo(x,y):g.moveTo(x,y);}});g.stroke();}}
document.querySelector('#plot-legend').innerHTML=series.map(s=>'<span style="color:'+s[3]+'">'+s[0]+'</span>').join(' · ')+' · dashed line: rain off';
}}).catch(()=>{{document.querySelector('#plot-legend').textContent='Timeline unavailable; numeric checkpoints remain in the table.';}});
</script></main></html>"""
    with (export / "index.html").open("x") as file:
        file.write(page)
    print(json.dumps(evidence), flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=("capture", "report"))
    for key in ("study", "off", "on", "out"):
        p.add_argument("--"+key, type=Path, required=True)
    for key in ("guide", "override", "perf"):
        p.add_argument("--"+key, type=Path)
    a = p.parse_args()
    if a.mode == "capture":
        capture(a)
    else:
        if not all((a.guide, a.override, a.perf)):
            p.error("report requires guide, override and perf receipts")
        report(a)


if __name__ == "__main__":
    main()
