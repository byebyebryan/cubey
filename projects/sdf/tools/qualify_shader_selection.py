#!/usr/bin/env python3
"""Serial native qualification and interleaved timing of source shader selection."""
import argparse
import copy
import csv
import json
import math
from pathlib import Path
import re
import statistics
from check_tracing_images import pixels
from acceleration_loop import gpu_state, save
from benchmark_tracing import percentile
from investigate_guardian import ROOT, digest, invoke, metadata
from tracing_comparison import check

ROUTES = [("analytic-full", "analytic", "full", "off"),
          ("hybrid-full", "hybrid", "full", "off"),
          ("analytic-pruned", "analytic", "pruned", "off"),
          ("hybrid-pruned", "hybrid", "pruned", "off"),
          ("analytic-control", "analytic", "pruned", "control"),
          ("hybrid-control", "hybrid", "pruned", "control"),
          ("hybrid-unclamped", "hybrid", "pruned", "unclamped"),
          *[("analytic-"+v, "analytic", "pruned", v) for v in ("root", "far", "root-far")]]


def route_flags(route):
    _, mode, csg, acceleration = route
    return ["--surface-trace", mode, "--csg-evaluation", csg,
            "--region-axis", "64" if acceleration != "off" else "16", "--acceleration-variant", acceleration]


def selection_actions(output):
    initial = json.loads((ROOT/"projects/sdf/comparison/initial.json").read_text())
    initial["materials"] = initial["materials"][:1]
    one = copy.deepcopy(initial)
    two = copy.deepcopy(initial)
    two["materials"].append(dict(name="blue", base_color=[0, 0, 1], roughness=.7))
    branch = copy.deepcopy(one)
    branch["materials"][0]["base_color"] = [.2, .8, .3]
    domain = copy.deepcopy(branch)
    domain["materials"][0]["material_from_object"] = [2,0,0,0,0,2,0,0,0,0,2,0,.1,0,0,1]
    moved = copy.deepcopy(two)
    moved["root"]["children"][0]["position"] = [.03, 0, 0]
    actions = []
    def add(label, scene, route, render=None, visible=None, fault=None, unchanged=True):
        row = dict(label=label, scene=copy.deepcopy(scene),
                   visible_scene=copy.deepcopy(scene if visible is None else visible),
                   expect_shader=route, geometry_unchanged=unchanged)
        if render is not None: row["render"] = render
        if fault is not None: row.update(fault=fault, expect_failure=True)
        actions.append(row)
    add("initial one material", one, "lit-single-material")
    add("register unused second material", two, "lit-general")
    add("undo registration", one, "lit-single-material")
    add("redo registration", two, "lit-general")
    add("branch to one material and recolor", branch, "lit-single-material")
    add("normal debug", branch, "general", dict(debug=1))
    add("provenance debug", branch, "general", dict(debug=5))
    add("return to lit", branch, "lit-single-material", dict(debug=0))
    add("disable material shortcut", branch, "lit-general", dict(material_fast_path=False))
    add("material debug", branch, "general", dict(debug=6))
    add("restore shortcut and lit", branch, "lit-single-material", dict(debug=0, material_fast_path=True))
    add("appearance domain only", domain, "lit-single-material")
    add("failed second material and debug edit", two, "lit-single-material",
        dict(debug=1, material_fast_path=False), visible=domain, fault="region")
    add("retry second material", two, "lit-general")
    add("undo to one material domain", domain, "lit-single-material")
    add("failed geometry and eligibility edit", moved, "lit-single-material",
        visible=domain, fault="field")
    add("retry geometry and second material", moved, "lit-general", unchanged=False)
    add("undo geometry and registration", one, "lit-single-material", unchanged=False)
    add("redo geometry and registration", moved, "lit-general", unchanged=False)
    add("metallic debug", moved, "general", dict(debug=12))
    add("return to multi-material lit", moved, "lit-general", dict(debug=0))
    multi = copy.deepcopy(two)
    multi["root"] = dict(name="two-material-union",type="union",children=[dict(name="red-ball",type="sphere",radius=.16,position=[-.12,0,0],material="red"),dict(name="blue-ball",type="sphere",radius=.16,position=[.12,0,0],material="blue")])
    save(output/"selection-multi.json", multi)
    save(output/"selection-initial.json", initial)
    save(output/"selection-actions.json", dict(format="cubey.sdf.action-study", version=1, actions=actions))
    return actions


class Study:
    def __init__(self, args):
        self.args = args
        self.output = args.output
        self.base = [str(args.binary), "--headless", "--require-validation", "--supersample", "1"]
        self.receipt = dict(metadata(args.binary), runner_sha256=digest(Path(__file__)), runs=[])
        if args.resume:
            previous=json.loads((args.output/"qualification.json").read_text())
            if previous["binary_sha256"]!=digest(args.binary):raise RuntimeError("resume binary differs")
            self.receipt=previous
            self.receipt.setdefault("resume_runner_hashes",[]).append(digest(Path(__file__)))

    def run(self, name, flags, shader="automatic", frames=1, render_only=False):
        prefix = self.output/name
        command = self.base + flags + ["--source-shaders", shader, "--frames", str(frames),
                    "--profile-output", str(prefix), "--profile-warmup-frames", "0"]
        command += ["--headless-render-only"] if render_only else ["--output", str(prefix.with_suffix(".png"))]
        if self.args.resume and "--comparison-actions" not in flags and not render_only:
            old=next((r for r in self.receipt["runs"] if r["name"]==name),None)
            if old and old["command"]==command and old.get("image_sha256")==digest(prefix.with_suffix(".png")):
                return old
        wall = invoke(command, prefix, timeout=180)
        if "--comparison-actions" in flags:
            result_path=Path(flags[flags.index("--comparison-output")+1])
            rows=json.loads(result_path.read_text())["actions"]
            manifest=json.loads(Path(flags[flags.index("--comparison-actions")+1]).read_text())
            if len(rows)!=frames or (manifest.get("capture_frames") and any("frame_png" not in r for r in rows)):
                raise RuntimeError("incomplete action replay or missing completed-frame capture")
        metrics = list(csv.DictReader(prefix.with_suffix(".metrics.csv").open()))
        selection = {r["name"]: float(r["value"]) for r in metrics if r["category"] == "sdf.shader_selection"}
        log = prefix.with_suffix(".log").read_text()
        if "Validation Error" in log: raise RuntimeError("Vulkan validation failure")
        row = dict(name=name, command=command, shader_control=shader, wall_seconds=wall, selection=selection)
        if not render_only: row["image_sha256"] = digest(prefix.with_suffix(".png"))
        self.receipt["runs"]=[r for r in self.receipt["runs"] if r["name"]!=name]
        self.receipt["runs"].append(row)
        save(self.output/"qualification.json", self.receipt)
        return row

    def pair(self, name, flags, expected, frames=1):
        a = self.run(name+"-general", flags, "general", frames)
        b = self.run(name+"-automatic", flags, "automatic", frames)
        if a["selection"]["route"] != 0 or a["selection"]["pipeline_count"] != 1:
            raise RuntimeError("general control did not retain single general pipeline")
        if b["selection"]["route"] != expected or b["selection"]["pipeline_count"] != 3:
            raise RuntimeError("automatic native binding did not select expected pipeline")
        changed = 0
        maximum = 0
        if a["image_sha256"] != b["image_sha256"]:
            wa, ha, ca, ra = pixels(self.output/(a["name"]+".png"))
            wb, hb, cb, rb = pixels(self.output/(b["name"]+".png"))
            if (wa, ha, ca) != (wb, hb, cb): raise RuntimeError("image dimensions differ")
            for ar, br in zip(ra, rb):
                for i in range(0, len(ar), ca):
                    error=max(abs(ar[i+j]-br[i+j]) for j in range(3))
                    changed += error != 0
                    maximum=max(maximum,error)
        b["different_pixels"] = changed
        b["max_channel_error"] = maximum
        save(self.output/"qualification.json", self.receipt)
        if changed: raise RuntimeError(f"{name}: shader selection changed {changed} pixels")
        print(name, "identical; route", expected, flush=True)


def qualify(args):
    study = Study(args)
    actions = selection_actions(args.output)
    small = ["--width", "96", "--height", "64", "--authored-scene", str(args.output/"selection-initial.json")]
    # Every new SPIR-V family is loaded and drawn in its atmosphere and optical lighting form.
    for route in ROUTES:
        for lighting in ("atmosphere", "material-study"):
            study.pair(route[0]+"-"+lighting, small+route_flags(route)+["--lighting", lighting], 2)
            study.pair(route[0]+"-"+lighting+"-multi", small[:-1]+[str(args.output/"selection-multi.json")]+route_flags(route)+["--lighting",lighting], 1)
    # Real multi-material surfaces, procedural/bump appearance, all three fixed views.
    for scene, preset in (("guardian", "sculpture-guardian-limestone"), ("robot", "survey-robot-fieldkit")):
        config = json.loads((ROOT/f"projects/sdf/presets/{preset}.json").read_text())
        for view, offset in (("preset", 0), ("side", math.pi/2), ("rear", math.pi)):
            study.pair(scene+"-"+view, ["--config", f"projects/sdf/presets/{preset}.json",
                "--width", "1920", "--height", "1080", "--surface-trace", "analytic",
                "--csg-evaluation", "pruned", "--region-axis", "64", "--acceleration-variant", "root-far",
                "--yaw", str(config["yaw"]+offset)], 2 if scene == "guardian" else 1)
    # Optics retains the complete material/provenance evaluator and debug transport path.
    for preset in ("material-study/sphere-glass", "material-study/guardian-glass",
                   "environment-study/sphere-glass-sunset", "material-study/sphere-marble"):
        for debug in (0, 9) if "glass" in preset else (0, 10, 11, 12):
            name = preset.replace("/", "-")+"-debug"+str(debug)
            study.pair(name, ["--config", f"projects/sdf/presets/{preset}.json", "--width", "320", "--height", "180",
                *route_flags(ROUTES[-1]), "--debug", str(debug)], 2 if debug == 0 else 0)
            if debug == 9:
                w,h,c,rows=pixels(args.output/(name+"-automatic.png"))
                rgb=rows[h//2][(w//2)*c:(w//2)*c+3]
                exhausted=sum(row[i]>245 and row[i+1]<10 and row[i+2]>245 for row in rows for i in range(0,len(row),c))
                if not (rgb[2]>=32 and rgb[0]<10 and rgb[1]<10 and exhausted==0):
                    raise RuntimeError("bounded glass transport failed")
    # Actual geometry/history failure/retry semantics, plus one-to-two material selection.
    for mode, acceleration in (("analytic", "root-far"), ("hybrid", "unclamped")):
        flags=["--surface-trace",mode,"--csg-evaluation","pruned","--region-axis","64","--acceleration-variant",acceleration,
               "--width","96","--height","64","--authored-scene","projects/sdf/comparison/initial.json",
               "--comparison-actions","projects/sdf/comparison/actions.json", "--comparison-output",str(args.output/(mode+"-history.json"))]
        study.run(mode+"-history", flags, frames=20, render_only=True)
        for control in ("general", "automatic"):
            manifest = copy.deepcopy(actions)
            if control == "general":
                for action in manifest: action["expect_shader"] = "general"
            path=args.output/(mode+"-"+control+"-actions.json")
            save(path, dict(format="cubey.sdf.action-study",version=1,actions=manifest,capture_frames=True))
            result=args.output/(mode+"-"+control+"-replay.json")
            study.run(mode+"-"+control+"-replay", small+flags[:8]+["--comparison-actions",str(path),
                      "--comparison-output",str(result)], control, len(actions), True)
            rows=json.loads(result.read_text())["actions"]
            if len(rows)!=len(actions) or any(r["shader_pipeline_count"]!=(3 if control=="automatic" else 1) for r in rows):
                raise RuntimeError("incomplete or allocating action replay")
        general=json.loads((args.output/(mode+"-general-replay.json")).read_text())["actions"]
        automatic=json.loads((args.output/(mode+"-automatic-replay.json")).read_text())["actions"]
        for a,b in zip(general,automatic):
            if digest(Path(a["frame_png"]))!=digest(Path(b["frame_png"])):
                raise RuntimeError("transition GPU pixel parity failed: "+a["label"])
        print(mode,"20 original history +",len(actions),"eligibility actions and native per-frame pixels passed",flush=True)
    # Frozen independent compute oracle; source selection does not alter query/CSG code.
    manifest=ROOT/"projects/sdf/comparison/fixtures.json"
    study.receipt["fixtures"]=[]
    for route in ROUTES:
        result=args.output/(route[0]+"-fixtures.json")
        study.run(route[0]+"-fixtures", ["--width","32","--height","32",*route_flags(route),
                  "--comparison-input",str(manifest),"--comparison-output",str(result)], render_only=True)
        score=check(json.loads(manifest.read_text()),json.loads(result.read_text()))
        study.receipt["fixtures"].append(dict(route=route[0],score=score))
        if not score["passed"]: raise RuntimeError("frozen independent fixture failed")
    # Sampled default owns exactly its original single pipeline.
    row=study.run("sampled-default",small)
    if row["selection"]["route"] != 0 or row["selection"]["pipeline_count"]!=1:
        raise RuntimeError("sampled default changed")
    study.receipt["complete"]=True
    save(args.output/"qualification.json",study.receipt)


def appearance(args):
    study=Study(args)
    selection_actions(args.output)
    plain=json.loads((args.output/"selection-initial.json").read_text())
    patterned=copy.deepcopy(plain)
    patterned["materials"][0]["procedural"]=dict(type="marble",seed=9,scale=12,color_strength=.7,
        secondary_color=[.1,.2,.4],bump_strength=.0007)
    domain=copy.deepcopy(patterned)
    domain["materials"][0]["material_from_object"]=[2,0,0,0,0,2,0,0,0,0,2,0,.1,0,0,1]
    changed=copy.deepcopy(domain)
    changed["materials"][0]["procedural"].update(seed=17,scale=20,bump_strength=.001)
    states=[("plain",plain),("add procedural material",patterned),("change material domain",domain),
            ("change pattern and bump",changed),("undo pattern edit",domain),("redo pattern edit",changed)]
    actions=[dict(label=label,scene=scene,visible_scene=scene,geometry_unchanged=True)
             for label,scene in states]
    study.receipt["appearance_replays"]=[]
    for mode,acceleration in (("analytic","root-far"),("hybrid","unclamped")):
        flags=["--surface-trace",mode,"--csg-evaluation","pruned","--region-axis","64",
               "--acceleration-variant",acceleration,"--width","320","--height","180",
               "--authored-scene",str(args.output/"selection-initial.json")]
        for control in ("general","automatic"):
            path=args.output/f"{mode}-{control}.json"
            manifest=copy.deepcopy(actions)
            for a in manifest: a["expect_shader"]="general" if control=="general" else "lit-single-material"
            save(path,dict(format="cubey.sdf.action-study",version=1,actions=manifest,capture_frames=True))
            result=args.output/f"{mode}-{control}-receipt.json"
            study.run(f"{mode}-{control}",flags+["--comparison-actions",str(path),
                      "--comparison-output",str(result)],control,len(actions),True)
        general=json.loads((args.output/f"{mode}-general-receipt.json").read_text())["actions"]
        automatic=json.loads((args.output/f"{mode}-automatic-receipt.json").read_text())["actions"]
        for a,b in zip(general,automatic):
            if digest(Path(a["frame_png"]))!=digest(Path(b["frame_png"])):
                raise RuntimeError("appearance GPU pixel parity failed: "+a["label"])
            study.receipt["appearance_replays"].append(dict(mode=mode,label=a["label"],identical=True))
        if digest(Path(automatic[0]["frame_png"]))==digest(Path(automatic[3]["frame_png"])):
            raise RuntimeError("procedural appearance test did not change rendered pixels")
    for flags,label in ((["--ao","--clay","--ground","--occluder","--shadows","2"],"ao-clay-ground-occluder-soft-shadow"),
                        (["--no-material-fast-path"],"disabled-shortcut"),
                        (["--lighting","material-furnace"],"material-furnace")):
        study.pair(label,["--width","320","--height","180","--authored-scene",str(args.output/"selection-initial.json"),
                   *route_flags(ROUTES[-1]),*flags],1 if label=="disabled-shortcut" else 2)
    study.pair("clouded-glass",["--config","projects/sdf/presets/environment-study/sphere-glass-sunset.json",
               "--width","320","--height","180",*route_flags(ROUTES[-1]),"--clouds"],2)
    study.receipt["complete"]=True;save(args.output/"qualification.json",study.receipt)


def timing(args):
    q=json.loads((args.qualification/"qualification.json").read_text())
    if not q.get("complete") or q["binary_sha256"]!=digest(args.binary):
        raise RuntimeError("timing requires completed qualification of this binary")
    receipt=dict(metadata(args.binary),runner_sha256=digest(Path(__file__)),frames=180,warmup=30,runs=[])
    cases=[(scene,preset,view,offset,control) for scene,preset in
           (("guardian","sculpture-guardian-limestone"),("robot","survey-robot-fieldkit"))
           for view,offset in (("preset",0),("side",math.pi/2),("rear",math.pi))
           for control in ("general","automatic")]
    for repeat in range(1,4):
        for scene,preset,view,offset,control in cases if repeat%2 else reversed(cases):
            prefix=args.output/f"{scene}-{view}-{control}-r{repeat}"
            config=json.loads((ROOT/f"projects/sdf/presets/{preset}.json").read_text())
            command=[str(args.binary),"--headless","--headless-render-only","--require-validation","--supersample","1",
                     "--config",f"projects/sdf/presets/{preset}.json","--width","1920","--height","1080",
                     "--surface-trace","analytic","--csg-evaluation","pruned","--region-axis","64",
                     "--acceleration-variant","root-far","--source-shaders",control,"--yaw",str(config["yaw"]+offset),
                     "--frames","180","--profile-warmup-frames","30","--profile-output",str(prefix)]
            before=gpu_state();wall=invoke(command,prefix,timeout=180);after=gpu_state()
            groups={}
            for row in csv.DictReader(prefix.with_suffix(".passes.csv").open()):
                if row["kind"]=="gpu":groups.setdefault(row["label"],[]).append(float(row["duration_ms"]))
            if len(groups.get("sdf.whole_scene",[]))!=150:raise RuntimeError("incomplete GPU timestamp tail")
            metrics=list(csv.DictReader(prefix.with_suffix(".metrics.csv").open()))
            selection={r["name"]:float(r["value"]) for r in metrics if r["category"]=="sdf.shader_selection"}
            receipt["runs"].append(dict(scene=scene,view=view,control=control,repeat=repeat,command=command,
                gpu_before=before,gpu_after=after,wall_seconds=wall,selection=selection,
                passes={k:dict(samples=len(v),median_ms=statistics.median(v),p95_ms=percentile(v,.95)) for k,v in groups.items()}))
            save(args.output/"timing.json",receipt)
            print(prefix.name,round(receipt["runs"][-1]["passes"]["sdf.whole_scene"]["p95_ms"],4),flush=True)
    receipt["complete"]=True;save(args.output/"timing.json",receipt)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase",choices=["qualify","appearance","timing"])
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--qualification",type=Path)
    parser.add_argument("--resume",action="store_true",help="Reuse verified static capture receipts for the identical binary; rerun all action and oracle jobs")
    parser.add_argument("--binary",type=Path,default=ROOT/"build/release/projects/sdf/sdf")
    args=parser.parse_args();args.binary=args.binary.resolve();args.output=args.output.resolve()
    args.output.mkdir(parents=True,exist_ok=args.resume)
    {"qualify":qualify,"appearance":appearance,"timing":timing}[args.phase](args)


if __name__=="__main__":main()
