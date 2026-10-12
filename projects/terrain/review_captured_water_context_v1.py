#!/usr/bin/env python3
"""Bounded captured-water context study. No solver/generator/GUI launch."""
import argparse
import csv
import html
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET
import review_captured_water_v1 as base

ROOT=base.ROOT
def command(camera,context):
    return base.command(camera)+(["--terrain-water-context"] if context else [])

def report(out,s):
    folder=s.get("images_folder","pilot")
    cards=[]
    for camera,label in (("landscape","Wider landscape"),("overview","Same overview camera"),
                         ("stream","Retained stream view"),("lake","Retained lake view"),
                         ("boundary","Wet-boundary stress view")):
        cards.append(f'<section><h2>{label}</h2><div class="pair"><figure><img src="{folder}/{camera}-off.png" alt="{label}: captured patch only"><figcaption>Captured patch only</figcaption></figure><figure><img src="{folder}/{camera}-on.png" alt="{label}: same water and wider ground"><figcaption>Same water + exterior ground context</figcaption></figure></div></section>')
    rows=''.join(f'<tr><td>{r["camera"]}</td><td>{r["repeat"]}</td><td>{r["off_ms"]:.3f}</td><td>{r["on_ms"]:.3f}</td><td>{r["increment_ms"]:.3f}</td><td>{r["cpu_ms"]:.3f}</td></tr>' for r in s.get("performance",[]))
    verdict=html.escape(s.get("verdict","Prototype review; not acceptance. Tests and timing closure still pending."))
    extras=''
    receipt=''
    if "gpu_edge_height_error_m" in s:
        receipt=f'<section><h2>Validation receipt</h2><p>{s["foundation_tests"]} foundation and {s["fluid_tests"]} Fluid regression tests pass. All 8,184 perimeter vertices agree with the retained GPU basis within {s["gpu_edge_height_error_m"]*1000:.3f} mm (1 mm gate). Exterior projected area is complete, with no overlapping core triangles or unmatched interior edges. Three context-off Terrain views and three Fluid views retain exact baseline pixels. Water/context toggles, held/resumed clock, resize and actual Backdrop returns pass with exact saved GPU fields.</p><p><a href="foundation-tests.xml">Foundation tests</a> · <a href="fluid-tests.xml">Fluid tests</a> · <a href="final/cpu-perimeter.log">Full-source mesh receipt</a> · <a href="final/gpu-perimeter.log">GPU edge receipt</a> · <a href="final/transitions.log">App transitions</a>.</p></section>'
    if s.get("performance"):
        runs=[run for row in s["performance"] for run in row["runs"]]
        on=[row["runs"][1] for row in s["performance"]]
        receipt+=f'<p>Measured whole-app device-local usage: {max(r["device_local_usage_mib_max"] for r in runs):.0f} MiB; peak process RSS: {max(r["peak_process_rss_kib"] for r in runs)/1024:.1f} MiB. Context mesh upload: 18,762,072 bytes, once. These totals include the original app and the larger shadow target, not only the mesh.</p>'
        receipt+=f'<p>Isolated context opaque draw p95: {min(r["context_pass_p95_ms"]["terrain context opaque"] for r in on):.3f}–{max(r["context_pass_p95_ms"]["terrain context opaque"] for r in on):.3f} ms; context shadow draw: {min(r["context_pass_p95_ms"]["terrain context shadow"] for r in on):.3f}–{max(r["context_pass_p95_ms"]["terrain context shadow"] for r in on):.3f} ms. These intervals are included in whole-scene cost; do not add them again.</p>'
        receipt+='<p>Keep the original repeats: the first context-off overview measured 5.200 ms versus 2.989 ms in its repeat. Its negative paired delta is not evidence that adding terrain accelerates rendering. Causes were not isolated; fixed-state direct pass timings and all context-on scene timings are retained separately.</p>'
    if folder=="final":
        extras='<section><h2>Motion on held fields</h2><div class="pair"><video controls loop muted src="final/landscape-motion.mp4"></video><video controls loop muted src="final/lake-motion.mp4"></video></div><p>Surface motion only; no new water is computed. <a href="final/lake-frozen.mp4">Frozen control</a>.</p></section><details><summary>Ground-only and seam diagnostics</summary><div class="pair"><img src="final/overview-ownership.png"><img src="final/boundary-normal.png"><img src="final/boundary-clay.png"><img src="final/lake-ground.png"></div><p>Ownership: orange captured ground, blue exterior context. Diagnostic views hide water.</p></details>'
    (out/"index.html").write_text(f'''<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Captured water in a wider landscape</title><style>body{{margin:0;background:#111b24;color:#e5edf3;font:16px/1.55 system-ui}}main{{max-width:1280px;margin:auto;padding:30px}}p{{max-width:860px}}.pair{{display:grid;grid-template-columns:1fr 1fr;gap:14px}}figure{{margin:0}}img,video{{width:100%;display:block}}section{{margin:32px 0}}figcaption{{color:#afc3d4}}a{{color:#8dcefa}}td,th{{padding:8px;text-align:left;border-bottom:1px solid #405263}}.table{{overflow:auto}}@media(max-width:800px){{.pair{{grid-template-columns:1fr}}}}</style><main><p>Terrain foundation · offscreen Vulkan · no new hydraulic work</p><h1>Captured water in a wider landscape</h1><p>{verdict}</p><p>The same 16.368 km rain-fed water patch now sits inside its existing 61.41 km source terrain. An exact B-spline perimeter joins static, progressively coarser ground rings. There is no radial ground underneath the captured core and no new water outside it.</p><p>The new context is ground-only: it does not prove the surroundings are dry. The +Z source margin is only 3.321 km. The boundary stress view deliberately shows recorded streams stopping; this is not a seamless full-landscape hydraulic capture.</p><p>One opaque color/depth target precedes water. The accepted terrain shading is reused. A bounded up-sun caster fit protects core receivers at 4096 resolution; far-context shadow coverage is unqualified. Clouds remain off. Exterior damp-ground hints fade over 64 m; they are presentation weights, not invented water depth.</p>{''.join(cards)}{extras}<section><h2>1080p render-only costs</h2><p>Context off/on, same camera and saved state. Two repeats, 10 seconds at 60 Hz, first 120 frames excluded. No encoding/readback in timing windows. All values are milliseconds; increments compare run p95 values. Off uses the retained 2048 shadow; on includes the core-focused 4096 shadow upgrade. The increment therefore includes the full context integration, not merely its draw calls.</p><div class="table"><table><tr><th>Camera</th><th>Repeat</th><th>Off GPU p95</th><th>On GPU p95</th><th>Increment</th><th>CPU service p95</th></tr>{rows}</table></div></section><p><a href="summary.json">Evidence and commands</a> · <a href="/">All reports</a></p></main>''')

    if receipt:
        page=out/"index.html"
        page.write_text(page.read_text().replace("</main>",receipt+"</main>"))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output",type=Path)
    parser.add_argument("--phase",choices=("pilot","capture","performance","report"),required=True)
    a=parser.parse_args();out=a.output.resolve()
    if out.parent!=ROOT/"outputs/terrain" or not out.name.startswith("captured-water-context-"):
        raise ValueError("owned context output directory required")
    path=out/"summary.json";s=json.loads(path.read_text()) if path.exists() else {}
    runtime,inputs=base.runtime(),base.inputs()
    if a.phase in ("pilot","capture"):
        folder="pilot" if a.phase=="pilot" else "final"
        directory=out/folder;directory.mkdir()
        assets=[];parity=[]
        prior=ROOT/"outputs/terrain/captured-water-paired-20261011-JK0Nmf/terrain"
        for camera in ("landscape","overview","stream","lake","boundary"):
            for context in (False,True):
                p=directory/(camera+("-on.png" if context else "-off.png"))
                result=base.run(command(camera,context)+["--frames","1","--width","1280","--height","720",
                    "--terrain-water-audit","--output",str(p)],p.with_suffix(".log"))
                result.update(path=p.name,sha256=base.digest(p));assets.append(result)
                if not context and camera in ("overview","stream","lake"):
                    difference=base.pixel_difference(p,prior/(camera+"-water.png"))
                    if difference["max_abs_8bit"]!=0:raise ValueError("context-off altered retained core appearance")
                    parity.append({"camera":camera,**difference})
            print("captured "+camera,flush=True)
        s.update(images_folder=folder,assets=assets,core_parity=parity,runtime=runtime,input_identity=inputs)
        if a.phase=="capture":
            diagnostics=[]
            for camera,view in (("overview","ownership"),("boundary","normal"),("boundary","clay")):
                p=directory/(camera+"-"+view+".png")
                result=base.run(command(camera,True)+["--frames","1","--terrain-water-ground-view",view,
                    "--terrain-water-audit","--output",str(p),"--width","1280","--height","720"],p.with_suffix(".log"))
                diagnostics.append(dict(path=p.name,sha256=base.digest(p),**result))
            p=directory/"lake-ground.png"
            base.run(command("lake",True)+["--frames","1","--no-terrain-water-visible","--terrain-water-audit",
                "--width","1280","--height","720","--output",str(p)],p.with_suffix(".log"))
            s["diagnostics"]=diagnostics
            for camera in ("landscape","lake"):
                p=directory/(camera+"-motion.mp4")
                base.run(command(camera,True)+["--capture","video","--frames","144","--fps","24",
                    "--width","1280","--height","720","--terrain-water-audit","--output",str(p)],p.with_suffix(".log"))
            p=directory/"lake-frozen.mp4"
            base.run(command("lake",True)+["--capture","video","--frames","48","--fps","24",
                "--width","1280","--height","720","--no-terrain-water-animate","--terrain-water-audit",
                "--output",str(p)],p.with_suffix(".log"))
            base.run(command("overview",True)+["--headless-render-only","--frames","8","--render-fps","0",
                "--width","640","--height","360","--terrain-water-transition-smoke","--terrain-water-audit"],
                directory/"transitions.log")
            for kind,target in (("cpu","cubey_terrain_surface_context_tests"),("gpu","cubey_terrain_surface_context_edge_tests")):
                result=base.run(["rtk","proxy",str(ROOT/"build/release/tests/cubey"/target),str(base.CAPTURE),str(base.SOURCE)],
                    directory/(kind+"-perimeter.log"))
                s[kind+"_perimeter"]=result
            sys.path.insert(0,str(ROOT/"projects/fluid/fluid_25d"))
            import review_terrain_presentation_v1 as fluid
            fluid_assets=[]
            for camera in ("overview","slope","basin"):
                p=directory/("fluid-"+camera+".png")
                result=base.run(fluid.command("mountain",camera,"shaded",p,surface="correlated",
                    shadow="scene",shadow_extent=2048),p.with_suffix(".log"))
                original=ROOT/"outputs/terrain/captured-water-paired-20261011-JK0Nmf/baseline-fluid"/(camera+".png")
                if base.digest(p)!=base.digest(original):raise ValueError("Fluid baseline changed")
                fluid_assets.append(dict(camera=camera,sha256=base.digest(p),**result))
            s["fluid_parity"]=fluid_assets
    elif a.phase=="performance":
        if s.get("runtime")!=runtime or s.get("input_identity")!=inputs or s.get("images_folder")!="final":
            raise ValueError("final capture runtime required before measurements")
        directory=out/"performance";directory.mkdir();rows=[]
        for camera in ("landscape","overview","lake"):
            for repeat in range(2):
                pair=[]
                for context in (False,True):
                    prefix=directory/f'{camera}-{repeat}-{"on" if context else "off"}'
                    result=base.run(command(camera,context)+["--headless-render-only","--run-seconds","10",
                        "--render-fps","60","--width","1920","--height","1080","--no-validation",
                        "--profile-output",str(prefix),"--profile-warmup-frames","120",
                        "--profile-sample-interval","1"],prefix.with_suffix(".log"))
                    result.update(base.timings(prefix));pair.append(result)
                    with prefix.with_suffix(".passes.csv").open() as f:passes=list(csv.DictReader(f))
                    result["context_pass_p95_ms"]={label:base.percentile([float(r["duration_ms"]) for r in passes
                        if r["kind"]=="gpu" and r["label"]==label]) for label in ("terrain context opaque","terrain context shadow")
                        if any(r["kind"]=="gpu" and r["label"]==label for r in passes)}
                rows.append({"camera":camera,"repeat":repeat,"off_ms":pair[0]["whole_gpu_p95_ms"],
                    "on_ms":pair[1]["whole_gpu_p95_ms"],"increment_ms":pair[1]["whole_gpu_p95_ms"]-pair[0]["whole_gpu_p95_ms"],
                    "cpu_ms":max(p["cpu_service_p95_ms"] for p in pair),"runs":pair})
                print("measured "+camera+" "+str(repeat),flush=True)
        s["performance"]=rows
        s["performance_budget_pass"]=all(r["on_ms"]<=8 and r["increment_ms"]<=2 and r["cpu_ms"]<=4 for r in rows)
    elif a.phase=="report":
        if s.get("runtime")!=runtime or s.get("input_identity")!=inputs:
            raise ValueError("final report does not match captured/measured state")
        tests=ET.parse(out/"foundation-tests.xml").getroot()
        if any(int(tests.get(k,"0")) for k in ("failures","skipped","disabled","errors")):
            raise ValueError("foundation checks did not all execute and pass")
        fluid_tests=ET.parse(out/"fluid-tests.xml").getroot()
        if any(int(fluid_tests.get(k,"0")) for k in ("failures","skipped","disabled","errors")):
            raise ValueError("Fluid regression checks did not all pass")
        for asset in s["assets"]+s["diagnostics"]:
            if base.digest(out/"final"/asset["path"])!=asset["sha256"]:raise ValueError("final image changed")
        for log in (out/"final").glob("*.log"):
            if "validation error" in log.read_text().lower():raise ValueError("validation error in final receipt")
            if not log.name.startswith(("fluid-","cpu-","gpu-")) and "terrain_water_capture_immutable" not in log.read_text():
                raise ValueError("missing immutable GPU audit")
        gpu=(out/"final/gpu-perimeter.log").read_text()
        s["gpu_edge_height_error_m"]=float(re.search(r"max_height_error_m=([0-9.e+-]+)",gpu).group(1))
        if s["gpu_edge_height_error_m"]>.001:raise ValueError("GPU seam exceeds 1 mm")
        s["foundation_tests"]=int(tests.get("tests"))
        s["fluid_tests"]=int(fluid_tests.get("tests"))
        s["hydraulic_dispatches"]=0
        s["verdict"]="Useful opt-in landscape context, not a seamless whole-landscape water product. Core parity, geometry and lifecycle pass. Water cutoffs and the finite outer source remain visible in stress/overview views; more coverage is needed for unrestricted shots."
        if s.get("performance_budget_pass"):
            s["verdict"]+=" The measured performance gates pass. Manual visual acceptance and default promotion remain deferred."
        else:s["verdict"]+=" The original performance gate does not pass; no default promotion."
    if base.runtime()!=runtime or base.inputs()!=inputs:raise ValueError("study inputs/runtime mutated")
    path.write_text(json.dumps(s,indent=2,allow_nan=False)+"\n");report(out,s)

if __name__=="__main__":main()
