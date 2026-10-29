#!/usr/bin/env python3
"""Assemble selected-path investigation receipts without promoting the probes."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
from acceleration_loop import save
from investigate_guardian import ROOT,digest


def ranges(data):
    keys=sorted({(r["view"],r["name"],r["width"],r["height"]) for r in data["runs"]})
    result=[]
    for view,name,width,height in keys:
        runs=[r for r in data["runs"] if (r["view"],r["name"],r["width"],r["height"])==(view,name,width,height)]
        values=[r["passes"]["sdf.whole_scene"]["p95_ms"] for r in runs]
        if len(runs)!=3 or any(r["passes"]["sdf.whole_scene"]["samples"]!=150 for r in runs):
            raise RuntimeError("missing repeat or GPU tail")
        result.append(dict(view=view,name=name,width=width,height=height,min_ms=min(values),max_ms=max(values),p95_ms=values))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args();raw=args.raw.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    phases={"ablations":"timing","resolution":"scale","shadow-pilot":"cull-timing","shadow-controlled":"cull-controlled-timing"}
    all_runs=[];summary=dict(scope="Investigation only; private static probes, no production optimization or default adoption",groups={})
    for label,directory in phases.items():
        receipt=json.loads((raw/directory/"timing.json").read_text())
        if not receipt.get("complete"):raise RuntimeError("unfinished timing "+directory)
        for run in receipt["runs"]:
            if digest(ROOT/"build/release/projects/sdf/shaders"/run["shader"].split("@")[0])!=run["shader_sha256"]:
                raise RuntimeError("measured shader changed")
        all_runs.extend(receipt["runs"])
        summary["groups"][label]=ranges(receipt)
        shutil.copyfile(raw/directory/"timing.json",out/(label+".json"))
    baseline=json.loads((raw/"baseline.json").read_text())
    changed=[name for name,sha in baseline["files"].items() if digest(ROOT/name)!=sha]
    if changed:raise RuntimeError("protected files changed")
    for target,source in [("baseline.json","baseline.json"),("qualification.json","qualification-final/qualification.json"),
                          ("shadow-qualification.json","cull-controls-qualification/qualification.json"),
                          ("compiler.json","compiler-final/statistics.json"),("sources.json","sources.json"),
                          ("opportunities.json","opportunities.json"),("workload.json","workload-analysis.json"),
                          ("author-source.json","references/auto-relaxed-source.json")]:
        shutil.copyfile(raw/source,out/target)
    for name in ["qualification.json","shadow-qualification.json"]:
        q=json.loads((out/name).read_text())
        if not q.get("complete"):raise RuntimeError("unfinished capture qualification")
    old=json.loads((ROOT/"docs/evidence/sdf-acceleration-loop-2026-10/context.json").read_text())
    kernel="root_far_workload_analytic.comp.spv"
    kernel_unchanged=digest(ROOT/"build/release/projects/sdf/shaders"/kernel)==old["shader_sha256"][kernel]
    if not kernel_unchanged:raise RuntimeError("historical counter kernel differs")
    summary.update(timing_runs=len(all_runs),completed_gpu_samples=sum(r["passes"]["sdf.whole_scene"]["samples"] for r in all_runs),
        protected_files=len(baseline["files"]),protected_changes=changed,historical_workload_kernel_unchanged=kernel_unchanged,
        encoder_boundary_count=len(all_runs)*2,
        encoder_nonzero_boundaries=sum(int(re.search(r"[0-9]+",r[k].split(",")[1]).group())!=0 for r in all_runs for k in ["gpu_before","gpu_after"]),
        candidate_head=subprocess.check_output(["git","rev-parse","HEAD"],text=True).strip())
    stats=json.loads((out/"compiler.json").read_text())
    summary["compiler"]=[]
    for row in stats["pipelines"]:
        frag=next(e for e in row["executables"] if e["stages"]==16)
        useful={s["name"]:s["value"] for s in frag["statistics"] if s["name"] in ["Register Count","Binary Size"]}
        summary["compiler"].append(dict(request=row["request"],**useful))
    summary["local_memory_statistic_usable"]=stats.get("local_memory_statistic_usable")
    (out/"images").mkdir(exist_ok=True);images=[]
    for view in ["preset","side","rear"]:
        for name in ["selected","cull-dark-shadows","reorder-shadow"]:
            source=raw/"cull-controls-qualification"/(view+"-"+name+".png");target=out/"images"/source.name
            shutil.copyfile(source,target);images.append(dict(view=view,name=name,sha256=digest(target),path="images/"+source.name))
        if len({r["sha256"] for r in images if r["view"]==view})!=1:raise RuntimeError("shadow probe pixels differ")
    save(out/"images.json",images)
    save(out/"context.json",dict(baseline=baseline["head"],investigation_head=summary["candidate_head"],
        runtime_binary_hashes={label:json.loads((raw/directory/"timing.json").read_text())["binary_sha256"] for label,directory in phases.items()},
        measured_shader_note="Private admission/shader variants were added between phases. Every retained timing shader hash still matches; final shadow comparisons remeasure all controls on one binary.",
        workload_note="Existing 320p counters, not current 1080p hardware times. Raw payload hashes are in workload.json; kernel and protected content hashes match.",
        workload_kernel=dict(name=kernel,sha256=old["shader_sha256"][kernel],unchanged=kernel_unchanged),
        raw_root=str(raw),no_production_adoption=True))
    save(out/"summary.json",summary)
    print("assembled",summary["timing_runs"],"runs,",summary["completed_gpu_samples"],"samples; protected",len(baseline["files"]))
    print("compiler",summary["compiler"])


if __name__=="__main__":main()
