#!/usr/bin/env python3
"""Frozen headless world/site review. No GUI or solver; report data stays local."""
import argparse
import html
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET
import review_captured_water_v1 as base
import review_wider_water_v1 as wide
import study_terrain_world_v1 as study


def sites(out):
    return sorted((out/"sites").glob("*/site.json"))


def site_command(path, span=11457.6, yaw=.25, pitch=-.4, shift=(0,0)):
    doc=json.loads(path.read_text()); x,z=doc["center_xz_m"]
    return ["rtk","proxy",str(base.APP),"--headless","--terrain-site",str(path),
        "--terrain-water-span",str(span),"--terrain-water-yaw",str(yaw),"--terrain-water-pitch",str(pitch),
        "--terrain-water-focus-x",str(x+shift[0]),"--terrain-water-focus-z",str(z+shift[1])]


def water_command(out, extension=True, pose=None):
    argv=wide.command(study.CAPTURE,pose=pose)
    argv.append("--terrain-water-context")
    if extension: argv += ["--terrain-water-context-source",str(out/"sites/mountain-original/heightfield.json")]
    return argv


def identities(out):
    return {"runtime":base.runtime(),"protected":study.protected(),
        "packages":{str(p.relative_to(out)):study.digest(p) for p in sorted((out/"sites").rglob("*")) if p.is_file()}}


def capture(out):
    study.check(out)
    folder=out/"final"
    if folder.exists():
        attempt=len(list(out.glob("previous-render-*-*")))
        folder.rename(out/f"previous-render-{attempt}-final")
        review=out/"render-review.json"
        if review.exists(): review.rename(out/f"previous-render-{attempt}-review.json")
    folder.mkdir()
    evidence={"identities":identities(out),"assets":[],"legacy_parity":[],"site_checks":[]}
    def still(label,cmd):
        p=folder/(label+".png")
        result=base.run(cmd+["--frames","1","--width","1280","--height","720",
            "--terrain-water-audit","--output",str(p)],p.with_suffix(".log"))
        log=p.with_suffix(".log").read_text()
        if "NVIDIA GeForce RTX 5070 Ti" not in log or "hydraulic_dispatches=0" not in log:
            raise ValueError("missing device/immutable-field receipt")
        evidence["assets"].append(dict(path=str(p.relative_to(out)),sha256=study.digest(p),**result))
        print("captured "+label,flush=True)
    for path in sites(out):
        name=path.parent.name
        still(name,site_command(path))
        still(name+"-wide",site_command(path,span=32768))
        still(name+"-clay",site_command(path)+["--terrain-water-ground-view","clay"])
        still(name+"-normal",site_command(path)+["--terrain-water-ground-view","normal"])
        for i in range(8): still(name+"-orbit-"+str(i),site_command(path,yaw=i*3.141592653589793/4))
        still(name+"-closer",site_command(path,span=8192))
        for i,shift in enumerate(((-1000,0),(1000,0),(0,-1000),(0,1000))):
            still(name+"-shift-"+str(i),site_command(path,shift=shift))
    for extension in (False,True):
        label="water-world" if extension else "water-original-source"
        still(label,water_command(out,extension))
        still(label+"-wide",water_command(out,extension,(*wide.CAMERAS["landscape"][:3],32768,.25,-.4)))
    for i in range(8):
        pose=(*wide.CAMERAS["landscape"][:4],i*3.141592653589793/4,-.4)
        still("water-orbit-"+str(i),water_command(out,True,pose))
    prior=base.ROOT/"outputs/terrain/captured-water-paired-20261011-JK0Nmf/terrain"
    for camera in ("overview","stream","lake"):
        still("legacy-"+camera,base.command(camera))
        difference=base.pixel_difference(folder/("legacy-"+camera+".png"),prior/(camera+"-water.png"))
        if difference["max_abs_8bit"]: raise ValueError("legacy pixels changed")
        evidence["legacy_parity"].append(dict(camera=camera,**difference))
    # A completely separate process selects each prepared package. These prove
    # startup selection/reset, not an unimplemented live GUI site picker.
    for path in sites(out):
        log=folder/(path.parent.name+"-lifecycle.log")
        receipt=base.run(site_command(path)+["--headless-render-only","--frames","8","--render-fps","0",
            "--width","640","--height","360","--terrain-water-transition-smoke","--terrain-water-audit"],log)
        evidence["site_checks"].append(dict(site=path.parent.name,**receipt))
    if identities(out)!=evidence["identities"]: raise ValueError("capture inputs changed")
    study.write(out/"render-review.json",evidence)


def performance(out):
    evidence=json.loads((out/"render-review.json").read_text())
    if identities(out)!=evidence["identities"]: raise ValueError("frozen runtime required")
    folder=out/"performance";folder.mkdir(exist_ok=False); rows=[]
    cases=[(p.parent.name,site_command(p)) for p in sites(out)]
    cases += [("water-original-source",water_command(out,False)),("water-world",water_command(out,True))]
    for name,cmd in cases:
        for repeat in range(2):
            prefix=folder/f"{name}-{repeat}"
            receipt=base.run(cmd+["--headless-render-only","--run-seconds","10","--render-fps","60",
                "--width","1920","--height","1080","--no-validation","--profile-output",str(prefix),
                "--profile-warmup-frames","120","--profile-sample-interval","1"],prefix.with_suffix(".log"))
            rows.append(dict(case=name,repeat=repeat,**receipt,**base.timings(prefix)))
            study.write(out/"performance.json",{"rows":rows})
            print("measured "+name+" "+str(repeat),flush=True)
    study.write(out/"performance.json",{"rows":rows,"budget_pass":all(
        r["whole_gpu_p95_ms"]<=8 and r["cpu_service_p95_ms"]<=4 for r in rows)})


def maps_and_numerics(out):
    import numpy as np
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Rectangle
    from matplotlib.colors import LightSource
    protocol=study.check(out); survey=json.loads((out/"survey.json").read_text())
    raw=np.fromfile(out/"survey/elevation.f32",dtype="<f4").reshape(survey["elevation"]["shape"])
    origin=survey["grid"]["origin_xz_m"]; spacing=survey["grid"]["spacing_m"]
    fig,ax=plt.subplots(figsize=(10,9),facecolor="#14202a",layout="constrained")
    extent=[(origin[0]-spacing/2)/1000,(origin[0]+(raw.shape[1]-.5)*spacing)/1000,
            (origin[1]-spacing/2)/1000,(origin[1]+(raw.shape[0]-.5)*spacing)/1000]
    im=ax.imshow(raw,origin="lower",extent=extent,cmap="terrain")
    colors=("#edb559","#f07ba7","#83d5ce")
    names=[]; arrays=[]; diagnostics=[]
    for path,color in zip(sites(out),colors):
        doc=json.loads(path.read_text()); names.append(doc["site_id"])
        x,z=doc["center_xz_m"]
        for level,lw,alpha in ((doc["levels"][0],2,1),(doc["levels"][-1],1,.7)):
            span=level["span_m"]/1000
            ax.add_patch(Rectangle(((x-level["span_m"]/2)/1000,(z-level["span_m"]/2)/1000),span,span,
                                  fill=False,edgecolor=color,lw=lw,alpha=alpha))
        ax.scatter([x/1000],[z/1000],color=color,s=35,label=doc["site_id"])
        field=json.loads((path.parent/"heightfield.json").read_text())
        a=np.fromfile(path.parent/"elevation.f32",dtype="<f4").reshape(field["files"]["elevation"]["shape"])
        if not np.isfinite(a).all(): raise ValueError("nonfinite native terrain")
        arrays.append((field,a))
        first=doc["tier_fields"][0]; fine=np.fromfile(path.parent/first["path"],dtype="<f4").reshape(first["shape"])
        dz,dx=np.gradient(fine.astype(np.float64),first["sample_spacing_m"])
        diagnostics.append({"site_id":doc["site_id"],"native_detail_relief_m":float(np.ptp(fine)),
                            "rendered_detail_relief_m":float(np.ptp(fine))*field["height"]["scale"],
                            "detail_slope_p95":float(np.percentile(np.hypot(dx,dz),95))})
    b=protocol["world"]["survey_bounds"]
    lower=np.array(b["minimum"])/1000; upper=np.array(b["maximum"])/1000
    ax.add_patch(Rectangle(lower,*(upper-lower),fill=False,edgecolor="white",lw=2))
    radius=(protocol["sites"][0]["levels"][-1]["span_m"]/2+1480)/1000
    ax.add_patch(Rectangle(lower+radius,*(upper-lower-2*radius),fill=False,edgecolor="white",ls="--",lw=2))
    ax.set(xlabel="World X (km)",ylabel="World Z (km)",title="One world: survey, eligible centers, and prepared site boxes")
    ax.tick_params(colors="#d5e0e8"); ax.xaxis.label.set_color("#d5e0e8"); ax.yaxis.label.set_color("#d5e0e8")
    ax.title.set_color("#e4edf5"); ax.legend(loc="lower left",fontsize=9)
    bar=fig.colorbar(im,ax=ax,label="Coarse conditioning elevation (m), not rendered terrain")
    bar.ax.tick_params(colors="#d5e0e8");bar.ax.yaxis.label.set_color("#d5e0e8")
    fig.savefig(out/"world-map.png",dpi=140);plt.close(fig)
    overlaps=[]
    for i,(a,av) in enumerate(arrays):
        for j,(b,bv) in enumerate(arrays[:i]):
            ai=a["provenance"]["model_native_origin"]["i"];aj=a["provenance"]["model_native_origin"]["j"]
            bi=b["provenance"]["model_native_origin"]["i"];bj=b["provenance"]["model_native_origin"]["j"]
            y0,x0=max(ai,bi),max(aj,bj);y1=min(ai+av.shape[0],bi+bv.shape[0]);x1=min(aj+av.shape[1],bj+bv.shape[1])
            if y0<y1 and x0<x1:
                error=float(np.abs(av[y0-ai:y1-ai,x0-aj:x1-aj]-bv[y0-bi:y1-bi,x0-bj:x1-bj]).max())
                if error>1e-4: raise ValueError("prepared sites are not one coherent world")
                overlaps.append({"sites":[names[i],names[j]],"samples":(y1-y0)*(x1-x0),"max_abs_m":error})
    spike=json.loads((out/"spike.json").read_text()); fig,axes=plt.subplots(1,3,figsize=(15,5),layout="constrained")
    for ax,name in zip(axes,("native","filtered-native","low-frequency")):
        record=spike["files"][name];a=np.fromfile(out/"spike"/record["path"],dtype="<f4").reshape(record["shape"])
        hill=LightSource(315,45).hillshade(a,dx=30 if name=="native" else 240,dy=30 if name=="native" else 240)
        ax.imshow(hill,cmap="gray",vmin=0,vmax=1); ax.set_title(name);ax.axis("off")
    fig.savefig(out/"generation-spike.png",dpi=120);plt.close(fig)
    study.write(out/"world-numerics.json",{"site_diagnostics":diagnostics,"overlaps":overlaps})


def report(out):
    maps_and_numerics(out)
    e=json.loads((out/"render-review.json").read_text());p=json.loads((out/"performance.json").read_text())
    if identities(out)!=e["identities"]: raise ValueError("closure inputs changed")
    tests=ET.parse(out/"tests.xml").getroot()
    if any(tests.findall(".//"+k) for k in ("failure","error","skipped")): raise ValueError("incomplete tests")
    for asset in e["assets"]:
        if study.digest(out/asset["path"])!=asset["sha256"]:raise ValueError("media changed")
    expected_cases={path.parent.name for path in sites(out)}|{"water-original-source","water-world"}
    if (len(p["rows"])!=10 or {(r["case"],r["repeat"]) for r in p["rows"]}!=
            {(name,repeat) for name in expected_cases for repeat in (0,1)}):
        raise ValueError("incomplete performance repeats")
    if not p["budget_pass"]:raise ValueError("render budget gate failed")
    if len(tests.findall(".//testcase"))<17:raise ValueError("missing regression checks")
    def picture(name,caption): return f'<figure><img loading="lazy" src="final/{name}.png"><figcaption>{html.escape(caption)}</figcaption></figure>'
    cards="";generation=""
    for path in sites(out):
        d=json.loads(path.read_text());name=d["site_id"]
        cards+=f'<section><h2>{name}</h2><div class="pair">'+picture(name,"Detail inspection; no water simulated")+picture(name+"-wide","Wider framing; outer shadows remain unqualified")+'</div><details><summary>Orbit, translation and geometry diagnostics</summary><div class="grid">'
        cards+=''.join(picture(name+"-orbit-"+str(i),f"Orbit {i*45}°") for i in range(8))
        cards+=picture(name+"-clay","Clay")+picture(name+"-normal","Normals")+picture(name+"-closer","Closer")
        cards+=''.join(picture(name+"-shift-"+str(i),"Target shifted 1 km") for i in range(4))+'</div></details></section>'
        receipt=json.loads((out/f"site-{['mountain-original','mountain-interior','valley-interior'].index(name)}-receipt.json").read_text())
        generation+=f'<tr><td>{name}</td><td>{receipt["total_seconds"]:.1f} s</td><td>{receipt["peak_cuda_allocated_bytes"]/2**30:.2f} GiB</td><td>{d["generation"]["max_tile_overlap_m"]:.6f} m</td></tr>'
    perf=''.join(f'<tr><td>{r["case"]} / {r["repeat"]+1}</td><td>{r["whole_gpu_p95_ms"]:.3f} ms</td><td>{r["cpu_service_p95_ms"]:.3f} ms</td><td>{r["peak_process_rss_kib"]/1024:.0f} MiB</td></tr>' for r in p["rows"])
    page=f'''<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Terrain world/site foundation</title>
<style>body{{margin:0;background:#14202a;color:#e4edf5;font:16px/1.55 system-ui}}main{{max-width:1280px;margin:auto;padding:28px}}p{{max-width:950px}}.pair,.grid{{display:grid;grid-template-columns:1fr 1fr;gap:14px}}figure{{margin:0}}img{{display:block;width:100%}}section{{margin:32px 0}}figcaption{{color:#b0c3d3}}a{{color:#8bcafa}}td,th{{padding:8px;text-align:left;border-bottom:1px solid #405465}}.scroll{{overflow:auto}}@media(max-width:750px){{.pair,.grid{{grid-template-columns:1fr}}}}</style>
<main><p><a href="/">All reports</a> · Shared terrain foundation · headless Vulkan</p><h1>World-first terrain coverage</h1>
<p>One unchanged Terrain Diffusion world; approximately 262 km coarse survey. Sites require the complete 131 km backdrop plus movement/sampling margins, with 16/32/65/131 km nested requested coverage. Actual retained native coverage is about 132.7 km. Generated data is cached outside Git. No live diffusion, solver, default promotion, or push.</p>
<p>The original terrain and captured-water fields remain unchanged. New sites are terrain-only controls, not simulations with magically dry surroundings. Existing water still ends at its finite captured rectangle; broader terrain does not extend hydraulic coverage.</p>
<img src="world-map.png"><p>Solid white: requested world survey bounds. Dashed white: eligible site-center region. Colored inner/outer boxes: detail/backdrop. Coarse conditioning is for selection only, not final terrain.</p>
<section><h2>Existing water: more surrounding terrain</h2><div class="pair">{picture('water-original-source','Existing source coverage')}{picture('water-world','New same-world terrain coverage; water unchanged')}{picture('water-original-source-wide','Existing coverage, wider framing')}{picture('water-world-wide','Expanded coverage, same wider framing')}</div><details><summary>Eight water-site orbit headings</summary><div class="grid">{''.join(picture('water-orbit-'+str(i),f'Orbit {i*45}°') for i in range(8))}</div></details></section>
{cards}<section><h2>Generation decision</h2><img src="generation-spike.png"><p>Reject the low-frequency-only candidate: p95 absolute difference from filtered native is about 216 m. Accepted fallback generates native tiles for selected backdrops, then filters them. Strict precision and the existing 64-sample halo recover an exact overlap with the saved source. Native overlap is not a claim of cross-hardware bitwise reproducibility.</p><table><tr><th>Site</th><th>Generation including setup</th><th>Peak CUDA allocation</th><th>Native seam error</th></tr>{generation}</table><p>Runtime presently retains the native site raster and its existing filtered mip chain. Compact exported tiers document consistent sampling/coverage but are not yet a streamed, sparse-resident runtime hierarchy. Cold dependency/model installation is excluded; each producer process has a 900 s wall bound.</p></section>
<section><h2>1080p render-only costs</h2><p>Solver and diffusion generation stopped. Two repeats, 60 Hz pacing, 120 warmup frames excluded, no readback/encoding. Whole-scene GPU p95 ≤8 ms / CPU service p95 ≤4 ms: {p['budget_pass']}.</p><div class="scroll"><table><tr><th>Case / repeat</th><th>GPU p95</th><th>CPU service p95</th><th>Process peak RSS</th></tr>{perf}</table></div></section>
<section><h2>Scope and acceptance</h2><p>{len(tests.findall('.//testcase'))} CPU/GPU/regression tests passed with no skips. Three legacy captured-water views remain pixel-exact. Numerical overlap checks connect all three prepared sites to the same world. Startup site selection/reset and existing surface/context lifecycle pass; there is no new live GUI site picker and native GUI acceptance is deferred.</p><p>Finite outer terrain, conservative distant geometry, and core-focused shadows remain limitations. The lower-relief valley is a contrast control, not a promoted water site. Full horizon/free-flight coverage, new water bakes and sparse terrain streaming are follow-up work.</p><p><a href="protocol.json">Protocol</a> · <a href="world-numerics.json">World overlap checks</a> · <a href="package-validation.json">Packages</a> · <a href="render-review.json">Render receipts</a> · <a href="performance.json">Performance</a> · <a href="tests.xml">Tests</a></p></section></main>'''
    (out/"index.html").write_text(page)


def main():
    parser=argparse.ArgumentParser(__doc__);parser.add_argument("output",type=Path)
    parser.add_argument("--phase",choices=("capture","performance","report"),required=True)
    args=parser.parse_args();out=study.owned(args.output)
    globals()[args.phase](out)


if __name__=="__main__":main()
