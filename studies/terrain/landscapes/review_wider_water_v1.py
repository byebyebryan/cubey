#!/usr/bin/env python3
"""Headless wider-capture review, frozen inputs, no solver or GUI launch.

Capture/performance use standard Python. Basin/report use the already installed
optional NumPy/Matplotlib environment; no dependency installation is attempted.
"""
import argparse
import html
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET
import review_captured_water_v1 as base
import study_wider_water_v1 as study

ROOT=base.ROOT
FOCUS=(11520,19200)
CAMERAS={"landscape":(11520,19200,1342.083496,11457.6,.25,-.4),
         "closer":(11520,19200,1342.083496,8192,.25,-.4),
         "stream":(15464.687988,20214.815613,1494.829956,900,-.52,-.92),
         "lake":(13713.312012,19543.728455,1718.439941,1800,-.52,-.92)}


def command(capture,camera="landscape",detail="fixed",water=True,pose=None):
    x,z,y,span,yaw,pitch=pose or CAMERAS[camera]
    argv=["rtk","proxy",str(base.APP),"--headless","--terrain-heightfield",str(study.SOURCE),
          "--terrain-water-capture",str(capture),"--terrain-water-capture-camera",
          "landscape" if camera in ("landscape","closer") else camera,
          "--terrain-water-detail",detail,
          "--terrain-water-detail-focus-x",str(FOCUS[0]),"--terrain-water-detail-focus-z",str(FOCUS[1]),
          "--terrain-water-focus-x",str(x),"--terrain-water-focus-z",str(z),
          "--terrain-water-focus-height",str(y),"--terrain-water-span",str(span),
          "--terrain-water-yaw",str(yaw),"--terrain-water-pitch",str(pitch)]
    if not water:argv+=["--no-terrain-water-visible"]
    return argv


def inputs(out):
    result={"old":base.inputs(),"protocol_sha256":base.digest(out/"protocol.json")}
    result["captures"]={str(p.relative_to(out)):base.digest(p) for p in sorted((out/"captures").rglob("*")) if p.is_file()}
    return result


def capture(out):
    folder=out/"final";folder.mkdir()
    evidence={"runtime":base.runtime(),"inputs":inputs(out),"assets":[],"differences":[],"old_parity":[]}
    wide=out/"captures/wide-20h/water-capture.json"
    pilot=out/"captures/pilot-20h/water-capture.json"
    def still(label,bundle,camera="landscape",detail="fixed",pose=None,extra=()):
        p=folder/(label+".png")
        receipt=base.run(command(bundle,camera,detail,pose=pose)+["--frames","1","--width","1280","--height","720",
            "--terrain-water-audit","--output",str(p),*extra],p.with_suffix(".log"))
        receipt.update(path=str(p.relative_to(out)),sha256=base.digest(p))
        evidence["assets"].append(receipt)
        print("captured "+label,flush=True)
        return p
    # The old product stays exactly unchanged. Explicit world poses are only
    # used in new comparisons, never as its pixel-parity reference.
    prior=ROOT/"outputs/terrain/captured-water-paired-20261011-JK0Nmf/terrain"
    for camera in ("overview","stream","lake"):
        p=folder/("old-default-"+camera+".png")
        receipt=base.run(base.command(camera)+["--frames","1","--width","1280","--height","720",
            "--terrain-water-audit","--output",str(p)],p.with_suffix(".log"))
        difference=base.pixel_difference(p,prior/(camera+"-water.png"))
        if difference["max_abs_8bit"]!=0:raise ValueError("old captured path changed")
        evidence["old_parity"].append(dict(camera=camera,**difference,**receipt))
    for camera in CAMERAS:
        a=still(camera+"-uniform",wide,camera,"uniform-control")
        b=still(camera+"-fixed",wide,camera)
        evidence["differences"].append(dict(camera=camera,**base.pixel_difference(a,b)))
    still("old-context",base.CAPTURE,detail="uniform",extra=("--terrain-water-context",))
    still("pilot-64m",pilot)
    still("wide-32m-10h",out/"captures/wide-10h/water-capture.json")
    still("ground-only",wide,extra=("--no-terrain-water-visible",))
    still("normal",wide,extra=("--terrain-water-ground-view","normal",))
    # Freeze mesh focus while varying camera headings, zoom and translations.
    for i,yaw in enumerate(json.loads((out/"protocol.json").read_text())["camera_envelope"]["orbit_yaws_rad"]):
        pose=(*CAMERAS["landscape"][:4],yaw,-.4)
        still("orbit-"+str(i),wide,pose=pose)
    for i,(dx,dz) in enumerate(((-1000,0),(1000,0),(0,-1000),(0,1000))):
        pose=(FOCUS[0]+dx,FOCUS[1]+dz,*CAMERAS["landscape"][2:])
        still("shift-"+str(i),wide,pose=pose)
    for frozen in (False,True):
        p=folder/("motion-frozen.mp4" if frozen else "motion.mp4")
        receipt=base.run(command(wide)+["--capture","video","--frames", "48" if frozen else "144", "--fps","24",
            "--width","1280","--height","720","--terrain-water-audit","--output",str(p)]+
            (["--no-terrain-water-animate"] if frozen else []),p.with_suffix(".log"))
        evidence["assets"].append(dict(path=str(p.relative_to(out)),sha256=base.digest(p),**receipt))
    receipt=base.run(command(wide)+["--headless-render-only","--frames","8","--render-fps","0",
        "--width","640","--height","360","--terrain-water-context","--terrain-water-transition-smoke",
        "--terrain-water-audit"],folder/"transitions.log")
    evidence["transitions"]=receipt
    sys.path.insert(0,str(study.FLUID))
    import review_terrain_presentation_v1 as fluid
    for camera in ("overview","slope","basin"):
        p=folder/("fluid-"+camera+".png")
        receipt=base.run(fluid.command("mountain",camera,"shaded",p,surface="correlated",shadow="scene",shadow_extent=2048),
                         p.with_suffix(".log"))
        prior=ROOT/"outputs/terrain/captured-water-paired-20261011-JK0Nmf/baseline-fluid"/(camera+".png")
        if base.digest(p)!=base.digest(prior):raise ValueError("Fluid pixels changed")
        evidence.setdefault("fluid_parity",[]).append(dict(camera=camera,sha256=base.digest(p),**receipt))
    if evidence["runtime"]!=base.runtime() or evidence["inputs"]!=inputs(out):
        raise ValueError("frozen render/capture inputs changed")
    study.write(out/"review.json",evidence)


def performance(out):
    s=json.loads((out/"review.json").read_text())
    if s["runtime"]!=base.runtime() or s["inputs"]!=inputs(out):raise ValueError("frozen runtime required")
    folder=out/"performance";folder.mkdir();rows=[]
    wide=out/"captures/wide-20h/water-capture.json"
    for camera in ("landscape","stream","lake"):
        for repeat in range(2):
            runs={}
            for detail,water in (("uniform-control",True),("fixed",False),("fixed",True)):
                prefix=folder/f'{camera}-{repeat}-{detail}-{"water" if water else "ground"}'
                receipt=base.run(command(wide,camera,detail,water)+["--headless-render-only","--run-seconds","10",
                    "--render-fps","60","--width","1920","--height","1080","--no-validation",
                    "--profile-output",str(prefix),"--profile-warmup-frames","120","--profile-sample-interval","1"],
                    prefix.with_suffix(".log"))
                runs[detail+("-water" if water else "-ground")]=dict(**receipt,**base.timings(prefix))
            w=runs["fixed-water"];g=runs["fixed-ground"]
            rows.append({"camera":camera,"repeat":repeat,"runs":runs,"whole_gpu_ms":w["whole_gpu_p95_ms"],
                "water_increment_ms":w["whole_gpu_p95_ms"]-g["whole_gpu_p95_ms"],"cpu_ms":w["cpu_service_p95_ms"]})
            s["performance"]=rows;study.write(out/"review.json",s)
            print("measured "+camera+" "+str(repeat),flush=True)
    s["performance_budget_pass"]=all(r["whole_gpu_ms"]<=8 and r["cpu_ms"]<=4 for r in rows)
    study.write(out/"review.json",s)


def maps(out):
    import numpy as np
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import LightSource,LogNorm
    capture=out/"captures/wide-20h"
    doc=json.loads((capture/"water-capture.json").read_text())
    # Neutral capture fields are kept unchanged; these are presentation maps.
    n=doc["grid"]["width"];spacing=doc["grid"]["cell_size_m"]
    bed=np.fromfile(capture/"bed.f32",dtype="<f4").reshape(n,n)
    depth=np.fromfile(capture/"depth.f32",dtype="<f4").reshape(n,n)
    fig,axes=plt.subplots(1,2,figsize=(14,6),facecolor="#14202a",layout="constrained")
    for ax in axes:
        ax.set_facecolor("#14202a");ax.tick_params(colors="#cad8e4")
        ax.set_xlabel("World X (km)",color="#cad8e4");ax.set_ylabel("World Z (km)",color="#cad8e4")
    extent=(-30.592,30.592,30.592,-30.592)
    hill=LightSource(315,45).hillshade(bed,dx=spacing,dy=spacing)
    axes[0].imshow(hill,cmap="gray",extent=extent);axes[0].imshow(np.ma.masked_less(depth,.05),cmap="Blues",
        norm=LogNorm(.05,200),alpha=.8,extent=extent)
    axes[0].set_title("Wider rain-fed state: water ≥ 5 cm",color="#e3edf5")
    im=axes[1].imshow(np.ma.masked_less(depth,.01),cmap="viridis",norm=LogNorm(.01,200),extent=extent)
    axes[1].set_title("Captured depth (m), logarithmic",color="#e3edf5")
    bar=fig.colorbar(im,ax=axes[1],label="Depth (m)")
    bar.ax.tick_params(colors="#cad8e4");bar.ax.yaxis.label.set_color("#cad8e4")
    for ax in axes:
        from matplotlib.patches import Rectangle
        ax.add_patch(Rectangle((3.336,11.016),16.368,16.368,fill=False,color="#eea455",lw=1.5))
        ax.scatter([11.52],[19.2],s=12,c="#f7d5a0")
    fig.savefig(out/"coverage.png",dpi=120);plt.close(fig)
    return {"max_depth_m":float(depth.max()),"wet_ge_5cm_cells":int((depth>=.05).sum()),
            "water_volume_m3":float(depth.sum(dtype=np.float64)*spacing**2)}


def basin(out):
    """Inspect the new deepest large body, not just the old lake camera location.

    Camera selection reads the capture only; it changes neither terrain nor water.
    This far-region stress control retains the same fixed mesh/shadow focus.
    """
    import numpy as np
    s=json.loads((out/"review.json").read_text())
    if s["runtime"]!=base.runtime() or s["inputs"]!=inputs(out):raise ValueError("frozen runtime required")
    bundle=out/"captures/wide-20h/water-capture.json"
    doc=json.loads(bundle.read_text());n=doc["grid"]["width"];spacing=doc["grid"]["cell_size_m"]
    depth=np.fromfile(bundle.parent/"depth.f32",dtype="<f4").reshape(n,n)
    bed=np.fromfile(bundle.parent/"bed.f32",dtype="<f4").reshape(n,n)
    z,x=np.unravel_index(depth.argmax(),depth.shape)
    origin=doc["grid"]["sample_origin_xz_m"]
    pose=(origin[0]+int(x)*spacing,origin[1]+int(z)*spacing,float(bed[z,x]+depth[z,x]),6000,-.52,-.65)
    s["basin_control"]={"pose":pose,"selection":"maximum captured depth; no source/state edits",
                        "shadow_receivers":"outside fine region; unqualified"}
    for detail in ("uniform-control","fixed"):
        p=out/"final"/("basin-"+detail+".png")
        receipt=base.run(command(bundle,"lake",detail,pose=pose)+["--frames","1","--width","1280","--height","720",
            "--terrain-water-audit","--output",str(p)],p.with_suffix(".log"))
        s["assets"].append(dict(path=str(p.relative_to(out)),sha256=base.digest(p),**receipt))
    s["basin_control"]["difference"]=base.pixel_difference(out/"final/basin-uniform-control.png",out/"final/basin-fixed.png")
    rows=s["performance"]
    for repeat in range(2):
        runs={}
        for detail,water in (("uniform-control",True),("fixed",False),("fixed",True)):
            prefix=out/"performance"/f'basin-{repeat}-{detail}-{"water" if water else "ground"}'
            receipt=base.run(command(bundle,"lake",detail,water,pose)+["--headless-render-only","--run-seconds","10",
                "--render-fps","60","--width","1920","--height","1080","--no-validation",
                "--profile-output",str(prefix),"--profile-warmup-frames","120","--profile-sample-interval","1"],prefix.with_suffix(".log"))
            runs[detail+("-water" if water else "-ground")]=dict(**receipt,**base.timings(prefix))
        w=runs["fixed-water"];g=runs["fixed-ground"]
        rows.append({"camera":"basin","repeat":repeat,"runs":runs,"whole_gpu_ms":w["whole_gpu_p95_ms"],
                     "water_increment_ms":w["whole_gpu_p95_ms"]-g["whole_gpu_p95_ms"],"cpu_ms":w["cpu_service_p95_ms"]})
        print("measured large basin "+str(repeat),flush=True)
    s["performance_budget_pass"]=all(r["whole_gpu_ms"]<=8 and r["cpu_ms"]<=4 for r in rows)
    study.write(out/"review.json",s)


def report(out):
    s=json.loads((out/"review.json").read_text())
    if s["runtime"]!=base.runtime() or s["inputs"]!=inputs(out):raise ValueError("report inputs/runtime changed")
    for a in s["assets"]:
        if base.digest(out/a["path"])!=a["sha256"]:raise ValueError("review asset changed")
    expected={(camera,repeat) for camera in ("landscape","stream","lake","basin") for repeat in range(2)}
    if len(s.get("performance",[]))!=8 or {(r["camera"],r["repeat"]) for r in s["performance"]}!=expected:
        raise ValueError("all eight final render-only comparisons are required")
    for row in s["performance"]:
        if set(row["runs"])!={"uniform-control-water","fixed-ground","fixed-water"}:
            raise ValueError("missing matched performance control")
    for camera in ("overview","stream","lake"):
        prior=ROOT/"outputs/terrain/captured-water-paired-20261011-JK0Nmf/terrain"/(camera+"-water.png")
        if base.pixel_difference(out/"final"/("old-default-"+camera+".png"),prior)["max_abs_8bit"]!=0:
            raise ValueError("retained Terrain reference differs at closure")
    for row in s["fluid_parity"]:
        if base.digest(out/"final"/("fluid-"+row["camera"]+".png"))!=row["sha256"]:
            raise ValueError("retained Fluid reference changed at closure")
    tests=ET.parse(out/"tests.xml").getroot()
    if any(int(tests.get(k,"0")) for k in ("failures","errors","skipped","disabled")):
        raise ValueError("tests failed or did not execute")
    for p in (out/"final").glob("*.log"):
        if "validation error" in p.read_text().lower():raise ValueError("validation error in final capture")
        if "NVIDIA GeForce RTX 5070 Ti" not in p.read_text():raise ValueError("missing actual hardware-device receipt")
    s["tests"]={"count":int(tests.get("tests")),"sha256":base.digest(out/"tests.xml")}
    s["coverage_stats"]=maps(out)
    s["visual_verdict"]="Useful opt-in wider water backdrop. Main views retain streams and lakes; aggressive coarse rings were rejected. The full original-site orbit is not visually qualified because several headings expose the finite outer-source edge. Far shore and shadow fidelity remains a tradeoff. No default promotion or manual visual acceptance."
    s["helper_tests"]={"count":7,"scope":"portable Python geometry/command tests, not native GPU acceptance"}
    s["hydraulic_dispatches_in_consumer"]=0
    s["manual_visual_acceptance"]="deferred"
    s["consumer_asset"]="cache/terrain/water/v1/mountain-wide-rain-fed-1913-32m-study/water-capture.json"
    rows=[]
    for candidate in study.CANDIDATES:
        r=json.loads((out/"runs"/candidate/"result.json").read_text())
        if r["errors"] or r["status"]!="completed":raise ValueError("complete immutable offline runs required")
        rows.append(f'<tr><td>{candidate}</td><td>{r["total_wall_s"]/60:.2f} min</td><td>{r["milestones"][-1]["physical_s"]/3600:.2f} h</td><td>{r["milestones"][-1]["stats"]["max_depth_m"]:.1f} m</td></tr>')
    cards=[]
    for camera,label in (("landscape","Main landscape"),("closer","Closer landscape"),("stream","Original stream location"),("lake","Original lake location")):
        cards.append(f'<section><h2>{label}</h2><div class="pair"><figure><img src="final/{camera}-uniform.png"><figcaption>Uniform 2× mesh, matching shadows</figcaption></figure><figure><img src="final/{camera}-fixed.png"><figcaption>Fixed 16 / 32 / 64 m mesh</figcaption></figure></div></section>')
    if "basin_control" in s:
        cards.append('<section><h2>New large lake: far-region stress control</h2><div class="pair"><figure><img src="final/basin-uniform-control.png"><figcaption>Uniform reference</figcaption></figure><figure><img src="final/basin-fixed.png"><figcaption>Fixed distant detail</figcaption></figure></div><p>Camera selected from maximum saved depth, without editing the state. This lake is outside the original fine/shadow receiver region: its coarser banks and unqualified far shadows are retained tradeoffs, not a promise of close-up quality everywhere.</p></section>')
    perf=''.join(f'<tr><td>{r["camera"]} #{r["repeat"]+1}</td><td>{r["runs"]["uniform-control-water"]["whole_gpu_p95_ms"]:.2f}</td><td>{r["whole_gpu_ms"]:.2f}</td><td>{r["water_increment_ms"]:.2f}</td><td>{r["cpu_ms"]:.2f}</td></tr>' for r in s.get("performance",[]))
    orbit=''.join(f'<figure><img loading="lazy" src="final/orbit-{i}.png"><figcaption>Yaw {i*45}° · original site</figcaption></figure>' for i in range(8))
    limits=html.escape(s.get("visual_verdict","Useful opt-in wider water backdrop. Main views retain streams and lakes; aggressive coarse rings were rejected. The full original-site orbit is not visually qualified because several headings expose the finite outer-source edge. No default promotion or manual visual acceptance."))
    page=f'''<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Wider captured water backdrop</title><style>body{{margin:0;background:#14202a;color:#e4edf5;font:16px/1.55 system-ui}}main{{max-width:1280px;margin:auto;padding:28px}}p{{max-width:900px}}.pair,.grid{{display:grid;grid-template-columns:1fr 1fr;gap:14px}}figure{{margin:0}}img,video{{display:block;width:100%}}section{{margin:32px 0}}figcaption{{color:#b0c3d3}}a{{color:#8bcafa}}td,th{{padding:8px;text-align:left;border-bottom:1px solid #405465}}.scroll{{overflow:auto}}@media(max-width:750px){{.pair,.grid{{grid-template-columns:1fr}}}}</style><main><p><a href="/">All reports</a> · Terrain foundation · genuine offscreen Vulkan</p><h1>Wider captured water backdrop</h1><p>{limits}</p><p>One unchanged terrain source; one coherent 61.184 km rain-fed capture. No independently simulated outer splice. Bed, depth and velocity remain static; surface shading and whitewater animate. No solver runs in the Terrain consumer.</p><section><h2>Coverage first</h2><img src="coverage.png"><p>Orange rectangle: previous 16.368 km water capture. New water spans almost the full existing terrain source. Logarithmic colors show captured depth, not terrain climate or realistic rainfall.</p><div class="pair"><figure><img src="final/old-context.png"><figcaption>Old water + ground-only context</figcaption></figure><figure><img src="final/landscape-fixed.png"><figcaption>Wider 32 m capture, twenty simulated rain-on hours</figcaption></figure></div></section>{''.join(cards)}<section><h2>Surface motion on saved fields</h2><video controls loop muted src="final/motion.mp4"></video><p><a href="final/motion-frozen.mp4">Frozen motion control</a>. Animation is not an advancing hydraulic simulation.</p></section><section><h2>Offline bake cost</h2><table><tr><th>Grid</th><th>Total wall time</th><th>Physical time</th><th>Maximum depth</th></tr>{''.join(rows)}</table><p>512 mm/h continuous visual-demo rain; dry start, existing fall boundaries and roughness. Depth maxima are localized basins under exaggerated rainfall, not ordinary permanent lakes or a steady-state claim.</p></section><section><h2>1080p render-only costs</h2><p>Two repeats per view, 60 Hz pacing, 120 warmup frames excluded; solver stopped, no image readback/encoding. Both topology controls use the same bounded 4096 shadow fit. Proposed scene GPU p95 ≤8 ms / CPU service p95 ≤4 ms: {s.get("performance_budget_pass","pending")}.</p><div class="scroll"><table><tr><th>View</th><th>Uniform GPU</th><th>Fixed GPU</th><th>Water increment</th><th>CPU service</th></tr>{perf}</table></div><p>Incremental water cost is reported separately; independent percentile differences are not GPU attribution. Negative deltas are variability, not speedups.</p></section><details><summary>Orbit envelope and finite-source limits</summary><div class="grid">{orbit}</div><p>The original site is only about 3.2 km from the wider capture's +Z boundary. Wider water removes the old inner rectangular cutoff, not the finite outer-source limit. Views exposing that outer edge remain unqualified; no fade pretends the surrounding world is hydraulically dry.</p></details><details><summary>Retained rejected density trial and ground diagnostics</summary><div class="pair"><img src="pilot-6h-lod.png"><img src="pilot-6h-lod64.png"><img src="final/ground-only.png"><img src="final/normal.png"></div><p>The first aggressive ring schedule exaggerated distant wet silhouettes. The retained candidate caps outer spacing at 64 m; no new adaptive feature algorithm or water optics changes were needed.</p></details><p>{s["tests"]["count"]} CPU/GPU/regression checks pass; old Terrain and Fluid pixel references remain exact. <a href="tests.xml">Tests</a> · <a href="protocol.json">Frozen study contract</a> · <a href="review.json">Commands, inputs, timings and receipts</a>.</p></main>'''
    (out/"index.html").write_text(page)
    additions='<details><summary>Resolution, capture duration and motion controls</summary><div class="pair"><figure><img src="final/pilot-64m.png"><figcaption>64 m pilot, 20 simulated hours</figcaption></figure><figure><img src="final/wide-32m-10h.png"><figcaption>32 m candidate, 10.6 simulated hours</figcaption></figure></div><p>The old capture is 16 m / 10.6 hours; the main new capture is 32 m / 20 hours over a wider catchment. Old/new coverage images are not an isolated accuracy or LOD comparison. The matched uniform/fixed panels isolate rendering topology on identical saved fields and shadow settings.</p><p>Decoded animated endpoints differ by about 1.54/255 mean RGB; the frozen MP4 endpoints differ by 0.13/255. MP4 is lossy and is not a pixel-exact freeze assertion. Exact pixel parity uses PNG references; immutable GPU field audits cover both clips.</p></details>'
    page_path=out/"index.html"
    page_path.write_text(page_path.read_text().replace('</main>',additions+'</main>'))
    study.write(out/"review.json",s)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("phase",choices=("capture","performance","basin","report"));p.add_argument("out",type=Path)
    a=p.parse_args();out=study.owned_root(a.out).resolve()
    study.check_frozen(out)
    globals()[a.phase](out)


if __name__=="__main__":main()
