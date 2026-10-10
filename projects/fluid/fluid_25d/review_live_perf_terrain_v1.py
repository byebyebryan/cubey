#!/usr/bin/env python3
"""Read-only study summarizer; writes only a report inside the supplied study."""
from __future__ import annotations

import argparse
import html
import json
from pathlib import Path
import time


def e(value):
    return html.escape(str(value))


def number(value, decimals=1):
    return "—" if value is None else f"{value:.{decimals}f}"


def metric(row, key, stat="p95"):
    return (row.get("profile") or {}).get("metrics", {}).get(key, {}).get(stat)


def gates(row, directory):
    prof = row.get("profile") or {}
    frame = prof.get("frame_ms", {}).get("p95")
    fresh = metric(row, "fluid_25d.external.publication_freshness_s")
    advancing = metric(row, "fluid_25d.external.advancing_state_age_s")
    shown = metric(row, "fluid_25d.external.physical_time_s", "max")
    end = row.get("measurement_final_physical_s")
    worker = row["mode"] != "renderer-only"
    viewer = row["mode"] != "worker-only"
    log = directory / "viewer.log"
    text = log.read_text(errors="replace") if log.exists() else ""
    viewer_failure = any(marker in text for marker in ("VIEWER ERROR", "no changed valid service publication", "external service publication is stale"))
    completion = row.get("completion") or {}
    cleanup = row.get("cleanup")
    clean = (all(not c.get("active_group_remaining") and not c.get("escalated")
                 for c in cleanup) if cleanup else None)
    warm_ack = (row.get("warm_ack_ms") or {}).get("p95")
    return {"pace_at_least_54x": (row.get("physical_wall_ratio") or 0) >= 54 if worker else None,
            "frame_p95_at_most_16_7ms": frame <= 16.7 if frame is not None else None,
            "field_freshness_p95_at_most_1s": fresh <= 1 if fresh is not None else None,
            "advancing_state_p95_at_most_1s": advancing <= 1 if advancing is not None else None,
            "display_near_worker_endpoint": abs(end - shown) <= 180 if end is not None and shown is not None else None,
            "warm_ack_p95_at_most_250ms": warm_ack <= 250 if warm_ack is not None else None,
            "viewer_reported_failure": viewer_failure if viewer else None,
            "immutable_native_inputs": completion.get("inputs_unchanged") and completion.get("reference_inputs_unchanged") if worker else None,
            "clean_owned_shutdown": clean,
            "outcome_is_not_automatic_acceptance": True}


def progress_plot(rows, site):
    colors = {"patch-512-32m": "#587539", "patch-1024-16m": "#237ead", "patch-2048-8m": "#895892"}
    lines = []
    for row in rows:
        for config, color in colors.items():
            if row["case"] != site + "-" + config:
                continue
            points = [(0, row.get("measurement_initial_physical_s", 0))] + [
                (s["wall_s"], s["physical_s"]) for s in row.get("samples", [])]
            xy = " ".join(f'{45 + min(x,600)/600*600:.1f},{215-min(y,7200)/7200*180:.1f}' for x, y in points)
            dash = ' stroke-dasharray="5 4"' if row["mode"] == "worker-only" else ''
            lines.append(f'<polyline fill="none" stroke="{color}" stroke-width="2"{dash} points="{xy}"><title>{e(row["case"])} / {e(row["mode"])}</title></polyline>')
    if not lines:
        return ''
    return f'<article><h3>{e(site)} · same 16.384 km patch</h3><svg viewBox="0 0 680 260" role="img" aria-label="Physical simulation progress versus wall time">' + \
        '<path d="M45 35V215H645" fill="none" stroke="#80909f"/><path d="M45 215L165 35" fill="none" stroke="#999" stroke-dasharray="2 3"/><text x="45" y="250" font-size="12">0</text><text x="610" y="250" font-size="12">600 wall s</text><text x="45" y="20" font-size="12">7200 physical s (two hours)</text><text x="178" y="45" font-size="12">60× request</text>' + ''.join(lines) + '</svg><p>Green: 512/32 m · blue: 1024/16 m · purple: 2048/8 m. Dashed: worker-only; solid: live. Traces end when their bounded run ends.</p></article>'


def render(root):
    rows = []
    for path in sorted(root.glob("*/**/result.json")):
        if path.parent.parent.name == "smoke":
            continue
        data = json.loads(path.read_bytes())
        if "mode" not in data or "case" not in data:
            continue
        data["_directory"] = str(path.parent.relative_to(root))
        data["_gates"] = gates(data, path.parent)
        rows.append(data)
    headings = []
    for group in sorted({r["_directory"].split("/")[0] for r in rows}):
        selected = [r for r in rows if r["_directory"].split("/")[0] == group]
        table = []
        for row in selected:
            prof = row.get("profile") or {}
            rss = max((r["worker_rss_mib"] + r["viewer_rss_mib"] for r in row.get("samples", [])), default=0)
            gpu = prof.get("passes_ms", {}).get("gpu:fluid_25d native presentation total", {}).get("p95")
            end = prof.get("passes_ms", {}).get("cpu:host.end_frame", {}).get("p95")
            fields = [row["case"], row["mode"], row["status"],
                number(row.get("measurement_final_physical_s"), 0), number(row.get("measurement_wall_s")),
                number(row.get("physical_wall_ratio")), number(prof.get("frame_ms", {}).get("p95")),
                number(gpu, 2), number(end, 2), number(metric(row, "fluid_25d.external.publication_freshness_s"), 2),
                number(metric(row, "fluid_25d.external.whitewater_source_age_s"), 2),
                number((row.get("warm_ack_ms") or {}).get("p95")), number(rss, 0)]
            link = row["_directory"] + "/result.json"
            cells = "".join(f"<td>{e(value)}</td>" for value in fields)
            table.append(f'<tr>{cells}<td><a href="{e(link)}">JSON</a></td></tr>')
        headings.append(f'<section><h2>{e(group)}</h2><div class="scroll"><table><thead><tr>' +
            ''.join(f'<th>{x}</th>' for x in ("Site / grid", "Mode", "Outcome", "Physical s", "Wall s", "Pace ×",
                "Frame p95 ms", "GPU p95 ms", "End frame p95 ms", "Freshness p95 s", "Fleck age p95 s", "Warm ack p95 ms", "RSS peak MiB", "Evidence")) +
            '</tr></thead><tbody>' + ''.join(table) + '</tbody></table></div></section>')
    coverage = [r for r in rows if r["_directory"].startswith("coverage/")]
    pending = []
    for path in root.glob("*/**/progress.json"):
        if not (path.parent / "result.json").exists():
            value = json.loads(path.read_bytes())
            if "latest" in value:
                pending.append(f'{value["case"]} / {value["mode"]}: {value["latest"]["physical_s"]:.0f} physical seconds, {value["latest"]["wall_s"]:.0f} wall seconds')
    galleries = []
    for row in rows:
        if row["mode"] != "live" or not row.get("captures"):
            continue
        last = row["captures"][-1]
        src = row["_directory"] + "/" + last["path"]
        video = row.get("video")
        motion = (f'<video controls preload="none" poster="{e(src)}"><source src="{e(row["_directory"] + "/" + video)}" type="video/mp4"></video>'
                  if video else f'<a href="{e(src)}"><img loading="lazy" src="{e(src)}"></a>')
        galleries.append(f'<article><h3>{e(row["case"])} / {e(row["_directory"].split("/")[0])}</h3>{motion}'
            f'<p>{last["observed_physical_s"]:.0f} physical s. Actual live Vulkan GUI; worker running. '
            f'<a href="{e(src)}">Full capture</a> · <a href="{e(row["_directory"] + "/result.json")}">Evidence</a></p></article>')
    live = [r for r in coverage if r["mode"] == "live"]
    solver = [r for r in coverage if r["mode"] == "worker-only"]
    summary = {"coverage_completed_trials": len(coverage), "total_coverage_trials": 30,
        "live_cases": len(live), "worker_cases": len(solver),
        "live_at_least_54x": sum((r.get("physical_wall_ratio") or 0) >= 54 for r in live),
        "timeouts": sum(r["status"] == "timeout" for r in rows),
        "errors": [{"directory": r["_directory"], "errors": r["errors"]} for r in rows if r.get("errors")],
        "gates": {r["_directory"]: r["_gates"] for r in rows},
        "written_unix_s": time.time(), "human_visual_acceptance": "deferred",
        "measurement_scope": "private-X11 real GUI; sampled profiles; not physical-monitor scanout or calibrated hydrology"}
    (root / "overview.json").write_text(json.dumps(summary, indent=2) + "\n")
    source = '<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
    source += '<title>Fluid 2.5D · live performance and terrain study</title><style>'
    source += 'body{font:16px/1.5 system-ui;background:#f5f6f8;color:#203040;max-width:1500px;margin:32px auto;padding:0 22px}h1{font-size:2rem}h2{margin-top:2em}a{color:#145b9a}section,article{background:white;border:1px solid #dce1e7;padding:18px;margin:18px 0;border-radius:6px}.scroll{overflow:auto}table{border-collapse:collapse;white-space:nowrap;font-size:13px}th,td{padding:8px;text-align:right;border-bottom:1px solid #eee}th:first-child,td:first-child{text-align:left}video,img{width:100%;height:auto;max-height:800px;object-fit:contain;background:#18232c}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,600px),1fr));gap:18px}.note{border-left:4px solid #709bb9;padding-left:16px}code{font-size:14px}footer{margin:40px 0}</style>'
    source += '<nav><a href="/">All Cubey reports</a> · <a href="overview.json">Summary JSON</a> · <a href="cases/preparation.json">Prepared cases</a></nav>'
    source += '<h1>Fluid 2.5D: live solver + rendering</h1><p>Three existing Terrain Diffusion sites; five power-of-two grid/footprint configurations. Native SynxFlow unchanged. New options are opt-in; existing defaults stay intact.</p>'
    source += f'<section><h2>Campaign progress</h2><p>{len(coverage)}/30 broad-coverage trials retained. {len(live)} live and {len(solver)} worker-only cases. {summary["timeouts"]} timeouts retained.</p>'
    source += '<p>' + e(' · '.join(pending) or 'No active trial found; see results below.') + '</p><p>Reload this page for new results. Detailed repeats, stress and soak results appear as separate sections.</p></section>'
    source += '<div class="note"><p>Read GPU pass time separately from frame-loop time: private X11 presentation can dominate the frame-ending step. No physical-monitor FPS claim. Worker-only includes download/publication; it is not pure CUDA timing.</p><p>All coverage starts dry under continuous 512 mm/h rain and asks for 60× physical time. 54× is the proposed pace target. A 600-wall-second timeout is an incomplete two-hour horizon, not a solver fallback. 8 m spacing samples the same original 30 m terrain; it adds no new terrain detail.</p><p>Static isolation controls contain exactly two captured physical states and remain paused on the final state. They are not smooth recorded simulation replays. Match renderer-only against renderer-plus-worker to assess contention.</p></div>'
    source += '<p>“Completed” means the bounded trial finished, not that all targets passed. The <a href="overview.json">summary JSON</a> separately checks pace, frame time, field/advancing freshness, endpoint lag, viewer errors and clean input-preserving shutdown. Debug baseline and labelled Release follow-on are separate artifacts.</p>'
    source += '<h2>How quickly does the water simulation advance?</h2><div class="grid">' + ''.join(progress_plot(coverage, site) for site in ("mountain", "alpine", "rolling")) + '</div>'
    source += ''.join(headings)
    source += '<h2>Live captures</h2><p>Water color is artistic shading, not a depth legend. Whitewater/trails are rendering cues, not tracked water parcels. The side panel identifies LIVE, rainfall, physical time and the prepared terrain/grid.</p><div class="grid">' + ''.join(galleries) + '</div>'
    source += '<footer>Generated from retained trial JSON, sampled CSV profiles and scoped GUI captures. Numerical parity and human visual acceptance are separate checks.</footer></html>'
    (root / "index.html").write_text(source)
    print(json.dumps(summary), flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--study", required=True, type=Path)
    p.add_argument("--watch", action="store_true")
    a = p.parse_args()
    root = a.study.resolve(strict=True)
    while True:
        render(root)
        if not a.watch:
            return
        time.sleep(15)


if __name__ == "__main__":
    main()
