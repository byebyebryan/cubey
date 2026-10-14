#!/usr/bin/env python3
"""Compact frozen-field wide-view study; publish the dedicated export separately."""
import argparse
import html
import json
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

import review_wide_view_v1 as trial

base = trial.base
study = trial.study
LABELS = {"local32": "Local mountain · 32m water", "wide32": "Wide mountain · 32m water",
          "wide64": "Wide mountain · 64m water", "valley64": "Wide valley · 64m water",
          "boundary32": "Finite water boundary · 32m water"}
VARIANT_LABELS = {"default": "Current shaded water", "hidden": "Water hidden (bed still wet)",
    "clay": "Clean clay · water hidden", "clay-no-shadow": "Clean clay · no sun shadows",
    "no-shadow": "Shaded water · no sun shadows", "hidden-no-shadow": "Water hidden · no sun shadows",
    "uniform": "Denser uniform draw · same shadow fit", "uniform-clay": "Denser uniform draw · clean clay",
    "normal": "Geometry normals · water hidden"}
REQUIRED_TESTS = {
    "terrain_water_capture_tests", "terrain_backdrop_surface_tests", "terrain_surface_field_tests",
    "terrain_landscape_catalog_cpu", "terrain_site_cpu", "terrain_surface_patch_lod_cpu",
    "terrain_world_water_lod_cpu", "terrain_site_water_cpu", "terrain_surface_context_cpu",
    "terrain_surface_context_edge_gpu", "terrain_surface_patch_lod_gap_gpu", "terrain_surface_patch_gap_gpu",
    "terrain_water_capture_lifecycle_gpu", "fluid_25d_terrain_alignment", "fluid_25d_terrain_demo_contract",
    "fluid_25d_terrain_surface", "fluid_25d_surface_gradient_gpu", "fluid_25d_external_session",
    "fluid_25d_terrain_presentation_review_helpers", "fluid_25d_offscreen_performance_helpers",
    "fluid_25d_terrain_demo_launcher", "fluid_25d_terrain_site_catalogue", "fluid_25d_world_water_study_helpers",
    "fluid_25d_external_worker_host", "fluid_25d_external_worker_build_helpers", "fluid_25d_terrain_demo_supervisor",
    "fluid_25d_terrain_demo_evidence_helpers", "fluid_25d_terrain_review_helpers", "fluid_25d_whitewater_gpu",
    "fluid_25d_scenic_gpu", "fluid_25d_tests", "terrain_landscape_prepare_helpers",
    "terrain_prepared_landscape_review_helpers", "terrain_config_tests", "terrain_source_catalog_tests",
    "terrain_site_package_helpers", "terrain_surface_model_tests", "terrain_capture_preparation_cpu",
    "terrain_landscape_hardening_review_helpers", "terrain_landscape_presentation_cpu",
    "terrain_landscape_first_use_review_helpers", "terrain_wide_view_study_helpers"}


def close(out):
    protocol = trial.verify(out)
    stats = json.loads((Path(protocol["baseline"]) / "field-statistics.json").read_bytes())
    rows = []
    for filtered in (False, True):
        name = "close-filtered" if filtered else "close-control"
        args = trial.command("local32", "default", stats, filtered)
        index = args.index("--terrain-water-span") + 1
        args[index] = "6500"
        receipt = base.checked(args + ["--frames", "1", "--width", "1280", "--height", "720",
            "--terrain-water-audit", "--output", str(out / (name + ".png"))], out / (name + ".log"))
        if "terrain_water_capture_immutable: GPU bed/depth/decoded-velocity exact" not in (out / (name + ".log")).read_text():
            raise ValueError("close immutable audit missing")
        rows.append({"filtered": filtered, "image": name + ".png", **receipt})
    difference = base.render.pixel_difference(out / "close-control.png", out / "close-filtered.png")
    near = trial.crop_difference(out / "close-control.png", out / "close-filtered.png", [320,540,960,720])
    base.write(out / "close.json", {"rows": rows, "whole": difference, "near": near})
    trial.verify(out)


def validate(out):
    protocol = trial.verify(out)
    baseline = Path(protocol["baseline"])
    captures = json.loads((baseline / "captures.json").read_bytes())
    expected = {(case, variant) for case, variants in study.MATRIX.items() for variant in variants}
    if len(captures["rows"]) != 29 or {(r["case"],r["variant"]) for r in captures["rows"]} != expected:
        raise ValueError("incomplete baseline matrix")
    candidate = json.loads((out / "captures.json").read_bytes())
    controls = {(c,"default") for c in study.CASES} | {("wide64","clay-no-shadow"),("wide64","hidden-no-shadow")}
    if (len(candidate["rows"]) != 14 or
        {(r["case"],r["variant"],r["filtered"]) for r in candidate["rows"]} !=
        {(c,v,f) for c,v in controls for f in (False,True)} or
        len(candidate["default_parity"]) != 7 or
        {(r["case"],r["variant"]) for r in candidate["default_parity"]} != controls or
        any(r["difference"]["max_abs_8bit"] != 0 for r in candidate["default_parity"])):
        raise ValueError("incomplete trial or changed default pictures")
    performance = json.loads((out / "performance.json").read_bytes())
    expected_perf = {(c,v,f,r) for c,v,f in (("local32","default",False),("local32","default",True),
        ("wide64","default",False),("wide64","default",True),("wide64","uniform",False)) for r in range(2)}
    if len(performance["rows"]) != 10 or {(r["case"],r["variant"],r["filtered"],r["repeat"]) for r in performance["rows"]} != expected_perf:
        raise ValueError("incomplete reversed-order timing controls")
    if any(not r["budget_pass"] or not (0 <= r["whole_gpu_p95_ms"] <= 8) or not (0 <= r["cpu_service_p95_ms"] <= 4)
           for r in performance["rows"] if r["variant"] == "default"):
        raise ValueError("current/candidate scene or CPU budget failed")
    tests = ET.parse(out / "regression-tests.xml").getroot().findall(".//testcase")
    if not REQUIRED_TESTS <= {t.get("name") for t in tests} or any(
        t.find("failure") is not None or t.find("error") is not None or t.find("skipped") is not None for t in tests):
        raise ValueError("missing/failed/skipped regression groups")
    detail = json.loads((out / "close.json").read_bytes())
    if len(detail["rows"]) != 2 or {r["filtered"] for r in detail["rows"]} != {False,True}:
        raise ValueError("missing close pair")
    return protocol, captures, candidate, performance, tests, detail


def report(out, export_name="export"):
    protocol, captures, candidate, performance, tests, detail = validate(out)
    baseline = Path(protocol["baseline"])
    if Path(export_name).name != export_name or export_name in (".",".."):
        raise ValueError("export must be a fresh owned leaf")
    export = out / export_name
    export.mkdir()
    for source, rows in ((baseline,captures["rows"]),(out,candidate["rows"]),(out,detail["rows"])):
        for row in rows:
            shutil.copy2(source / row["image"],export / row["image"])
    for source, names in ((baseline,("protocol.json","field-statistics.json")),
                          (out,("protocol.json","captures.json","performance.json","close.json","regression-tests.xml"))):
        for name in names:
            target = "baseline-" + name if source == baseline and name == "protocol.json" else name
            shutil.copy2(source / name,export / target)
    stats = json.loads((baseline / "field-statistics.json").read_bytes())
    data = {"labels": LABELS, "variants": VARIANT_LABELS, "matrix": study.MATRIX,
            "trial": sorted({r["case"]+"/"+r["variant"] for r in candidate["rows"]}),
            "notes": {
                "local32": "Near detail is retained; the filter mainly changes distant or grazing pixels. 65.5km native water rectangle; wider ground continues without water.",
                "wide32": "Same 32m saved state as Local mountain, only the camera is farther away. The 64m case is a DIFFERENT saved state, not another LOD ring.",
                "wide64": "131km independent storm state. Use clean clay/no shadows to isolate terrain grain, then trial filtering to see what it actually fixes.",
                "valley64": "Independent 131km valley storm state. Broad shallow sheets and deep pools remain; this is not a permanent-water inventory.",
                "boundary32": "Deliberately wet finite north edge. The straight water cutoff remains visible. Detail mesh and shadow fit stay at the site centre; they do not follow this camera."}}
    stat_table = ''.join(f'<tr><td>{html.escape(r["id"])}</td><td>{r["percentiles_m"]["0.5"]*100:.1f} cm</td><td>{r["percentiles_m"]["0.9"]:.2f} m</td><td>{r["maximum_m"]:.1f} m</td><td>{r["boundary_max_depth_m"]:.2f} m</td></tr>' for r in stats["rows"])
    perf_table = ''.join(f'<tr><td>{r["case"]} · {"filter trial" if r["filtered"] else r["variant"]} · repeat {r["repeat"]+1}</td><td>{r["whole_gpu_p95_ms"]:.3f}</td><td>{r["cpu_service_p95_ms"]:.3f}</td><td>{"pass" if r["budget_pass"] else "outside 8ms scene gate"}</td></tr>' for r in performance["rows"])
    paired = []
    for case in ("local32","wide64"):
        for repeat in range(2):
            a = next(r for r in performance["rows"] if r["case"] == case and r["repeat"] == repeat and r["variant"] == "default" and not r["filtered"])
            b = next(r for r in performance["rows"] if r["case"] == case and r["repeat"] == repeat and r["filtered"])
            paired.append({"case": case,"repeat":repeat,"p95_difference_ms":b["whole_gpu_p95_ms"]-a["whole_gpu_p95_ms"],
                "fraction":b["whole_gpu_p95_ms"]/a["whole_gpu_p95_ms"]-1})
    receipt = {"verdict":"retain one explicit lighting-filter study option; no default promotion",
        "native_gui":"deferred", "baseline_views":29,"trial_views":14,"close_views":2,
        "default_pixel_exact_controls":7,"regression_groups":len(tests),"input_files":len(protocol["inputs"]),
        "timing_pairs":paired,"runtime":protocol["runtime"],
        "scope":"Same immutable captures; one approximate lighting normal, no solver/new states/geometry/water-coverage changes"}
    base.write(export / "summary.json",receipt)
    document = '''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Wide landscapes: grain, flooding and coverage</title><style>
body{margin:0;background:#141a20;color:#e5eaf0;font:16px/1.55 system-ui}main{max-width:1500px;margin:auto;padding:24px}h1{font-size:clamp(1.6rem,3vw,2.2rem);line-height:1.2}h2{margin-top:32px;font-size:1.3rem}a{color:#9cdae5}p{max-width:1000px}.muted{color:#aebac6}.controls{display:flex;gap:12px;flex-wrap:wrap;align-items:center;margin:16px 0}select,button{color:inherit;background:#26323d;border:1px solid #758796;border-radius:5px;padding:9px;font:inherit;max-width:100%}button:focus-visible,select:focus-visible{outline:3px solid #bfe4fa}.pair{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}figure{margin:0}img{width:100%;height:auto;background:#222}figcaption{color:#c7d3de;font-size:.9rem}.scroll{overflow:auto}table{border-collapse:collapse;width:100%;font-size:.9rem}td,th{text-align:left;padding:8px;border-bottom:1px solid #44515e}details{margin:20px 0}summary{cursor:pointer}.notice{border-left:4px solid #86b8bb;padding:8px 16px;background:#1e2932}code{overflow-wrap:anywhere}#focus-pair{scroll-margin-top:16px}@media(max-width:800px){.pair{grid-template-columns:1fr}main{padding:16px}}
</style></head><body><main>
<h1>Wide landscapes: separate terrain grain from real flooding</h1>
<p class="notice">The inexpensive lighting-normal trial reduces distant terrain peppering. It does <em>not</em> remove the shallow blue water, change simulated fields, or repair the finite capture edge. Retained as an explicit study option; existing rendering and startup defaults remain unchanged.</p>
<p>All images are hardware headless Vulkan, held at the same saved time: <strong>12 simulated hours of 512 mm/h rain</strong>. This deliberately exaggerated storm is not normal weather. No solver or new terrain generation ran in this study.</p>
<h2>Matched comparison</h2><p>Start here: left is current lighting; right is the same scene with only the trial lighting filter. Inspect distant mountain slopes, not the blue water. Then choose clean clay/no shadows and toggle filtering: grain survives without material detail or sun shadows. “Water hidden” leaves captured wet-bed shading intact.</p>
<div class="controls"><label>View <select id="view" disabled><option value="wide64">Wide mountain · 64m water</option></select></label><label>Right-hand treatment <select id="treatment" disabled><option value="default">Current shaded water</option></select></label><label><input id="filter" type="checkbox" checked disabled> Trial filter on right</label><button id="flip" disabled>Swap sides</button></div>
<div class="pair" id="focus-pair"><figure><a id="left-link" href="wide64-default.png"><img id="left" src="wide64-default.png" alt="Wide mountain current shaded saved water" width="1280" height="720"></a><figcaption id="left-label">Current shaded water</figcaption></figure><figure><a id="right-link" href="wide64-default-filtered.png"><img id="right" src="wide64-default-filtered.png" alt="Wide mountain same saved water with trial ground lighting filter" width="1280" height="720"></a><figcaption id="right-label">Same scene · trial ground lighting</figcaption></figure></div>
<p id="caption" class="muted">131km independent storm state. Click an image for its original 1280×720 PNG. This is not coupled water LOD.</p><noscript><p>JavaScript is off; this static comparison and all evidence below remain usable.</p></noscript>
<h2>What worked—and what did not</h2><ul><li><strong>Terrain lighting:</strong> a wider finite-difference bed stencil reduces distant slope-normal variation. It fades in at 1–3 native cells per pixel, caps its radius at 8 cells, and fades out at the capture/context seam. It is an approximation, not a filtered height-mip hierarchy.</li><li><strong>More triangles alone:</strong> the expensive uniform control retains distant grain. It is a geometry reference, not the recommended production draw.</li><li><strong>Shadows off:</strong> does not cure this grain. Far receivers remain outside the qualified core-focused shadow area. Setting strength to zero still renders the shadow map; it is not a performance optimization.</li><li><strong>Blue films and pools:</strong> much of the water is genuinely shallow saved storm flooding. Rendering still determines how prominently those cells read; this study does not claim every visible artifact is explained.</li><li><strong>Finite edge:</strong> water stops at the capture boundary even where native boundary depth is nonzero. Filtering cannot supply missing simulation coverage, and no fog hides it.</li></ul>
<details><summary>Close-view preservation</summary><div class="pair"><figure><a href="close-control.png"><img src="close-control.png" alt="6500m-span original close view" loading="lazy"></a><figcaption>Current lighting · 6.5km camera span</figcaption></figure><figure><a href="close-filtered.png"><img src="close-filtered.png" alt="6500m-span filtered trial close view" loading="lazy"></a><figcaption>Trial · near pixels retain original normals</figcaption></figure></div><p>@@CLOSE@@ Pixel differences are image-space checks, not perceptual quality scores; steep grazing pixels can activate the filter even in a close view.</p></details>
<h2>What is actually saved in the water fields?</h2><div class="scroll"><table><thead><tr><th>Independent capture</th><th>Median native depth</th><th>90th percentile</th><th>Maximum</th><th>North-edge maximum</th></tr></thead><tbody>@@STATS@@</tbody></table></div><p class="muted">These are native-cell depth statistics, not projected wet area, calibrated hydrology, equilibrium, or a permanent-lake inventory. Same source does not mean the 32m and 64m storm states are interchangeable.</p>
<details><summary>Rendering cost and qualification</summary><p>RTX 5070 Ti, 1920×1080 genuine offscreen Vulkan, two reversed-order repetitions, 120 warmup frames omitted, 60Hz rendering cap. No solver, GUI, encoding or image readback during timings. Scene gate ≤8ms GPU p95; CPU service ≤4ms. The uniform reference is allowed to fail, but its failure remains visible.</p><div class="scroll"><table><thead><tr><th>View · draw · repeat</th><th>GPU p95 ms</th><th>CPU p95 ms</th><th>Budget</th></tr></thead><tbody>@@PERF@@</tbody></table></div><p>@@COST@@ Paired p95 differences include run variance; they are not isolated shader attribution or claims of statistically significant speedup.</p><p>@@TESTS@@ regression groups passed. All 29 baseline ablations and 14 trial frames audit exact GPU bed/depth/decoded velocity; both close frames do too. Seven re-rendered default controls remain pixel-exact after the optional filter was added.</p></details>
<h2>Verdict and next bounded step</h2><p>Keep <code>--terrain-readability filtered-study</code> as one explicit saved-water-ground experiment, with <code>refined</code> unchanged as default. It is not promoted to the normal radial backdrop or live Fluid. Native GUI/default review is deferred.</p><p>If we pursue the remaining distant water shimmer, next isolate continuous water coverage and water lighting at pixel footprints spanning multiple cells, on these same immutable captures. Use separate coverage/normal controls; do not alter rain or add more terrain during that rendering study. A true source-aligned filtered height/normal hierarchy is the larger fallback if this stencil fails temporal or seam review.</p>
<details><summary>Established references and evidence</summary><p><a href="https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry">GPU Gems 2: GPU geometry clipmaps</a> describes prefiltered height hierarchies, level-matched normal blending, and a four-filtered-height-sample pixel-normal alternative. That supports the filtering direction, not an assertion that this sparse Cubey stencil is equivalent or universally anti-aliased.</p><p><a href="baseline-protocol.json">Baseline freeze</a> · <a href="field-statistics.json">Depth statistics</a> · <a href="protocol.json">Trial runtime/inputs</a> · <a href="captures.json">Parity/region differences</a> · <a href="close.json">Close comparison</a> · <a href="performance.json">Raw timing summary</a> · <a href="regression-tests.xml">Regression results</a> · <a href="summary.json">Verdict receipt</a></p></details>
</main><script>
const data=@@DATA@@; const byId=id=>document.getElementById(id); let swapped=false;
const view=byId('view'),treatment=byId('treatment'),filter=byId('filter');
view.innerHTML=Object.entries(data.labels).map(([id,label])=>`<option value="${id}">${label}</option>`).join('');view.value='wide64';
function options(){const old=treatment.value; treatment.innerHTML=data.matrix[view.value].map(id=>`<option value="${id}">${data.variants[id]}</option>`).join('');treatment.value=data.matrix[view.value].includes(old)?old:'default';update();}
function update(){const v=view.value,t=treatment.value;const available=data.trial.includes(v+'/'+t);filter.disabled=!available;if(!available)filter.checked=false;
let a={file:v+'-default.png',label:data.labels[v]+' · Current shaded water'},b={file:v+'-'+t+(filter.checked?'-filtered':'')+'.png',label:data.labels[v]+' · '+data.variants[t]+(filter.checked?' · Trial lighting filter':'')};if(swapped)[a,b]=[b,a];
for(const [id,item]of [['left',a],['right',b]]){byId(id).src=item.file;byId(id).alt=item.label;byId(id+'-link').href=item.file;byId(id+'-label').textContent=item.label;}
byId('caption').textContent=data.notes[v]+' Click either image for the original PNG. Other view/cell-size pairs can represent different saved states.';}
view.onchange=options;treatment.onchange=update;filter.onchange=update;byId('flip').onclick=()=>{swapped=!swapped;update()};view.disabled=treatment.disabled=byId('flip').disabled=false;options();
</script></body></html>'''
    close_text = f'Central near crop [320,540–960,720]: mean difference {detail["near"]["mean_abs_8bit"]:.6f}/255; {detail["near"]["different_channels"]} changed channels.'
    cost_text = 'Observed paired filter-minus-control GPU p95 range: ' + f'{min(p["p95_difference_ms"] for p in paired):+.3f} to {max(p["p95_difference_ms"] for p in paired):+.3f} ms.'
    for key, value in {"CLOSE":close_text,"STATS":stat_table,"PERF":perf_table,"COST":cost_text,
                       "TESTS":str(len(tests)),"DATA":json.dumps(data).replace('<','\\u003c')}.items():
        document=document.replace('@@'+key+'@@',value)
    with (export / "index.html").open("x") as stream: stream.write(document)
    base.write(out / ("report-receipt.json" if export_name == "export" else "report-receipt-"+export_name+".json"),{**receipt,"export_assets":{
        p.name:base.render.digest(p) for p in sorted(export.iterdir()) if p.is_file()}})
    trial.verify(out)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out",type=Path)
    parser.add_argument("--export-name",default="export")
    parser.add_argument("--phase",choices=("close","report"),required=True)
    args=parser.parse_args()
    if args.phase == "report": report(args.out.resolve(),args.export_name)
    else: close(args.out.resolve())


if __name__ == "__main__": main()
