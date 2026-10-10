#!/usr/bin/env python3
"""Read-only study summarizer; writes only a report inside the supplied study."""
from __future__ import annotations

import argparse
import html
import json
from pathlib import Path
import statistics
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
    viewer_failure = (any(marker in text for marker in ("VIEWER ERROR", "no changed valid service publication", "external service publication is stale"))
                      if log.exists() else None)
    completion = row.get("completion") or {}
    cleanup = row.get("cleanup")
    clean = (all(not c.get("active_group_remaining") and not c.get("escalated")
                 for c in cleanup) if cleanup else None)
    warm_ack = (row.get("warm_ack_ms") or {}).get("p95")
    return {"pace_at_least_54x": (row.get("physical_wall_ratio") or 0) >= 54 if worker else None,
            "frame_p95_at_most_16_7ms": frame <= 16.7 if frame is not None else None,
            "field_freshness_p95_at_most_1s": fresh <= 1 if fresh is not None else None,
            "advancing_state_p95_at_most_1s": advancing <= 1 if advancing is not None else None,
            # A held renderer-plus-worker control intentionally displays a
            # different physical state than its computing contention worker.
            "display_near_worker_endpoint": abs(end - shown) <= 180 if row["mode"] == "live" and end is not None and shown is not None else None,
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


def coverage_matrix(rows):
    cells = []
    configurations = (("patch-512-32m", "512² / 32 m", "16.384 km"),
                      ("patch-1024-16m", "1024² / 16 m", "16.384 km"),
                      ("patch-2048-8m", "2048² / 8 m", "16.384 km"),
                      ("small-512-16m", "512² / 16 m", "8.192 km"),
                      ("large-2048-16m", "2048² / 16 m", "32.768 km"))
    by_case = {(r["case"], r["mode"]): r for r in rows}
    for site in ("mountain", "alpine", "rolling"):
        for config, grid, footprint in configurations:
            name = site + "-" + config
            worker = by_case.get((name, "worker-only"), {})
            live = by_case.get((name, "live"), {})
            fields = (site, grid, footprint, number(worker.get("physical_wall_ratio")),
                      number(live.get("physical_wall_ratio")),
                      number(live.get("measurement_final_physical_s"), 0),
                      number((live.get("profile") or {}).get("frame_ms", {}).get("p95")),
                      number(metric(live, "fluid_25d.external.publication_freshness_s"), 2),
                      live.get("status", "pending"))
            cells.append('<tr>' + ''.join(f'<td>{e(value)}</td>' for value in fields) + '</tr>')
    headings = ("Site", "Grid / spacing", "Square footprint", "Worker pace ×", "Live pace ×",
                "Live physical s", "Frame p95 ms", "Field age p95 s", "Live outcome")
    return '<div class="scroll"><table><thead><tr>' + ''.join(f'<th>{e(h)}</th>' for h in headings) + \
        '</tr></thead><tbody>' + ''.join(cells) + '</tbody></table></div>'


def gallery(row):
    last = row["captures"][-1]
    src = row["_directory"] + "/" + last["path"]
    video = row.get("video")
    motion = (f'<video controls preload="none" poster="{e(src)}"><source src="{e(row["_directory"] + "/" + video)}" type="video/mp4"></video>'
              if video else f'<a href="{e(src)}"><img loading="lazy" src="{e(src)}"></a>')
    description = ("Actual live Vulkan GUI; worker running." if row["mode"] == "live" else
                   "Actual Vulkan GUI held on an exact recorded state; not a running water simulation.")
    return f'<article><h3>{e(row["case"])} / {e(row["_directory"].split("/")[0])}</h3>{motion}' + \
        f'<p>{last["observed_physical_s"]:.0f} physical s. {description} ' + \
        f'<a href="{e(src)}">Full capture</a> · <a href="{e(row["_directory"] + "/result.json")}">Evidence</a></p></article>'


def isolation_table(rows):
    output = []
    def key(row):
        return (row.get("_directory", "unlabelled/").split("/")[0], row["case"])
    for phase, case in sorted({key(r) for r in rows if r["mode"] == "renderer-only"}):
        group = [r for r in rows if key(r) == (phase, case) and r["mode"].startswith("renderer")]
        for label, get in (("GPU render p95 ms", lambda r: (r.get("profile") or {}).get(
                "passes_ms", {}).get("gpu:fluid_25d native presentation total", {}).get("p95")),
                ("Frame-loop p95 ms", lambda r: (r.get("profile") or {}).get("frame_ms", {}).get("p95"))):
            values = {mode: [value for r in group if r["mode"] == mode and
                            (value := get(r)) is not None]
                      for mode in ("renderer-only", "renderer-plus-worker")}
            def describe(items):
                return (f'{statistics.median(items):.2f} [{min(items):.2f}–{max(items):.2f}] / n={len(items)}'
                        if items else "pending")
            base, contended = values["renderer-only"], values["renderer-plus-worker"]
            change = (f'{(statistics.median(contended)/statistics.median(base)-1)*100:+.1f}%'
                      if base and contended and statistics.median(base) > 0 else "—")
            output.append('<tr>' + ''.join(f'<td>{e(v)}</td>' for v in
                (phase + " / " + case, label, describe(base), describe(contended), change)) + '</tr>')
    if not output:
        return ''
    return '<section><h2>Shared-GPU isolation · identical held render state</h2><p>Median of per-run p95 values; brackets show the run-to-run range, not a confidence interval. The added worker includes CUDA computation and CPU publication. This is contention, not isolated bridge overhead.</p><div class="scroll"><table><thead><tr>' + \
        ''.join(f'<th>{h}</th>' for h in ("Case", "Measure", "Renderer only", "Same renderer + worker", "Median change")) + \
        '</tr></thead><tbody>' + ''.join(output) + '</tbody></table></div></section>'


def build_table(rows):
    output = []
    for case in ("mountain-patch-1024-16m", "mountain-patch-2048-8m"):
        for phase in ("coverage", "release-" + case):
            group = [r for r in rows if r["case"] == case and r["mode"] == "live" and
                     r["_directory"].split("/")[0] == phase and r.get("profile")]
            if not group:
                continue
            def describe(get, decimals=2):
                values = [value for r in group if (value := get(r)) is not None]
                return (f'{statistics.median(values):.{decimals}f} [{min(values):.{decimals}f}–{max(values):.{decimals}f}]'
                        if values else "—")
            fields = (case, ("Debug baseline" if phase == "coverage" else "Release experiment") + f" / n={len(group)}",
                      describe(lambda r: r.get("physical_wall_ratio"), 1),
                      describe(lambda r: r["profile"].get("frame_ms", {}).get("p95"), 1),
                      describe(lambda r: r["profile"].get("passes_ms", {}).get(
                          "gpu:fluid_25d native presentation total", {}).get("p95")),
                      describe(lambda r: metric(r, "fluid_25d.external.publication_freshness_s")),
                      describe(lambda r: metric(r, "fluid_25d.whitewater.classification_cpu_ms"), 1),
                      describe(lambda r: metric(r, "fluid_25d.external.whitewater_source_age_s")))
            output.append('<tr>' + ''.join(f'<td>{e(value)}</td>' for value in fields) + '</tr>')
    return '<section><h2>Build-only experiment · live fields</h2><p>Median [run-to-run range] of per-run p95 values; pace uses the per-run physical/wall ratio. Debug coverage has one run per case, not a repeated baseline. Native extension, numerical inputs and visual settings are unchanged.</p><div class="scroll"><table><thead><tr>' + \
        ''.join(f'<th>{h}</th>' for h in ("Case", "Artifact / repeats", "Pace ×", "Frame p95 ms", "GPU p95 ms", "Field age p95 s", "Whitewater classification p95 ms", "Whitewater source age p95 s")) + \
        '</tr></thead><tbody>' + ''.join(output) + '</tbody></table></div></section>'


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
        headings.append(f'<details><summary>{e(group)} · {len(selected)} retained trials</summary><div class="scroll"><table><thead><tr>' +
            ''.join(f'<th>{x}</th>' for x in ("Site / grid", "Mode", "Outcome", "Physical s", "Wall s", "Pace ×",
                "Frame p95 ms", "GPU p95 ms", "End frame p95 ms", "Freshness p95 s", "Fleck age p95 s", "Warm ack p95 ms", "RSS peak MiB", "Evidence")) +
            '</tr></thead><tbody>' + ''.join(table) + '</tbody></table></div></details>')
    coverage = [r for r in rows if r["_directory"].startswith("coverage/")]
    pending = []
    for path in root.glob("*/**/progress.json"):
        if not (path.parent / "result.json").exists():
            value = json.loads(path.read_bytes())
            if "latest" in value:
                pending.append(f'{value["case"]} / {value["mode"]}: {value["latest"]["physical_s"]:.0f} physical seconds, {value["latest"]["wall_s"]:.0f} wall seconds')
    captured = [r for r in rows if r["mode"] == "live" and r.get("captures")]
    representatives = []
    for site in ("mountain", "alpine", "rolling"):
        name = site + "-patch-1024-16m"
        candidates = [r for r in captured if r["case"] == name and
                      r["_directory"].split("/")[0] in ("coverage", "release-" + name)]
        if candidates:
            # Prefer repeat zero of the measured Release artifact; otherwise baseline.
            representatives.append(sorted(candidates, key=lambda r: (
                not r["_directory"].startswith("release-"), r["_directory"]))[0])
    representative_paths = {r["_directory"] for r in representatives}
    galleries = [gallery(r) for r in captured if r["_directory"] not in representative_paths]
    display_captures = [r for r in rows if r["_directory"].startswith("display-") and
                        r["mode"] == "renderer-only" and r.get("captures") and
                        r["_directory"].endswith("-0")]
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
    completion = root / "study-completion.json"
    summary["study_completion"] = json.loads(completion.read_bytes()) if completion.exists() else None
    phases = root / "follow-on/results.json"
    summary["follow_on_phases"] = json.loads(phases.read_bytes()).get("phases", []) if phases.exists() else []
    (root / "overview.json").write_text(json.dumps(summary, indent=2) + "\n")
    source = '<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
    source += '<title>Fluid 2.5D · live performance and terrain study</title><style>'
    source += 'body{font:16px/1.5 system-ui;background:#f5f6f8;color:#203040;max-width:1500px;margin:32px auto;padding:0 22px}h1{font-size:2rem}h2{margin-top:2em}a{color:#145b9a}section,article,details{background:white;border:1px solid #dce1e7;padding:18px;margin:18px 0;border-radius:6px}summary{cursor:pointer;font-weight:600}.scroll{overflow:auto}table{border-collapse:collapse;white-space:nowrap;font-size:13px}th,td{padding:8px;text-align:right;border-bottom:1px solid #eee}th:first-child,td:first-child{text-align:left}video,img{width:100%;height:auto;max-height:800px;object-fit:contain;background:#18232c}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,600px),1fr));gap:18px}.note{border-left:4px solid #709bb9;padding-left:16px}code{font-size:14px}footer{margin:40px 0}</style>'
    source += '<nav><a href="/">All Cubey reports</a> · <a href="overview.json">Summary JSON</a> · <a href="cases/preparation.json">Prepared cases</a></nav>'
    if (root / "environment.json").exists():
        source += '<p><a href="environment.json">Measured host and evidence boundaries</a></p>'
    source += '<h1>Fluid 2.5D: live solver + rendering</h1><p>Three existing Terrain Diffusion sites; five power-of-two grid/footprint configurations. Native SynxFlow unchanged. New options are opt-in; existing defaults stay intact.</p>'
    source += f'<section><h2>Campaign progress</h2><p>{len(coverage)}/30 broad-coverage trials retained. {len(live)} live and {len(solver)} worker-only cases. {summary["timeouts"]} timeouts retained.</p>'
    source += '<p>' + ('Study execution and review complete; human visual acceptance remains deferred.'
                       if summary["study_completion"] else
                       'Study still in progress. Broad coverage is only one phase; Release, isolation, stress, soaks and final review are separate.') + '</p>'
    if summary["follow_on_phases"]:
        last_phase = summary["follow_on_phases"][-1]
        source += f'<p>Last retained follow-on phase: {e(last_phase["phase"])} / {e(last_phase["status"])}. <a href="follow-on/results.json">Phase receipts</a></p>'
    source += '<p>' + e(' · '.join(pending) or 'No active trial found; see results below.') + '</p><p>Reload this page for new results. Detailed repeats, stress and soak results appear as separate sections.</p></section>'
    source += '<div class="note"><p>Read GPU pass time separately from frame-loop time: private X11 presentation can dominate the frame-ending step. No physical-monitor FPS claim. Worker-only includes download/publication; it is not pure CUDA timing.</p><p>All coverage starts dry under continuous 512 mm/h rain and asks for 60× physical time. 54× is the proposed pace target. A 600-wall-second timeout is an incomplete two-hour horizon, not a solver fallback. 8 m spacing samples the same original 30 m terrain; it adds no new terrain detail.</p><p>Static isolation controls contain exactly two captured physical states and remain paused on the final state. They are not smooth recorded simulation replays. Match renderer-only against renderer-plus-worker to assess contention.</p></div>'
    source += '<p>“Completed” means the bounded trial finished, not that all targets passed. The <a href="overview.json">summary JSON</a> separately checks pace, frame time, field/advancing freshness, endpoint lag, viewer errors and clean input-preserving shutdown. Debug baseline and labelled Release follow-on are separate artifacts.</p>'
    source += '<h2>Start here: the three 1k terrain options</h2><p>Same 16.384 km square size, same forcing. These are full-scene overviews, not close-up bank-quality comparisons. The terrain and water amounts differ; those differences are the point of the site options.</p><div class="grid">' + ''.join(gallery(r) for r in representatives) + '</div>'
    source += '<section><h2>Broad coverage at a glance · Debug baseline</h2>' + coverage_matrix(coverage) + '</section>'
    source += '<h2>How quickly does the water simulation advance?</h2><div class="grid">' + ''.join(progress_plot(coverage, site) for site in ("mountain", "alpine", "rolling")) + '</div>'
    source += build_table(rows)
    source += isolation_table(rows)
    if display_captures:
        source += '<details><summary>B-spline display-density experiment · held-state comparisons</summary><p>Existing B-spline reconstruction is unchanged. Only display tessellation changes between 2× and 1×; native fields and terrain are the same. These held-state views do not demonstrate live simulation motion.</p><div class="grid">' + ''.join(gallery(r) for r in display_captures) + '</div></details>'
    source += '<h2>Detailed timing phases</h2><p>Expand a phase for individual repetitions, resource bounds and the raw evidence links.</p>' + ''.join(headings)
    source += '<details><summary>All other live captures · ' + str(len(galleries)) + '</summary><p>Water color is artistic shading, not a depth legend. Whitewater/trails are rendering cues, not tracked water parcels. The side panel identifies LIVE, rainfall, physical time and the prepared terrain/grid.</p><div class="grid">' + ''.join(galleries) + '</div></details>'
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
