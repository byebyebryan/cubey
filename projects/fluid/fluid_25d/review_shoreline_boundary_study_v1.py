#!/usr/bin/env python3
"""Recorded-terrain checks and compact review for the offline boundary study.

This does not import coverage into Cubey, alter native fields, or launch a solver.
The original frozen gate results are never overwritten by this follow-up.
"""
from __future__ import annotations

import argparse
import html
import json
from pathlib import Path
import subprocess
import time

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection
from scipy.ndimage import uniform_filter

import run_shoreline_boundary_study_v1 as study
import run_native_presentation_v1 as reference


def recorded(case: str, timestamp: int):
    directory = reference.INPUT_ROOT / "recordings" / case
    document = json.loads((directory / "recording.json").read_text())
    frame = next(f for f in document["frames"] if f["time_s"] == timestamp)
    path = directory / frame["path"]
    if reference.sha256_file(path) != frame["sha256"]:
        raise ValueError("recording frame hash differs")
    grid = document["grid"]
    shape = (grid["height"],grid["width"])
    data = np.fromfile(path,dtype="<f4")
    if data.size != 3*np.prod(shape):
        raise ValueError("invalid planar recording")
    return data[:np.prod(shape)].reshape(shape).astype(float), {
        "case":case,"time_s":timestamp,"frame_path":str(path.relative_to(reference.ROOT)),
        "frame_sha256":frame["sha256"],"cell_size_m":grid["cell_size_m"]}


def region(h: np.ndarray) -> tuple[int,int,int,int]:
    # Choose a detail window by RAW boundary density, not candidate appearance.
    counts = np.zeros(h.shape)
    for c in study.extract(h,.026,study.METHODS[0]):
        xy = np.rint(c).astype(int)
        np.add.at(counts,(xy[:,1],xy[:,0]),1)
    density = uniform_filter(counts,size=64,mode="constant")
    y,x = np.unravel_index(density.argmax(),density.shape)
    x,y = max(0,min(int(x)-32,h.shape[1]-64)),max(0,min(int(y)-32,h.shape[0]-64))
    return x,y,64,64


def native_panel(path: Path, h: np.ndarray, curves: dict, window: tuple, title: str):
    fig,axes = plt.subplots(1,3,figsize=(15,5),layout="constrained")
    x,y,w,height = window
    for ax,variant in zip(axes,("raw","light","moderate")):
        ax.imshow(np.log10(np.maximum(h,.001)),origin="lower",cmap="Blues",vmin=-3,vmax=1,
                  interpolation="nearest",extent=(-.5,h.shape[1]-.5,-.5,h.shape[0]-.5))
        ax.add_collection(LineCollection(curves[variant],colors="#d64226",linewidths=1.1))
        ax.set(xlim=(x,x+w),ylim=(y,y+height),aspect="equal",title=variant)
        ax.set_xticks(np.arange(x,x+w+1,8))
        ax.set_yticks(np.arange(y,y+height+1,8))
        ax.grid(alpha=.2)
    fig.suptitle(title+"\nRed: 26 mm display contour; depth/grid unchanged. Offline 2D, not Cubey.")
    fig.savefig(path,dpi=120)
    plt.close(fig)


def analysis(out: Path):
    initial = json.loads((out/"checkpoint/identity.json").read_text())
    reference.assert_same_runtime(initial["runtime"],reference.runtime_identity(),"before native offline checks")
    report = json.loads((out/"offline/report.json").read_text())
    protocol = report["protocol"]
    phase = out/"recorded"
    reference.reserve_directory(phase)
    stills = [("rain-on",6000),("rain-off",9600),("rain-off",14400)]
    # Freeze follow-up sampling before any recorded-frame candidate is examined.
    reference.write_json_exclusive(phase/"protocol.json",{
        "stills":stills,"levels_m":protocol["levels_m"],"methods":list(study.METHODS),
        "settings":"Unchanged frozen raw/light/moderate variants; no gate relaxation",
        "roi":"64x64 detail selected from raw 26mm boundary density; same ROI for all variants",
        "clips":[{"case":"rain-on","times_s":list(range(4800,5281,60))},
                 {"case":"rain-off","times_s":list(range(7260,7741,60))}],
        "clip_fps":2,"scope":"Offline 2D contours only; no Cubey candidate or real-time profile"})
    rows,assets = [],[]
    for case,timestamp in stills:
        h,identity = recorded(case,timestamp)
        window = region(h)
        for method in study.METHODS:
            display = {}
            for level in protocol["levels_m"]:
                start = time.perf_counter()
                try:
                    raw = study.extract(h,level,method)
                except ValueError as error:
                    rows.append({**identity,"method":method,"level_m":level,"extraction_error":str(error)})
                    continue
                extraction_ms = (time.perf_counter()-start)*1000
                original = study.filled(raw,h,level)
                for variant in protocol["variants"]:
                    start = time.perf_counter()
                    curves = study.constrained(raw,variant,protocol["relaxation"])
                    smoothing_ms = (time.perf_counter()-start)*1000
                    simple = study.linework_simple(curves)
                    shape = study.filled(curves,h,level,raw)
                    row = {**identity,"method":method,"level_m":level,"variant":variant["name"],
                           "raw_components_holes":study.topology(original),"components_holes":study.topology(shape),
                           "simple_linework":simple,"valid_shape":bool(shape.is_valid),
                           "relative_area_change":abs(shape.area-original.area)/max(original.area,1e-12),
                           "raw_area_cells2":original.area,"area_cells2":shape.area,
                           "turn_energy_reduction":1-study.turn_energy(curves)/max(study.turn_energy(raw),1e-12),
                           "max_displacement_cells":max((float(np.max(np.linalg.norm(a-b,axis=1)))
                                                         for a,b in zip(raw,curves,strict=True)),default=0),
                           "extraction_ms":extraction_ms,"smoothing_ms":smoothing_ms}
                    rows.append(row)
                    if level == .026:
                        display[variant["name"]] = curves
            if len(display) == 3:
                path = phase/f"{case}-{timestamp}-{method}.png"
                native_panel(path,h,display,window,f"{case} / {timestamp}s / {method} — 30 m cells")
                assets.append(str(path.relative_to(out)))
        print(f"Recorded offline comparison complete: {case} {timestamp}s",flush=True)
    # Two short, honest saved-frame sequences; no interpolation or fake motion.
    method = study.METHODS[0]
    for case,start in (("rain-on",4800),("rain-off",7260)):
        frames = phase/f"{case}-frames"
        reference.reserve_directory(frames)
        window = region(recorded(case,start)[0])
        timeline = []
        for i,timestamp in enumerate(range(start,start+481,60)):
            h,identity = recorded(case,timestamp)
            raw = study.extract(h,.026,method)
            curves = {v["name"]:study.constrained(raw,v,protocol["relaxation"]) for v in protocol["variants"]}
            path = frames/f"frame-{i:03d}.png"
            native_panel(path,h,curves,window,f"{case} / saved physical time {timestamp}s / marching squares + constrained smoothing")
            timeline.append({**identity,"image":str(path.relative_to(out)),"sha256":reference.sha256_file(path)})
        clip = phase/f"{case}-offline-contours.mp4"
        cmd = ["rtk","proxy","ffmpeg","-v","error","-framerate","2","-i",str(frames/"frame-%03d.png"),
               "-c:v","libx264","-pix_fmt","yuv420p","-movflags","+faststart",str(clip)]
        subprocess.run(cmd,check=True,timeout=60)
        assets.append(str(clip.relative_to(out)))
        reference.write_json_exclusive(phase/f"{case}-timeline.json",{"window_x_y_w_h":window,"fps":2,"timeline":timeline,
            "meaning":"60 physical seconds per frame; offline 2D presentation at 120x physical speed; no solver or Cubey candidate",
            "clip_sha256":reference.sha256_file(clip)})
    reference.write_json_exclusive(phase/"report.json",{"rows":rows,"assets":assets,
        "interpretation":"Context only; synthetic gate failures remain authoritative. No new candidate promotion.",
        "tools_sha256":{Path(__file__).name:reference.sha256_file(Path(__file__))}})
    reference.assert_same_runtime(initial["runtime"],reference.runtime_identity(),"after native offline checks")
    return report,rows,assets


def review(out: Path,report: dict,rows: list,assets: list):
    # Explicitly display pre-existing Cubey reference images as such, not pretend
    # that the 2D study is an integrated bank-rendering comparison.
    media = out/"reference"
    reference.reserve_directory(media)
    old = reference.OUTPUT_ROOT/"bank-presentation-v4-20261005-UOEZby"
    baseline_assets = []
    for case,camera,timestamp in (("rain-on","runoff",6000),("rain-off","collection",14400)):
        relative = f"capture/triangular-off/media/{case}-{camera}-{timestamp}s.png"
        source = old/relative
        target = media/source.name
        # Generated evidence copy, never overwrite source or existing artifacts.
        with target.open("xb") as f:
            f.write(source.read_bytes())
        baseline_assets.append({"path":str(target.relative_to(out)),"source":str(source.relative_to(reference.ROOT)),
                                "sha256":reference.sha256_file(target),"kind":"Pre-existing V4 Cubey quiet triangular reference, unchanged"})
    table = []
    for s in report["summaries"]:
        if s["variant"] != "raw":
            table.append(f'<tr><td>{s["method"]}</td><td>{s["variant"]}</td><td>Rejected</td><td>{html.escape(", ".join(s["failed_check_names"]))}</td></tr>')
    native_images = "".join(f'<p><a href="../{p}"><img width="100%" src="../{p}"></a></p>' for p in assets if p.endswith(".png"))
    clips = "".join(f'<h3>{Path(p).stem}</h3><video controls width="100%" src="../{p}"></video>' for p in assets if p.endswith(".mp4"))
    references = "".join(f'<img width="100%" src="../{a["path"]}">' for a in baseline_assets)
    page = f'''<!doctype html><meta charset="utf-8"><title>Cell-scale shoreline verdict</title>
<body style="max-width:1300px;margin:auto;padding:24px;font:18px sans-serif;line-height:1.45">
<h1>Cell-scale shoreline study: useful mechanism, no accepted candidate</h1>
<p>Constrained contour smoothing reduces the measured corner oscillation, but uniform settings fail the frozen small-feature preservation gates. No coverage was imported into Cubey; numerical fields, terrain and rendering defaults are unchanged.</p>
<p>Marching squares + light smoothing is the best lead. It keeps streams/gaps/junctions/holes connected correctly in these synthetic tests, but exceeds the 5% area limit in recession pockets and tiny diagonal features. Stronger smoothing shrinks narrow streams. Surface Nets also creates a branching diagonal contour, which this boundary pipeline deliberately refuses to reconnect.</p>
<table border="1" cellpadding="8"><tr><th>Method</th><th>Setting</th><th>Verdict</th><th>Failed gates</th></tr>{"".join(table)}</table>
<p><a href="../RESULTS.md">Results and limitations</a> | <a href="../offline/report.json">Synthetic measurements</a> | <a href="../recorded/report.json">Recorded-field measurements</a> | <a href="index.html">Full synthetic matrix</a></p>
<h2>2D saved-frame sequences — NOT Cubey rendering</h2>
<p>Raw / light / moderate; same recorded h and ROI. Blue/gray grid: original logarithmic depth. Red outline: 26 mm DISPLAY band, not numerical wet/dry shore or measured river width. Each video frame advances 60 physical seconds, at two frames/second.</p>{clips}
<details><summary>Recorded-terrain still comparisons</summary>{native_images}</details>
<details><summary>Unchanged Cubey references from V4</summary><p>These are existing captures, not newly integrated candidates.</p>{references}</details>
</body>'''
    reference.write_text_exclusive(out/"review/summary.html",page)
    reference.write_json_exclusive(out/"reference/manifest.json",baseline_assets)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if out.parent != reference.OUTPUT_ROOT or not out.name.startswith("shoreline-boundary-study-"):
        raise ValueError("invalid study output path")
    report,rows,assets = analysis(out)
    review(out,report,rows,assets)


if __name__ == "__main__":
    main()
