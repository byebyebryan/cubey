#!/usr/bin/env python3
"""Compact remote mountain-rain story from immutable native fields, not a solve."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import run_mountain_rain_demo as demo
import run_native_bank_render_v1 as bank
import run_native_presentation_v1 as ref


def plan() -> list[dict]:
    # Include exactly the final state, without inventing time beyond the source.
    videos = [ref.video_asset("01-story", "rain-off", "overview", 0, 961, 10, 15),
              ref.video_asset("02-flow-detail", "rain-off", "runoff", 4800, 250, 10, 5),
              ref.video_asset("03-recession-off", "rain-off", "collection", 7260, 239, 10, 30),
              ref.video_asset("03-recession-on", "rain-on", "collection", 7260, 239, 10, 30)]
    stills = [ref.still_asset("rain-off", "overview", t) for t in (0, 1800, 6000, 14400)]
    stills += [ref.diagnostic_asset("rain-off", "depth", 10860),
               ref.diagnostic_asset("rain-off", "flow", 6000)]
    return videos + stills


def caption_filters(asset: dict) -> str:
    font = ref.find_font()
    video = asset["kind"] == "video"
    start = asset["requested_start_time_s"] if video else asset["requested_time_s"]
    requested = f"{start}+n*{asset['render_interval_s']}" if video else str(start)
    saved = f"floor(({requested})/60)*60"
    line = (asset["case"] + " | RECORDED requested %{eif\\:" + requested + "\\:d}s"
            " | saved field %{eif\\:" + saved + "\\:d}s")
    def text(value, y, enable=None):
        result = f"drawtext=fontfile={font}:fontcolor=white:fontsize=15:x=12:y={y}:text='{value}'"
        return result + (f":enable='{enable}'" if enable else "")
    filters = ([f"setpts=N/({asset['fps']}*TB)"] if video else [])
    filters += ["drawbox=x=0:y=0:w=iw:h=62:color=black@0.8:t=fill", text(line, 7)]
    speed = asset["fps"] * asset["render_interval_s"] if video else None
    suffix = f" | {speed}x viewing" if video else " | still"
    if asset["case"] == "rain-on":
        filters.append(text("Rain at saved field ON - 120 mm/h" + suffix, 34))
    else:
        filters += [text("Rain at saved field ON - 120 mm/h" + suffix, 34, f"lt({saved},7260)"),
                    text("Rain at saved field OFF - 0 mm/h" + suffix, 34, f"gte({saved},7260)")]
    legend = ("Blue = depth | dots = approximate velocity | held fields every 60s | reference banks"
              if asset["kind"] != "diagnostic" else
              ("RAW depth - log scale 0.01 / 0.1 / 1 / 10 m; not surface color" if asset["view"] == "depth" else
               "RAW speed - 0 to 15 m/s; color saturates above 15 m/s"))
    filters += ["drawbox=x=0:y=ih-30:w=iw:h=30:color=black@0.8:t=fill", text(legend, "h-23")]
    return ",".join(filters)


def run_ffmpeg(out: Path, label: str, args: list[str]) -> dict:
    _, receipt = ref.run_logged(["rtk", "proxy", "ffmpeg", "-nostdin", "-n", "-v", "error", *args],
                                out / "logs", label, timeout=600)
    return receipt


def source_hashes() -> dict:
    names = ("run_mountain_rain_demo.py", "probe_mountain_rain_demo.py",
             "review_mountain_rain_demo_v1.py", "test_run_mountain_rain_demo.py",
             "test_review_mountain_rain_demo_v1.py", "CMakeLists.txt")
    return {str((demo.HERE / name).relative_to(ref.ROOT)): ref.sha256_file(demo.HERE / name) for name in names}


def capture(out: Path) -> dict:
    ref.reserve_directory(out)
    (out / "media").mkdir()
    (out / "raw").mkdir()
    (out / "diagnostics").mkdir()
    identity, sources = bank.identity(), source_hashes()
    ref.write_json_exclusive(out / "protocol.json", {"frozen_before_capture": True, "assets": plan(),
        "runtime_identity": identity, "runner_source_sha256": sources,
        "hydraulic_solver": "none; immutable recording replay", "preset": "readable/reference/dots/highlights-off",
        "rain_labels": "phase/rate at held saved field; rain-off ramps 120 to 0 mm/h from 7200 to 7260s",
        "human_visual_acceptance": "deferred"})
    rows = []
    for asset in plan():
        video = asset["kind"] == "video"
        raw = out / "raw" / (asset["id"] + (".mp4" if video else ".png"))
        command = ref.app_command(asset, raw, "readable", asset["kind"] != "diagnostic")
        command += ["--fluid25d-native-surface-highlights", "off", "--fluid25d-native-bank-view", "reference"]
        _, receipt = ref.run_logged(command, out / "logs", asset["id"], timeout=900,
                                   expected_asset=asset, expected_presentation="readable")
        path = out / "media" / raw.name
        args = (["-ignore_editlist", "1"] if video else []) + ["-i", str(raw), "-vf", caption_filters(asset)]
        if video:
            ref.validate_probe(ref.ffprobe_video(raw), asset["frame_count"], asset["fps"])
            args += ["-an", "-c:v", "libx264", "-preset", "veryfast", "-crf", "20", "-pix_fmt", "yuv420p",
                     "-r", str(asset["fps"]), "-frames:v", str(asset["frame_count"]), "-movflags", "+faststart"]
        else:
            args += ["-frames:v", "1"]
        caption = run_ffmpeg(out, "caption-" + asset["id"], args + [str(path)])
        row = {**asset, "path": str(path.relative_to(out)), "sha256": ref.sha256_file(path),
               "raw_path": str(raw.relative_to(out)), "raw_sha256": ref.sha256_file(raw),
               "capture_command": receipt, "caption_command": caption}
        if video:
            row["probe"] = ref.validate_probe(ref.ffprobe_video(path, ignore_editlist=False), asset["frame_count"], asset["fps"])
            for sample in (0, asset["frame_count"] // 2, asset["frame_count"] - 1):
                run_ffmpeg(out, f"decode-{asset['id']}-{sample}", ["-i", str(path), "-vf", f"select=eq(n\\,{sample})",
                    "-frames:v", "1", str(out / "diagnostics" / f"{asset['id']}-{sample}.png")])
        elif ref.png_dimensions(path) != (960, 540):
            raise ValueError("captioned still dimensions changed")
        rows.append(row)
        print("Captured/decoded " + asset["id"], flush=True)
    off = next(r for r in rows if r["id"] == "03-recession-off-rain-off")
    on = next(r for r in rows if r["id"] == "03-recession-on-rain-on")
    if off["timeline"] != on["timeline"] or off["camera"] != on["camera"]:
        raise ValueError("rain-on/off pair is not matched")
    pair = out / "media/03-rain-on-off-pair.mp4"
    receipt = run_ffmpeg(out, "rain-pair", ["-i", str(out / off["path"]), "-i", str(out / on["path"]),
        "-filter_complex", "[0:v][1:v]hstack=inputs=2[v]", "-map", "[v]", "-an", "-c:v", "libx264",
        "-preset", "veryfast", "-crf", "20", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(pair)])
    pair_probe = ref.validate_probe(ref.ffprobe_video(pair, ignore_editlist=False), 239, 10, 1920, 540)
    run_ffmpeg(out, "pair-contact", ["-i", str(pair), "-vf", "select=eq(mod(n\\,60)\\,0),scale=960:270,tile=2x2",
        "-frames:v", "1", str(out / "diagnostics/pair-contact.png")])
    # Standalone rendering cost: do not run concurrently with CUDA/other GPU tests.
    profile_asset = ref.video_asset("profile", "rain-off", "runoff", 6000, 120, 30, 5)
    profile_asset["profile_warmup_frames"] = 12
    prefix = out / "diagnostics/profile"
    command = ref.app_command(profile_asset, prefix.with_suffix(".mp4"), "readable", True, prefix)
    command += ["--fluid25d-native-surface-highlights", "off", "--fluid25d-native-bank-view", "reference"]
    _, cost_receipt = ref.run_logged(command, out / "logs", "standalone-profile", timeout=600,
                                    expected_asset=profile_asset, expected_presentation="readable")
    cost = {"gpu": ref.profile_summary(prefix, 12, 108), "capture_render_readback_encode_wall_s": cost_receipt["wall_s"],
            "scope": "960x540, dots, native presentation GPU span; raw capture wall includes readback/encoding, not solver time",
            "solver_wall_s": 0, "concurrent_native_worker": False}
    ref.assert_same_runtime(identity, bank.identity(), "remote story capture")
    if sources != source_hashes():
        raise ValueError("review runner/source changed during capture")
    report = {"status": "pass", "runtime_identity": identity, "runner_source_sha256": sources, "assets": rows,
        "pair": {"path": str(pair.relative_to(out)), "sha256": ref.sha256_file(pair), "probe": pair_probe,
                 "command": receipt, "timeline": off["timeline"]}, "cost": cost, "human_visual_acceptance": "deferred"}
    ref.write_json_exclusive(out / "manifest.json", report)
    review(out, report)
    # Seal every generated artifact. The seal itself is intentionally outside its own tree.
    files = {str(p.relative_to(out)): ref.sha256_file(p) for p in sorted(out.rglob("*")) if p.is_file()}
    ref.write_json_exclusive(out / "integrity.json", {"status": "pass", "files": files,
        "inputs_preserved": identity["inputs"] == ref.frozen_input_identity(), "human_visual_acceptance": "deferred"})
    return report


def review(out: Path, report: dict) -> None:
    story = next(r for r in report["assets"] if r["id"].startswith("01-story"))
    detail = next(r for r in report["assets"] if r["id"].startswith("02-flow-detail"))
    stills = [r for r in report["assets"] if r["kind"] == "still"]
    labels = ("Dry start - 0 min", "Runoff developing - 30 min", "Converging streams - 100 min", "Rain off, recession - 240 min")
    page = """<!doctype html><meta charset='utf-8'><title>Mountain Rain Demo V1</title>
<style>body{background:#16202a;color:#eee;max-width:1200px;margin:auto;padding:24px;font:17px system-ui;line-height:1.5}a{color:#8cf}img,video{width:100%;height:auto}summary{cursor:pointer}.stills{display:grid;grid-template-columns:1fr 1fr;gap:16px}</style>
<h1>Mountain rain: follow the water, then remove the rain</h1>
<p>Unchanged Terrain Diffusion mountain terrain, 15.36 km across, 512x512 cells at 30 m.
Uniform rainfall supplies the whole map; gravity concentrates runoff into branching channels.
The native fall perimeter lets water leave. There is no authored river or point drain.</p>
<p><b>Blue is water depth, not speed.</b> Dots/trails reveal approximate velocity between saved states;
they are visual indicators, not native water parcels or conserved dye. Banks are the triangular reference.</p>
<h2>1. Full story - about 96 seconds, 150x playback</h2>
<p>Dry start, rainfall accumulation and streams, rain switching off at 121 physical minutes, then recession.
Look for small tributaries feeding larger channels and persistent downstream storage after rain stops.</p>"""
    page += f"<video controls preload='metadata' src='{story['path']}'></video>"
    page += "<h2>2. Read the motion - 25 seconds, 50x playback</h2><p>Closer mature-runoff view. Follow the dots along the branches and into the main flow. This is a different camera/window, not additional simulation.</p>"
    page += f"<video controls preload='metadata' src='{detail['path']}'></video>"
    page += "<h2>3. Does it respond to removing rain? - 24 seconds, 300x playback</h2><p>Left: rain off. Right: rain stays on. Same terrain, physical times and collection camera. Watch narrowing tributaries and changing storage rather than expecting all water to vanish instantly. Deep collection is not proof of a calm lake.</p>"
    page += f"<video controls preload='metadata' src='{report['pair']['path']}'></video><div class='stills'>"
    for label, row in zip(labels, stills, strict=True):
        page += f"<figure><figcaption>{label}</figcaption><a href='{row['path']}'><img src='{row['path']}'></a></figure>"
    page += "</div><details><summary>Diagnostic truth and limits</summary><p>Depth/momentum fields are held at saved 60-second states; they are not interpolated. Captions show both requested render time and actual held field time. Rain labels belong to that saved state; the 7200-7260 s ramp has no intermediate saved field. Playback pace is not solver throughput.</p>"
    for row in report["assets"]:
        if row["kind"] == "diagnostic":
            page += f"<img src='{row['path']}'>"
    page += "<p>Composite deliberately hides very thin rain film to reveal channels. Raw diagnostics keep it. Display smoothing can change apparent widths/connectivity; B-spline stays experimental and prerecorded marching-squares masks stay in a separate bounded comparison.</p>"
    page += "<p><a href='../../bank-comparison-v1-20261006-DxdrJe/review/index.html'>Existing three-bank comparison</a> (retained older executable, not a new matched benchmark).</p><img src='diagnostics/pair-contact.png'></details>"
    gpu = report["cost"]["gpu"]
    page += f"<details><summary>Provenance and separate costs</summary><p>Standalone reference/dots GPU p95 {gpu['gpu_p95_ms']:.3f} ms at 960x540 (108 measured spans after 12 warmup). This is presentation-only, not total GUI frame time or concurrent CUDA performance. Caption encoding is logged separately; raw capture wall includes readback and encoding. No solver ran for this page. The previously accepted shared-GPU slowdown still applies to live mode.</p><p><a href='manifest.json'>Exact inputs, commands and frame timelines</a> | <a href='protocol.json'>Frozen capture plan</a> | <a href='integrity.json'>Artifact hashes</a></p></details>"
    page += "<p>Use run_mountain_rain_demo.py replay / live / banks locally; --print-command is safe remotely. Built-in hillside launcher remains a simpler, different simulation, not a substitute for these results. Automated checks passed; owner visual acceptance is deferred.</p>"
    ref.write_text_exclusive(out / "index.html", page)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", required=True, type=Path)
    a = p.parse_args()
    out = demo.fresh_output(a.out)
    report = capture(out)
    print(f"Remote review PASS: {out / 'index.html'}; GPU p95 {report['cost']['gpu']['gpu_p95_ms']:.3f} ms", flush=True)


if __name__ == "__main__":
    main()
