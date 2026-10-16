#!/usr/bin/env python3
"""Headless promotion checks. No solver or GUI; never rewrites the approved report."""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET
import review_captured_water_v1 as base
import review_terrain_readability_v1 as review
import study_terrain_atlas_v1 as atlas
import study_terrain_world_v1 as study

APPROVED=study.ROOT/"outputs/terrain/readability-20261012-a1"
FLUID_BASE=study.ROOT/"outputs/terrain/captured-water-paired-20261011-JK0Nmf/baseline-fluid"
FLUID=study.ROOT/"build/release/projects/fluid/fluid_25d/fluid_25d"


def without(cmd, option):
    cmd=list(cmd);i=cmd.index(option);del cmd[i:i+2];return cmd


def owned(out):
    out=out.resolve()
    if out.parent!=study.ROOT/"outputs/terrain" or not out.name.startswith("readability-defaults-"):
        raise ValueError("fresh outputs/terrain/readability-defaults-* leaf required")
    return out


def run(out):
    out=owned(out)
    if out.exists():raise ValueError("refuse existing promotion output")
    out.mkdir()
    frozen={"packages":atlas.inputs(),"protected":study.protected(),"runtime":base.runtime(),
            "fluid_app_sha256":study.digest(FLUID)}
    rows=[]
    def capture(label,cmd,reference=None):
        path=out/(label+".png")
        receipt=base.run(cmd+["--frames","1","--width","1280","--height","720",
                              "--output",str(path)],out/(label+".log"))
        if reference:
            difference=base.pixel_difference(path,reference)
            if difference["max_abs_8bit"]:raise ValueError("promotion changed approved/reference pixels: "+label)
            receipt.update(reference=str(reference),pixel_difference=difference)
        rows.append(dict(label=label,path=path.name,sha256=study.digest(path),**receipt))
        return path
    for name in review.SITES:
        cmd=without(without(review.command(name),"--terrain-readability"),"--terrain-context-max-spacing")
        capture(name,cmd+["--terrain-water-audit"],APPROVED/"images"/(name+"-refined-wide.png"))
    cmd=without(without(review.command("water",span=11457.6),"--terrain-readability"),"--terrain-context-max-spacing")
    capture("water-default",cmd+["--terrain-water-audit"],APPROVED/"images/water-refined-normal.png")
    capture("backdrop-default",["rtk","proxy",str(base.APP),"--headless","--terrain-heightfield",str(study.SOURCE)],
            APPROVED/"images/backdrop-readable-detail.png")
    capture("previous-reference",review.command("valley-interior"),APPROVED/"images/valley-interior-current-wide.png")
    terrain_small=capture("water-small-default",base.command("overview",reference=False)+["--terrain-water-audit"])
    study.write(out/"previous-ground.json",{"schema":"cubey.fluid25d.scenic-material.v2","terrain_readability":0})
    record=json.loads((FLUID_BASE/"manifest.json").read_text())["assets"][0]
    cmd=list(record["command"]);cmd[2]=str(FLUID);cmd=without(cmd,"--output");cmd=without(cmd,"--frames")
    current=capture("fluid-macro-default",cmd)
    capture("fluid-previous-reference",cmd+["--fluid25d-scenic-tuning",str(out/"previous-ground.json")],FLUID_BASE/"overview.png")
    difference=base.pixel_difference(terrain_small,current)
    if difference["max_abs_8bit"]>1:raise ValueError("shared default Terrain/Fluid parity exceeds retained attachment rounding")
    log=(out/"fluid-macro-default.log").read_text()
    if ('"material_world_center_xz_m":[11520.0,19200.0]' not in log or
        '"terrain_readability":2.0' not in log or "hydraulic_dispatches=0" not in log):
        raise ValueError("missing world-origin/default/immutable recording receipt")
    if frozen!={"packages":atlas.inputs(),"protected":study.protected(),"runtime":base.runtime(),
                "fluid_app_sha256":study.digest(FLUID)}:
        raise ValueError("promotion inputs/runtime changed")
    study.write(out/"checks.json",{"frozen":frozen,"assets":rows,"default_promoted":True,
        "terrain_fluid_default_parity":difference,"geometry_rain_water_unchanged":True,
        "rendering":"headless-vulkan","solver_started":False})
    figures=''.join(f'<figure><img src="{r["path"]}" alt="{r["label"]}"><figcaption>{r["label"]}</figcaption></figure>' for r in rows[:5])
    (out/"index.html").write_text(f'''<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Terrain readable material — default promotion</title>
<style>body{{font:16px/1.6 system-ui;max-width:1150px;margin:auto;padding:24px;background:#101820;color:#e0e8ed}}a{{color:#8ed4cb}}img{{max-width:100%}}figure{{margin:20px 0}}</style></head><body>
<a href="/">All reports</a><h1>Approved material is now the default</h1><p>Default prepared sites, captured water and radial backdrop reproduce the approved opt-in images pixel-exactly. Fluid's Macro preset uses the same shared material and its validated source-world origin. The previous ground treatment remains selectable and pixel-exact. Terrain/Fluid default rounding difference: {difference['max_abs_8bit']} / 255.</p>
<p>No terrain, water fields, rainfall, mesh density or bank defaults changed. Headless only; no live solver or GUI run. <a href="checks.json">Commands, hashes and parity checks</a> · <a href="../readability-20261012-a1/">Original matched study and measured performance</a>.</p>{figures}</body></html>''')


def finalize(out):
    out=owned(out);receipt=json.loads((out/"checks.json").read_text())
    frozen={"packages":atlas.inputs(),"protected":study.protected(),"runtime":base.runtime(),
            "fluid_app_sha256":study.digest(FLUID)}
    if receipt["frozen"]!=frozen:raise ValueError("promotion inputs/runtime changed after capture")
    for row in receipt["assets"]:
        if study.digest(out/row["path"])!=row["sha256"]:raise ValueError("changed promotion image")
    cases=ET.parse(out/"tests.xml").getroot().findall(".//testcase")
    required={"fluid_25d_tests","fluid_25d_terrain_surface","terrain_config_tests",
        "terrain_water_capture_lifecycle_gpu","fluid_25d_surface_gradient_gpu",
        "fluid_25d_scenic_gpu","fluid_25d_whitewater_gpu"}
    if {c.get("name") for c in cases}!=required or any(c.find(t) is not None for c in cases for t in ("failure","error","skipped")):
        raise ValueError("missing/failed/skipped promotion checks")
    receipt["tests"]={"count":len(cases),"sha256":study.digest(out/"tests.xml"),"skipped":0}
    study.write(out/"checks.json",receipt)
    page=out/"index.html";body=page.read_text()
    marker='<p id="validation">All seven focused tests passed, none skipped. <a href="tests.xml">Final test receipt</a>.</p>'
    if 'id="validation"' not in body:page.write_text(body.replace('</body>',marker+'</body>'))


if __name__=="__main__":
    p=argparse.ArgumentParser(__doc__);p.add_argument("output",type=Path)
    p.add_argument("--finalize",action="store_true");args=p.parse_args()
    (finalize if args.finalize else run)(args.output)
