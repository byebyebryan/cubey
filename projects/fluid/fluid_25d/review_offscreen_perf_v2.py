#!/usr/bin/env python3
"""Derive V2 summaries and an HTML report, preserving raw trial receipts."""
from __future__ import annotations

import argparse
import html
import json
from pathlib import Path
import statistics

from analyze_live_perf_terrain_v1 import resource_trend, display_age_trend

GPU = "gpu:fluid_25d native presentation total"
AGE = "fluid_25d.external.publication_freshness_s"


def spread(values):
    values = [v for v in values if v is not None]
    return {"n": len(values), "median": statistics.median(values),
            "min": min(values), "max": max(values)} if values else {"n": 0}


def measure(row):
    profile = row.get("profile", {})
    passes = profile.get("passes_ms", {})
    metrics = profile.get("metrics", {})
    host = "headless" if row["host"] == "offscreen" else "host"
    values = {"pace_x": row.get("physical_wall_ratio"),
              "loop_p95_ms": profile.get("frame_ms", {}).get("p95"),
              "service_p95_ms": passes.get("cpu:" + host + ".frame_service", {}).get("p95"),
              "gpu_p95_ms": passes.get(GPU, {}).get("p95"),
              "field_age_p95_s": metrics.get(AGE, {}).get("p95"),
              "advancing_age_p95_s": metrics.get("fluid_25d.external.advancing_state_age_s", {}).get("p95"),
              "whitewater_age_p95_s": metrics.get("fluid_25d.external.whitewater_source_age_s", {}).get("p95"),
              "peak_owned_rss_mib": max((s.get("viewer_rss_mib", 0) + s.get("worker_rss_mib", 0)
                                         for s in row.get("samples", [])), default=0)}
    for label in ("slot_wait", "owner_dispatch_wait", "queue_submit", "present", "intentional_pacing"):
        values[label + "_p95_ms"] = passes.get("cpu:" + host + "." + label, {}).get("p95")
    values["upload_p95_ms"] = passes.get("cpu:fluid25d.record_field_upload", {}).get("p95")
    return values


def checks(row, directory):
    values = measure(row)
    log = directory / "viewer.log"
    raw = log.read_text(errors="replace") if log.exists() else ""
    completion = row.get("completion") or {}
    worker = row["mode"] != "renderer-only"
    viewer = row["mode"] != "worker-only"
    return {"completed": row["status"] == "completed" and not row.get("errors"),
            "inputs_unchanged": row["input_identity"] == row["input_identity_after"],
            "native_inputs_unchanged": bool(completion.get("inputs_unchanged") and completion.get("reference_inputs_unchanged")) if worker else None,
            "owned_cleanup": bool(row.get("cleanup")) and all(not c.get("active_group_remaining") and not c.get("escalated") and c.get("exit_code") == 0 for c in row["cleanup"]),
            "actual_expected_backend": ("headless_render_only_ready:" in raw and "readback=0 encode=0" in raw if row["host"] == "offscreen" else "cubey_window_backend: wayland" in raw) if viewer else None,
            "actual_expected_device": "NVIDIA GeForce RTX 5070 Ti" in raw if viewer else None,
            "viewer_error": any(s in raw for s in ("VIEWER ERROR", "backend adapter:", "external service publication is stale", "no changed valid service publication", "vulkan validation error")) if viewer else None,
            "service_p95_within_16_7ms": values["service_p95_ms"] <= 16.7 if values["service_p95_ms"] is not None else None,
            "pace_at_least_54x": values["pace_x"] >= 54 if values["pace_x"] is not None else None,
            "field_age_p95_within_1s": values["field_age_p95_s"] <= 1 if values["field_age_p95_s"] is not None else None}


def analyze(root):
    trials, groups, soaks = [], {}, {}
    for path in sorted(root.glob("*/*/result.json")):
        phase = path.parent.parent.name
        if not phase.startswith(("primary-", "paced-", "rolling-confirmation", "held-", "soak-")):
            continue
        row = json.loads(path.read_text())
        values = measure(row)
        relative = str(path.parent.relative_to(root))
        key = (phase, row["case"], row["host"], row["mode"])
        groups.setdefault(key, []).append(values)
        trials.append({"directory": relative, "phase": phase, "case": row["case"],
                       "host": row["host"], "mode": row["mode"], "repeat": row["repeat"],
                       "status": row["status"], "errors": row.get("errors", []),
                       "checks": checks(row, path.parent), "measures": values,
                       "physical_range_s": [row.get("initial_physical_s"), row.get("measurement_final_physical_s")],
                       "app_sha256": row["app_sha256"]})
        if phase.startswith("soak-"):
            soaks[relative] = {"resources": resource_trend(row), "display_age": display_age_trend(path.parent, row)}
    phases = [{"phase": phase, "case": case, "host": host, "mode": mode,
               "measures": {key: spread([r[key] for r in rows]) for key in rows[0]}}
              for (phase, case, host, mode), rows in groups.items()]
    result = {"schema": "cubey.fluid25d.offscreen-perf-analysis.v2",
              "scope": "median and range of per-run p95 values, not pooled frames or confidence intervals",
              "expected_primary_trials": 42, "expected_total_trials": 56,
              "observed_primary_trials": sum(t["phase"].startswith("primary-") for t in trials),
              "observed_total_trials": len(trials), "trials": trials, "phases": phases, "soaks": soaks}
    (root / "analysis.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def escaped(value):
    return html.escape(str(value))


def described(value, decimals=2):
    if not value.get("n"):
        return "—"
    return f'{value["median"]:.{decimals}f} [{value["min"]:.{decimals}f}–{value["max"]:.{decimals}f}] / {value["n"]}'


def table(headings, rows):
    return '<div class="scroll"><table><thead><tr>' + ''.join(f'<th>{escaped(v)}</th>' for v in headings) + \
        '</tr></thead><tbody>' + ''.join('<tr>' + ''.join(f'<td>{v}</td>' for v in row) + '</tr>' for row in rows) + '</tbody></table></div>'


def render(root):
    analysis = analyze(root)
    sections = []
    for title, prefix in (("Primary controls · 1080p, unpaced", "primary-"),
                          ("Operational checks · paced to 60 Hz", "paced-"),
                          ("Rolling-terrain confirmation", "rolling-confirmation"),
                          ("Held 1k field · output-resolution scaling", "held-"),
                          ("Ten-minute continuous live offscreen run", "soak-")):
        rows = []
        for phase in analysis["phases"]:
            if not phase["phase"].startswith(prefix):
                continue
            m = phase["measures"]
            rows.append([escaped(phase[k]) for k in ("case", "host", "mode")] +
                        [escaped(described(m[k])) for k in ("pace_x", "service_p95_ms", "loop_p95_ms", "gpu_p95_ms", "field_age_p95_s", "peak_owned_rss_mib")])
        sections.append(f'<section><h2>{escaped(title)}</h2>' + table(
            ["Case", "Host", "Workload", "Physical/wall ×", "Service p95 ms", "Loop p95 ms", "GPU p95 ms", "Field age p95 s", "Peak owned RSS MiB"], rows) + '</section>')
    attribution = []
    for phase in analysis["phases"]:
        if not phase["phase"].startswith("primary-") or phase["mode"] == "worker-only":
            continue
        attribution.append([escaped(phase[k]) for k in ("case", "host", "mode")] +
                           [escaped(described(phase["measures"][k])) for k in ("slot_wait_p95_ms", "owner_dispatch_wait_p95_ms", "queue_submit_p95_ms", "present_p95_ms", "upload_p95_ms")])
    sections.append('<details><summary>Attribution · waits, dispatch, submit, presentation and upload</summary><p>These are CPU wall-time spans, including waits, not CPU cycles. GPU work is shown above. Overlapping spans and independent percentiles must not be added or subtracted.</p>' + table(
        ["Case", "Host", "Workload", "Slot wait p95 ms", "Owner dispatch wait p95 ms", "Queue submit p95 ms", "Present p95 ms", "Sampled upload p95 ms"], attribution) + '</details>')
    checks_table = []
    for trial in analysis["trials"]:
        checks_table.append([f'<a href="{escaped(trial["directory"])}/result.json">{escaped(trial["directory"])}</a>', escaped(trial["status"]),
            escaped(str(trial["physical_range_s"])), escaped(json.dumps(trial["checks"], sort_keys=True))])
    verdict = root / "verdict.html"
    overview = verdict.read_text() if verdict.exists() else '<p>The campaign is still in progress. Results below are provisional; no performance acceptance or default change is implied.</p>'
    gallery = root / "captures/gallery.html"
    gallery_html = gallery.read_text() if gallery.exists() else ''
    page = '''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Fluid 2.5D · corrected Vulkan performance study</title><style>
*{box-sizing:border-box}body{margin:0;background:#121c25;color:#e5edf3;font:16px/1.55 system-ui,sans-serif}main{max-width:1500px;margin:auto;padding:36px 24px 80px}h1{font-size:34px;line-height:1.2}h2{font-size:23px;margin:0 0 12px}a{color:#9bd4f4}p,li{max-width:1000px;color:#bccbd6}section{padding:24px 0;border-top:1px solid #354957}.scroll{overflow:auto}table{border-collapse:collapse;width:100%;font-size:13px}th,td{text-align:left;padding:10px 12px;border-bottom:1px solid #354957;vertical-align:top}th{color:#95bfda}td:last-child{overflow-wrap:anywhere}img{max-width:100%;height:auto}article{margin:20px 0}details{margin:20px 0}.tag{font-size:13px;color:#8ab9d4}.callout{padding:18px;border-left:3px solid #6da9ca;background:#1a2b38}code{overflow-wrap:anywhere}</style></head><body><main>
<p class="tag">Cubey · performance study, not an automatic demo-default promotion</p><h1>Fluid 2.5D: real offscreen Vulkan and native Wayland</h1>'''
    page += '<div class="callout">' + overview + '</div>'
    page += f'<p>{analysis["observed_primary_trials"]}/42 primary trials; {analysis["observed_total_trials"]}/56 total campaign trials. <a href="analysis.json">Derived statistics</a> · <a href="campaign-progress.json">Campaign receipt</a> · <a href="../live-perf-terrain-20261010-Dd6K3h/">Original V1 study</a></p>'
    page += '''<section><h2>How to read this</h2><p>Each cell is the median [minimum–maximum] of per-run summaries, followed by the repetition count. Primary runs use three 45-wall-second windows after five seconds of warmup and at least 1800 physical seconds of solver prewarm. Worker-only is measured once per configuration, not duplicated for a rendering host. Held renderer-only and held renderer+worker display the identical mature recorded field. Live fields evolve, so their geometry and shader workload are not identical to the held controls.</p>
<p><strong>Service time</strong> excludes deliberate render pacing. <strong>Loop time</strong> includes it; a 60 Hz loop near 16.7 ms is expected even if service takes only a few milliseconds. <strong>GPU time</strong> is timestamped scene work. Native Wayland also includes UI/swapchain/presentation, but these are application-loop timings, not monitor scanout or displayed FPS. Offscreen performs no readback or encoding and needs no display server. Worker speed remains requested/capped at 60×: meeting that request is not an uncapped solver-capacity measurement.</p>
<p>The same 16.384 km mountain patch is simulated at 1024²/16 m and 2048²/8 m. The finer grid interpolates the original 30 m terrain; it does not invent new terrain detail. Both use dry start, 512 mm/h rain, Manning 0.05 and existing open boundaries. Scenic Macro, daylight, rough water, B-spline 2×, bank noise off, subtle foam/flecks, rain and trails are retained. No solver, forcing, material or product default was changed.</p>
<p>Held fields are exactly the prior saved endpoints: 7243.206543 physical s at 1k and 4767.136719 s at 2k. Held renderer and contention comparisons within each configuration are matched; comparisons between grids also include different water states and are not an isolated tessellation-cost ratio. Live physical progress ranges are retained in each trial receipt. <a href="host.json">Host/build identity</a></p></section>'''
    page += gallery_html + ''.join(sections)
    trends = []
    for name, soak in analysis["soaks"].items():
        for key in ("worker_rss_mib", "viewer_rss_mib", "publication_age_s"):
            item = soak["resources"][key]
            trends.append([escaped(name), escaped(key)] + [escaped("—" if v is None else f"{v:.3f}") for v in
                (item["early"].get("median"), item["late"].get("median"), item["median_change"], item["peak"])])
    page += '<section><h2>Soak trends</h2><p>Owned-process RSS medians compare 60–180 s with 480–600 s; startup allocation is excluded. Ten minutes is a bounded stability check, not an indefinite leak-free claim. Full resource/source-age receipts are in analysis.json.</p>' + table(
        ["Trial", "Measure", "Early median", "Late median", "Median change", "Peak"], trends) + '</section>'
    page += '''<section><h2>Evidence limits</h2><p>Frame profiles sample every tenth frame (maximum 12000 records). Upload timing is consequently sampled; asynchronous reader and whitewater-classification metrics repeat the last completed job and are not independent per-job distributions. NVIDIA utilization, VRAM, power and temperature samples cover the whole device, including the desktop, not individual processes. Desktop workloads, clocks and compositor settings are not controlled. No global display settings were changed; owned native windows were resized to an actual matched framebuffer.</p>
<p>V1 used Xvfb/X11 presentation with hardware Vulkan on the RTX 5070 Ti. Its frame-loop figures are not native Wayland performance and are retained without rewriting them. This corrected study does not run Xvfb, XWayland or a software Vulkan fallback. Selected 1440p/4K output scaling is offscreen-only: a fully visible native 4K window does not fit the current 5120×1440 desktop output.</p>
<p>Controls were validated separately from steady-state timing. Passive reset following requires the matching command/acknowledgement to remain available until the viewer observes its applied generation; this is not a multi-controller or missed-reset-history protocol. Ordinary stale-publication guards remain unchanged. Captures below/above are separate from primary timing windows. Human motion/monitor acceptance remains separate.</p></section>'''
    page += '<details><summary>Individual receipts and checks</summary>' + table(["Trial receipt", "Outcome", "Physical-time range s", "Checks (false is not silently promoted)"], checks_table) + '</details></main></body></html>'
    (root / "index.html").write_text(page)
    return analysis


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--study", required=True, type=Path)
    args = parser.parse_args()
    result = render(args.study.resolve(strict=True))
    print(json.dumps({"primary_trials": result["observed_primary_trials"], "total_trials": result["observed_total_trials"]}))


if __name__ == "__main__":
    main()
