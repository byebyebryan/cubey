#!/usr/bin/env python3
"""Serial diagnostics of the qualified automatic Guardian source shader."""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import subprocess
from acceleration_loop import gpu_state, save
from benchmark_tracing import percentile
from query_study import ROOT, digest, invoke, metadata

CASES=[("selected",[],"lit_root_far_sdf_analytic_pruned.frag.spv@single")]
CASES += [(stage,["--frame-profile","opt-"+stage],"profile_opt-"+stage+".frag.spv")
          for stage in ("full","primary","normal","material","shadow","no-sky")]
CASES += [("no-shadows",["--shadows","0"],CASES[0][2]),
          ("no-procedural",["--no-procedural","--no-bump"],CASES[0][2]),
          ("no-bump",["--no-bump"],CASES[0][2])]
VIEWS=[("preset",0),("side",math.pi/2),("rear",math.pi)]


def base(args,view,width=1920,height=1080):
    offset=dict(VIEWS)[view]
    return [str(args.binary),"--headless","--require-validation","--supersample","1",
        "--config","studies/sdf/guardian/presets/sculpture-guardian-limestone.json",
        "--surface-trace","analytic","--csg-evaluation","pruned","--region-axis","64",
        "--acceleration-variant","root-far","--width",str(width),"--height",str(height),
        "--yaw",str(.4+offset)]


def provenance(args):
    return dict(metadata(args.binary),selected_runner_sha256=digest(Path(__file__)),
                scope="Static private diagnostics; no optimization or quality/default adoption",
                runs=[])


def qualify(args):
    receipt=provenance(args)
    capture_cases=[CASES[0],CASES[1],("legacy-specialized",["--frame-profile","specialized"],"profile_specialized.frag.spv")]
    if args.phase=="cull-qualify":capture_cases=[CASES[0],CASES[1],("cull-dark-shadows",["--frame-profile","opt-cull-dark-shadows"],"profile_opt-cull-dark-shadows.frag.spv")]
    if args.phase=="cull-qualify":capture_cases.append(("reorder-shadow",["--frame-profile","opt-reorder-shadow"],"profile_opt-reorder-shadow.frag.spv"))
    for view,_ in VIEWS:
        image_hashes={}
        for name,flags,shader in capture_cases:
            prefix=args.output/(view+"-"+name)
            command=base(args,view)+["--frames","1","--output",str(prefix.with_suffix(".png")),*flags]
            invoke(command,prefix,timeout=180)
            image_hashes[name]=digest(prefix.with_suffix(".png"))
            receipt["runs"].append(dict(view=view,name=name,command=command,image_sha256=image_hashes[name]))
        save(args.output/"qualification.json",receipt)
        if len(set(image_hashes.values()))!=1:raise RuntimeError("full diagnostic differs from actual selected route")
    receipt["negative"]=[]
    for label,flags in [("debug",["--debug","1"]),("shortcut-disabled",["--no-material-fast-path"]),
                        ("multiple-materials",["--authored-scene","projects/sdf/comparison/initial.json"]),
                        ("hybrid",["--surface-trace","hybrid"]),("stage",["--comparison-stage","primary"])]:
        command=base(args,"rear")+["--frame-profile","opt-full","--headless-render-only","--frames","1",*flags]
        run=subprocess.run(command,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (args.output/(label+".log")).write_text(run.stdout)
        expected = "acceleration variant does not match" if label == "hybrid" else "requires"
        if not run.returncode or expected not in run.stdout:raise RuntimeError("unexpected negative-profile result: "+label)
        receipt["negative"].append(dict(label=label,command=command,exit=run.returncode,message=run.stdout.strip()))
    receipt["complete"]=True;save(args.output/"qualification.json",receipt)


def timing(args):
    q=json.loads((args.qualification/"qualification.json").read_text())
    if not q.get("complete") or q["binary_sha256"]!=digest(args.binary):raise RuntimeError("require qualified binary")
    receipt=provenance(args)
    receipt.update(frames=180,warmup=30)
    cases=[(view,name,flags,shader,1920,1080) for view,_ in VIEWS for name,flags,shader in CASES]
    if args.phase=="cull":
        cases=[(view,name,flags,shader,1920,1080) for view,_ in VIEWS for name,flags,shader in [CASES[0],("cull-dark-shadows",["--frame-profile","opt-cull-dark-shadows"],"profile_opt-cull-dark-shadows.frag.spv"),("reorder-shadow",["--frame-profile","opt-reorder-shadow"],"profile_opt-reorder-shadow.frag.spv")]]
    if args.phase=="scale":
        cases=[(view,"selected",[],CASES[0][2],w,h) for view,_ in VIEWS for w,h in [(960,540),(1280,720)]]
    if args.phase=="pilot":
        cases=[("rear",name,flags,shader,1920,1080) for name,flags,shader in CASES]
    for repeat in range(1,2 if args.phase=="pilot" else 4):
        for view,name,flags,shader,width,height in cases if repeat%2 else reversed(cases):
            prefix=args.output/f"{view}-{name}-{width}-r{repeat}"
            frames,warmup=(20,4) if args.phase=="pilot" else (180,30)
            command=base(args,view,width,height)+["--headless-render-only","--frames",str(frames),
                "--profile-warmup-frames",str(warmup),"--profile-output",str(prefix),*flags]
            before=gpu_state();wall=invoke(command,prefix,timeout=180);after=gpu_state()
            groups={}
            for r in csv.DictReader(prefix.with_suffix(".passes.csv").open()):
                if r["kind"]=="gpu":groups.setdefault(r["label"],[]).append(float(r["duration_ms"]))
            if len(groups.get("sdf.whole_scene",[]))!=frames-warmup:raise RuntimeError("GPU timestamp tail incomplete")
            metrics=list(csv.DictReader(prefix.with_suffix(".metrics.csv").open()))
            selection={r["name"]:float(r["value"]) for r in metrics if r["category"]=="sdf.shader_selection"}
            row=dict(view=view,name=name,width=width,height=height,repeat=repeat,command=command,
                shader=shader,shader_sha256=digest(args.binary.parent/"shaders"/shader.split("@")[0]),
                gpu_before=before,gpu_after=after,wall_seconds=wall,selection=selection,
                passes={k:dict(samples=len(v),median_ms=statistics.median(v),p95_ms=percentile(v,.95)) for k,v in groups.items()})
            if name in ("selected","no-shadows","no-procedural","no-bump") and selection["route"]!=2:raise RuntimeError("automatic shortcut not active")
            receipt["runs"].append(row);save(args.output/"timing.json",receipt)
            print(prefix.name,round(row["passes"]["sdf.whole_scene"]["p95_ms"],4),flush=True)
    receipt["complete"]=True;save(args.output/"timing.json",receipt)


def compiler(args):
    receipt=provenance(args)
    shaders=sorted({row[2] for row in CASES}|{"lit_root_far_sdf_analytic_pruned.frag.spv","profile_opt-cull-dark-shadows.frag.spv","profile_opt-reorder-shadow.frag.spv"})
    command=[str(args.binary.parent/"cubey_project_sdf_shader_stats"),str(args.binary.parent/"shaders"),str(args.output),*shaders]
    invoke(command,args.output/"compiler",timeout=300)
    receipt.update(command=command,complete=True);save(args.output/"context.json",receipt)
    for row in json.loads((args.output/"statistics.json").read_text())["pipelines"]:
        fragment=next(e for e in row["executables"] if e["stages"]==16)
        print(row["request"],{s["name"]:s["value"] for s in fragment["statistics"] if s["name"] in ("Register Count","Binary Size")},flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("phase",choices=["qualify","cull-qualify","cull","pilot","timing","scale","compiler"])
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--qualification",type=Path)
    p.add_argument("--binary",type=Path,default=ROOT/"build/release/projects/sdf/sdf")
    args=p.parse_args();args.output=args.output.resolve();args.binary=args.binary.resolve()
    args.output.mkdir(parents=True,exist_ok=False)
    (qualify if args.phase in ("qualify","cull-qualify") else compiler if args.phase=="compiler" else timing)(args)


if __name__=="__main__":main()
