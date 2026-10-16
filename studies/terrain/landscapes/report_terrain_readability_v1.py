#!/usr/bin/env python3
"""Compact atlas-first review; scientific plotting never runs a solver."""
import argparse
import csv
import html
import json
import statistics
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urljoin
from urllib.request import Request, urlopen
import xml.etree.ElementTree as ET
import review_captured_water_v1 as base
import review_terrain_readability_v1 as review
import study_terrain_atlas_v1 as atlas
import study_terrain_world_v1 as study


def maps(out):
    import numpy as np
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import LightSource, ListedColormap
    from matplotlib.patches import Rectangle
    doc=atlas.check(out); folder=out/"atlas"; manifest=json.loads((folder/"atlas.json").read_text())
    raw=np.fromfile(folder/"elevation.f32",dtype="<f4").reshape(1025,1025)
    mask=np.fromfile(folder/"coverage.u8",dtype="u1").reshape(raw.shape)
    if study.digest(folder/"elevation.f32")!=manifest["elevation"]["sha256"] or study.digest(folder/"coverage.u8")!=manifest["coverage_sha256"]:
        raise ValueError("atlas payload changed")
    elev=raw*doc["world"]["height_transform"]["scale"]+doc["world"]["height_transform"]["offset_m"]
    origin=doc["atlas"]["origin_xz_m"]; extent=[origin[0]/1000,(origin[0]+1024*256)/1000,
                                             origin[1]/1000,(origin[1]+1024*256)/1000]
    light=LightSource(315,45)
    relief=light.shade(elev,cmap=plt.get_cmap("terrain"),dx=256,dy=256,vert_exag=2,blend_mode="soft")
    relief[mask==0]=[.16,.17,.19,1]
    fig,ax=plt.subplots(figsize=(12,11),layout="constrained")
    ax.imshow(relief,extent=extent,origin="lower")
    colors=("#fd7722","#d20ca0","#006cff")
    for name,color in zip(review.SITES,colors):
        site=json.loads((atlas.WORLD/"sites"/name/"site.json").read_text());x,z=site["center_xz_m"]
        for level,lw in ((site["levels"][0],2),(site["levels"][-1],1)):
            span=level["span_m"]/1000
            ax.add_patch(Rectangle(((x-level["span_m"]/2)/1000,(z-level["span_m"]/2)/1000),span,span,
                                  fill=False,edgecolor=color,lw=lw))
        ax.plot(x/1000,z/1000,"o",color=color,label=name)
    ax.add_patch(Rectangle((-64.056,-64.056),128.112,128.112,fill=False,edgecolor="black",ls="--",lw=1.5))
    ax.set(title="Filtered native world atlas • 256 m samples • 262.144 km coverage",xlabel="World X (km)",ylabel="World Z (km)")
    ax.legend(loc="lower left");fig.savefig(out/"world-atlas.png",dpi=160);plt.close(fig)
    # A direct full-resolution raster is available for zooming without labels.
    plt.imsave(out/"world-relief.png",relief,origin="lower")
    fig,ax=plt.subplots(figsize=(10,9),layout="constrained")
    im=ax.imshow(np.ma.masked_where(mask==0,elev),extent=extent,origin="lower",cmap="terrain")
    ax.set(title="Source-matched rendered elevation (m)",xlabel="World X (km)",ylabel="World Z (km)")
    fig.colorbar(im,ax=ax,label="Metres; fixed accepted source transform")
    fig.savefig(out/"world-elevation.png",dpi=140);plt.close(fig)
    fig,ax=plt.subplots(figsize=(8,7),layout="constrained")
    ax.imshow(mask,extent=extent,origin="lower",cmap=ListedColormap(["#292d35","#65b6b0","#e9b765"]),vmin=0,vmax=2)
    ax.set(title="Coverage: teal cached native / gold generated native / grey missing",xlabel="World X (km)",ylabel="World Z (km)")
    fig.savefig(out/"world-coverage.png",dpi=120);plt.close(fig)
    # A source-normalized old-versus-new sampling comparison, same box and colour
    # range. The old conditioning map is explicitly NOT equivalent native data.
    survey=json.loads((atlas.WORLD/"survey.json").read_text())
    coarse=np.fromfile(atlas.WORLD/"survey/elevation.f32",dtype="<f4").reshape(survey["elevation"]["shape"])
    co=survey["grid"]["origin_xz_m"];s=survey["grid"]["spacing_m"]
    ce=[co[0]/1000,(co[0]+(coarse.shape[1]-1)*s)/1000,co[1]/1000,(co[1]+(coarse.shape[0]-1)*s)/1000]
    fig,axes=plt.subplots(1,2,figsize=(15,7),layout="constrained")
    lo,hi=float(raw.min()),float(raw.max())
    for ax,value,e,title in zip(axes,(coarse,raw),(ce,extent),("Before: 37² conditioning / 7.68 km","Now: 1025² filtered native / 256 m")):
        ax.imshow(value,extent=e,origin="lower",cmap="terrain",vmin=lo,vmax=hi,interpolation="nearest")
        ax.set(xlim=extent[:2],ylim=extent[2:],title=title,xlabel="World X (km)",ylabel="World Z (km)")
    fig.savefig(out/"atlas-before-after.png",dpi=130);plt.close(fig)


def image_pair(out,a,b,left="Retained",right="Refined"):
    return f'<div class="pair"><figure><a href="images/{a}.png"><img src="images/{a}.png" alt="{html.escape(left)}"></a><figcaption>{html.escape(left)}</figcaption></figure><figure><a href="images/{b}.png"><img src="images/{b}.png" alt="{html.escape(right)}"></a><figcaption>{html.escape(right)}</figcaption></figure></div>'


def report(out):
    atlas.check(out)
    refined=json.loads((out/"refined.json").read_text());perf=json.loads((out/"performance.json").read_text())
    if base.runtime()!=refined["runtime"]: raise ValueError("report runtime differs from measured render")
    if not perf["budget_pass"]: raise ValueError("failed performance gates must be resolved or explicitly retained")
    tests=ET.parse(out/"final-tests.xml").getroot()
    cases=tests.findall(".//testcase")
    required={"terrain_water_capture_tests","terrain_site_cpu","terrain_surface_patch_lod_cpu",
        "terrain_surface_context_cpu","terrain_surface_context_edge_gpu","terrain_surface_patch_lod_gap_gpu",
        "terrain_surface_patch_gap_gpu","terrain_water_capture_lifecycle_gpu","fluid_25d_daylight_gpu",
        "fluid_25d_surface_gradient_gpu","fluid_25d_whitewater_gpu","fluid_25d_scenic_gpu",
        "terrain_config_tests","terrain_source_cache_tests","terrain_world_coverage_tests",
        "terrain_world_study_helpers","terrain_world_review_helpers","terrain_atlas_filter_tests",
        "terrain_readability_review_helpers"}
    if required!={c.get("name") for c in cases} or any(c.find(tag) is not None for c in cases for tag in ("failure","error","skipped")):
        raise ValueError("failed or unexecuted final tests")
    motion=json.loads((out/"motion.json").read_text())
    if len(motion["rows"])!=3 or any(study.digest(out/r["path"])!=r["sha256"] for r in motion["rows"]):
        raise ValueError("missing or changed motion assets")
    controls=json.loads((out/"controls.json").read_text())
    for receipt in controls["assets"]+refined["assets"]:
        if "sha256" in receipt and study.digest(out/receipt["path"])!=receipt["sha256"]:
            raise ValueError("review image changed")
    m=json.loads((out/"atlas/atlas.json").read_text());attempt=json.loads((out/"atlas-attempt.json").read_text())
    diffs={label:base.pixel_difference(out/"images"/(a+".png"),out/"images"/(b+".png")) for label,a,b in (
        ("material", "valley-interior-current-wide", "valley-interior-refined-wide"),
        ("geometry", "valley-clay-clean", "valley-clay-512"))}
    rows=''.join(f'<tr><td>{r["case"]} / {r["mode"]} / {r["repeat"]+1}</td><td>{r["whole_gpu_p95_ms"]:.3f}</td><td>{r["cpu_service_p95_ms"]:.3f}</td></tr>' for r in perf["rows"])
    delta=max(r["added_gpu_p95_ms"] for r in perf["deltas"])
    terrain_rows=[r for r in perf["rows"] if r["case"]!="water"]
    medians=[]
    for r in terrain_rows:
        prefix=out/"performance"/f'{r["case"]}-{r["repeat"]}-{r["mode"]}.passes.csv'
        values=[float(p["duration_ms"]) for p in csv.DictReader(prefix.open())
                if p["kind"]=="gpu" and p["label"]=="terrain captured patch whole scene"]
        medians.append(statistics.median(values))
    variability=(f'Terrain-only medians are {min(medians):.2f}–{max(medians):.2f} ms, '
        f'but p95 varies {min(r["whole_gpu_p95_ms"] for r in terrain_rows):.2f}–'
        f'{max(r["whole_gpu_p95_ms"] for r in terrain_rows):.2f} ms across repeats in both modes. '
        'The tail variation is retained, not attributed to this material or to a verified GPU-clock cause. '
        'Negative paired p95 differences are not a claimed speedup.')
    body=f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Terrain readability — finer world atlas and restrained plains</title><style>
body{{font:16px/1.6 system-ui;background:#10181f;color:#dce4e9;max-width:1250px;margin:auto;padding:28px}}h1{{line-height:1.15}}h2{{margin-top:2em}}a{{color:#8ed4cb}}img,video{{max-width:100%;display:block;border-radius:4px}}figure{{margin:0}}figcaption{{color:#b0bec8}}.pair{{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin:18px 0}}details{{margin:18px 0;padding:16px;background:#182630}}summary{{cursor:pointer}}table{{border-collapse:collapse}}td,th{{padding:5px 15px;border-bottom:1px solid #31414a;text-align:left}}nav a{{margin-right:15px}}@media(max-width:750px){{.pair{{grid-template-columns:1fr}}}}
</style></head><body><nav><a href="/">All reports</a><a href="#atlas">World atlas</a><a href="#plains">Plains</a><a href="#controls">Controls</a><a href="#checks">Validation</a></nav>
<h1>Readable world, quieter plains</h1><p>The coarse world overview is replaced by a genuinely filtered native atlas. The plains' obvious repeating pattern mostly comes from periodic material detail, not a repeated terrain tile. The simple shared candidate suppresses that detail and keeps climate/landform colour organization. No stochastic tiling system was added.</p>
<p>Opt-in only. Same sites, terrain, lighting and water; headless Vulkan on RTX 5070 Ti. No solver, live GUI, new assets or defaults changed.</p>
<section id="atlas"><h2>1. A world map you can actually read</h2><p>1025² at 256 m across 262.144 km. {m['cached_samples']/m['total_samples']:.1%} reused cached native samples; the remainder was generated from the same pinned native producer. Full coverage in {attempt['wall_seconds']:.1f} s, under the 900 s bound. Filtered overlap: {m['max_filtered_overlap_m']:g} m.</p>
<a href="world-atlas.png"><img src="world-atlas.png" alt="Filtered world atlas and prepared site coverage"></a>
<p>Small rectangles: 16 km detail boxes. Large coloured rectangles: 131 km backdrop coverage. Dashed black: eligible site centres, including camera/sample margins. Colour indicates elevation, not simulated water. This is a display atlas, not a new runtime heightfield or hydrology result.</p>
<p>Sites: <a href="#valley-interior">Valley / plains</a> · <a href="#mountain-interior">Interior mountain</a> · <a href="#mountain-original">Original mountain + water</a>.</p>
<details><summary>Before/after sampling, elevation, coverage and zoomable raster</summary><img src="atlas-before-after.png" alt="Coarse conditioning versus filtered native world sampling"><div class="pair"><img src="world-elevation.png" alt="World elevation"><img src="world-coverage.png" alt="Cache and generated coverage"></div><a href="world-relief.png">Full-resolution relief raster</a> · <a href="atlas/atlas.json">Atlas provenance and timings</a></details></section>
<section id="plains"><h2>2. The plains comparison</h2><p>Read the broad right-side plain and far horizon first. Before: repeated small mineral/normal detail dominates the flat substrate. After: fine colour, roughness and normal contrast recede together with footprint; real terrain relief remains. The existing periodic detail texture is retained at low amplitude, not mathematically made non-periodic.</p>
{image_pair(out,'valley-interior-current-wide','valley-interior-refined-wide')}
<p>Candidate A was sufficient for this bounded pass. No random tile rotations, imported textures, new noise layers, terrain displacement or material-specific geometry refinement.</p></section>
<section id="controls"><details><summary>Why we chose material refinement instead of denser exterior geometry</summary><p>Matched 32 km camera and light. Clean clay removes normal/detail/roughness modulation too; it is not the old clay view with the material normal left enabled.</p>
{image_pair(out,'valley-interior-current-wide','valley-clay-clean','Retained material','Clean clay: underlying shape')}
{image_pair(out,'valley-detail-off','valley-clay-512','Periodic modulation off; source weights remain','Clean clay with 512 m exterior cap')}
<p>The 512 m cap changes exterior shape and reveals more terrain detail; it does not fix material repetition. It remains an explicit study control, not a default change. Core/perimeter data and water geometry are unchanged.</p>
{image_pair(out,'valley-albedo','valley-refined-albedo','Retained albedo','Filtered albedo')}</details></section>
'''
    for name in review.SITES:
        body+=f'<section id="{name}"><details><summary>{html.escape(name)} — normal framing</summary>'
        body+=image_pair(out,name+'-current-repeat',name+'-refined-wide','Retained wide','Refined wide')
        body+=f'<a href="images/{name}-refined-normal.png"><img src="images/{name}-refined-normal.png" alt="Refined normal framing"></a></details></section>'
    body+=f'''<details><summary>Existing water and shared terrain backdrop</summary><p>Water bed/depth/velocity are unchanged. Only ground material is refined. Water still stops at its finite recorded rectangle; wider terrain does not imply dry ground or simulated water outside it.</p>
{image_pair(out,'water-current-normal','water-refined-normal','Retained ground + existing water','Refined ground + SAME water')}
{image_pair(out,'backdrop-filtered-detail','backdrop-readable-detail','Established backdrop, retained','Established backdrop, shared helper')}
<p>Prepared sites/captured ground and the radial terrain backdrop both use the same filtering helper and source-world XZ phase. Their palettes and mesh products remain distinct.</p></details>
<details><summary>Short actual camera-motion checks (6 s each)</summary><p>Slow 0.24 rad orbit, not advancing hydrology. Fixed input fields; water has its existing decorative motion. Encoding is outside performance windows.</p><div class="pair"><video controls preload="metadata" src="motion/valley-interior-current.mp4"></video><video controls preload="metadata" src="motion/valley-interior-refined.mp4"></video></div><video controls preload="metadata" src="motion/water-refined.mp4"></video></details>
<section id="checks"><h2>3. Validation and cost</h2><p>{len(cases)} selected CTests passed, none skipped. Final disabled-candidate images remain pixel-exact for all three prepared sites and three legacy water cameras. Immutable GPU bed/depth/decoded-velocity checks passed; source and prepared-package SHA-256 identities are unchanged. Shared material GPU controls cover fade endpoints, flat/rock contrast, macro suppression and equivalent source-world positions. Lifecycle transitions passed headlessly.</p>
<p>Two matched 1080p repeats per case, 120 warmup frames excluded, 10 s at 60 Hz, no concurrent CUDA producer/GPU tests or image encoding. Largest candidate-minus-current GPU p95: {delta:+.3f} ms (0.3 ms gate); whole-scene GPU ≤8 ms and CPU service ≤4 ms gates passed. Local p95 is not a universal frame-time guarantee.</p>
<p>{variability}</p>
<details><summary>Timing table and reproducible receipts</summary><table><tr><th>Case / mode / repeat</th><th>GPU p95 ms</th><th>CPU service p95 ms</th></tr>{rows}</table><p><a href="performance.json">Timings</a> · <a href="controls.json">Matched controls</a> · <a href="refined.json">Final render identities</a> · <a href="motion.json">Motion receipts</a> · <a href="final-tests.xml">Tests</a> · <a href="protocol.json">Protected input protocol</a></p></details></section>
<h2>Verdict</h2><p>Keep the finer atlas. Keep the simple refined material available for review; no need to add stochastic anti-tiling complexity yet. Leave legacy defaults and exterior density unchanged until visual approval. Remaining limits: native 30 m terrain has no clutter/vegetation; exterior geometry is fixed/filtered rather than camera-streamed; far shadows/horizon and finite water edges remain unqualified.</p></body></html>'''
    (out/"index.html").write_text(body)
    study.write(out/"summary.json",{"schema":"cubey.terrain.readability-review.v1","tests":len(cases),
        "inputs_unchanged":True,"atlas_complete":m["complete"],"performance_budget_pass":True,
        "legacy_parity":refined["legacy_parity"],"image_differences":diffs,
        "default_promoted":False,"anti_tiling_system_added":False,"rendering":"headless-vulkan"})


def http(out):
    """HTTP asset/index check from this host, not remote Snap or browser acceptance."""
    refs=[]
    class Parser(HTMLParser):
        def handle_starttag(self, tag, attrs):
            for key,value in attrs:
                if key in ("src","href") and value and not value.startswith("#"):
                    refs.append(value)
    origin="http://starship:8801/"
    route=origin+str(out.relative_to(study.ROOT/"outputs"))+"/"
    with urlopen(route,timeout=10) as response:
        data=response.read().decode(); status=response.status
    Parser().feed(data)
    rows=[]
    for ref in sorted(set(refs)):
        with urlopen(Request(urljoin(route,ref),method="HEAD"),timeout=10) as response:
            rows.append({"path":ref,"status":response.status})
    with urlopen(origin,timeout=10) as response: listed=out.name in response.read().decode()
    if status!=200 or not listed or any(r["status"]!=200 for r in rows):
        raise ValueError("report/index/reference HTTP check failed")
    study.write(out/"http-check.json",{"origin":origin,"report":route,"status":status,
        "listed_in_index":listed,"navigation_injected":"cubey-report-navigation" in data,
        "references":rows,"remote_snap_checked":False,"browser_layout_checked":False})


def main():
    p=argparse.ArgumentParser(__doc__);p.add_argument("output",type=Path)
    p.add_argument("--phase",choices=("maps","report","http"),required=True)
    args=p.parse_args();globals()[args.phase](atlas.owned(args.output))


if __name__=="__main__":main()
