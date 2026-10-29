#!/usr/bin/env python3
"""Retain compact shader-selection evidence from completed native receipts."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
from investigate_guardian import ROOT, digest
from acceleration_loop import save


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args();raw=args.raw.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    qualification=json.loads((raw/"qualification-final/qualification.json").read_text())
    appearance=json.loads((raw/"appearance-v4/qualification.json").read_text())
    timing=json.loads((raw/"timing/timing.json").read_text())
    window=json.loads((raw/"window-v3/windowed-interaction.json").read_text())
    binary=ROOT/"build/release/projects/sdf/sdf"
    for receipt in (qualification,appearance,timing):
        if not receipt.get("complete") or receipt["binary_sha256"]!=digest(binary):
            raise RuntimeError("incomplete or different-binary evidence")
    if not window.get("passed") or window["binary_sha256"]!=digest(binary):raise RuntimeError("window evidence mismatch")
    baseline=json.loads((raw/"baseline.json").read_text())
    changed=[name for name,sha in baseline["files"].items() if digest(ROOT/name)!=sha]
    if changed:raise RuntimeError("protected baseline changed: "+str(changed))
    for name,source in (("qualification.json",raw/"qualification-final/qualification.json"),
                        ("appearance.json",raw/"appearance-v4/qualification.json"),
                        ("timing.json",raw/"timing/timing.json"),("window.json",raw/"window-v3/windowed-interaction.json"),
                        ("baseline.json",raw/"baseline.json"),("negative.json",raw/"negative.json")):
        shutil.copyfile(source,out/name)
    (out/"actions").mkdir(exist_ok=True)
    pairs=[]
    for directory,suffix in (("qualification-final","replay"),("appearance-v4","receipt")):
        for mode in ("analytic","hybrid"):
            a=json.loads((raw/directory/f"{mode}-general-{suffix}.json").read_text())["actions"]
            b=json.loads((raw/directory/f"{mode}-automatic-{suffix}.json").read_text())["actions"]
            if len(a)!=len(b):raise RuntimeError("action count differs")
            for general,automatic in zip(a,b):
                if digest(Path(general["frame_png"]))!=digest(Path(automatic["frame_png"])):
                    raise RuntimeError("action pixels differ")
                pairs.append(dict(family=directory,mode=mode,frame=general["frame"],label=general["label"],
                    image_sha256=digest(Path(automatic["frame_png"])),shader=automatic["shader_route"],
                    pipeline_count=automatic["shader_pipeline_count"],field_builds=automatic["field_builds"],
                    region_builds=automatic["region_builds"],failed=automatic["failed"]))
            for control in ("general","automatic"):
                source=raw/directory/f"{mode}-{control}-{suffix}.json"
                shutil.copyfile(source,out/"actions"/(directory+"-"+source.name))
    save(out/"action-pixels.json",pairs)
    images=[("guardian-preset-general.png","qualification-final/guardian-preset-general.png"),
            ("guardian-preset-automatic.png","qualification-final/guardian-preset-automatic.png"),
            ("guardian-rear-general.png","qualification-final/guardian-rear-general.png"),
            ("guardian-rear-automatic.png","qualification-final/guardian-rear-automatic.png"),
            ("robot-preset-general.png","qualification-final/robot-preset-general.png"),
            ("robot-preset-automatic.png","qualification-final/robot-preset-automatic.png"),
            ("glass-general.png","qualification-final/material-study-sphere-glass-debug0-general.png"),
            ("glass-automatic.png","qualification-final/material-study-sphere-glass-debug0-automatic.png"),
            ("glass-transport.png","qualification-final/material-study-sphere-glass-debug9-automatic.png"),
            ("pattern-before.png","appearance-v4/analytic-automatic-receipt.frame-0.png"),
            ("pattern-after.png","appearance-v4/analytic-automatic-receipt.frame-3.png"),
            ("window-material-edited.png","window-v3/captures/window-material-edited.png")]
    (out/"images").mkdir(exist_ok=True)
    for name,source in images:shutil.copyfile(raw/source,out/"images"/name)
    save(out/"images.json",[dict(name=name,raw=source,sha256=digest(out/"images"/name)) for name,source in images])
    groups=[]
    for scene in ("guardian","robot"):
        for view in ("preset","side","rear"):
            row=dict(scene=scene,view=view)
            for control in ("general","automatic"):
                runs=[r for r in timing["runs"] if r["scene"]==scene and r["view"]==view and r["control"]==control]
                if len(runs)!=3 or any(r["passes"]["sdf.whole_scene"]["samples"]!=150 for r in runs):raise RuntimeError("timing samples incomplete")
                values=[r["passes"]["sdf.whole_scene"]["p95_ms"] for r in runs]
                row[control]=dict(p95_min_ms=min(values),p95_max_ms=max(values),per_repeat_p95_ms=values,
                    setup_ms=[r["selection"]["pipeline_setup_ms"] for r in runs])
            row["speedup_range"]=[row["general"]["p95_min_ms"]/row["automatic"]["p95_max_ms"],
                                  row["general"]["p95_max_ms"]/row["automatic"]["p95_min_ms"]]
            groups.append(row)
    gpu_boundaries=[r[which] for r in timing["runs"] for which in ("gpu_before","gpu_after")]
    encoder=[int(re.match(r"[0-9]+",s.split(",")[1].strip()).group()) for s in gpu_boundaries]
    summary=dict(candidate_commit=subprocess.check_output(["git","rev-parse","HEAD"],text=True).strip(),
        binary_sha256=digest(binary),device=timing["device"],timing=groups,
        timing_runs=len(timing["runs"]),completed_gpu_samples=sum(r["passes"]["sdf.whole_scene"]["samples"] for r in timing["runs"]),
        native_jobs=len(qualification["runs"])+len(appearance["runs"]),
        static_image_pairs=sum("different_pixels" in r for d in (qualification,appearance) for r in d["runs"]),
        action_image_pairs=len(pairs),frozen_fixture_routes=len(qualification["fixtures"]),
        frozen_checks_per_route=155,protected_files=len(baseline["files"]),protected_changes=changed,
        encoder_boundary_max_pct=max(encoder),encoder_nonzero_boundaries=sum(n>0 for n in encoder),
        encoder_boundary_count=len(encoder),window_passed=True,default="sampled/full, one original trace pipeline",
        source_pool="three prebuilt pipelines; published registered-material eligibility; original debug/general fallback",
        controlled_cold_cache_benchmark=False,
        first_window_setup_ms=float(re.search(r"shader pipelines=3 setup_ms=([0-9.]+)",
            (raw/"window-v2/logs/windowed-interaction.log").read_text()).group(1)),
        caveats=["GPU/driver-specific fixed scenes; physical desktop and human visual acceptance remain separate",
                 "Registered material count is conservative; unused registered materials disable single-material specialization",
                 "Setup timings are observed driver-cache states, not controlled cold-cache timings",
                 "Shader/driver private allocation size, occupancy, cache stalls and spills are unmeasured",
                 "Prior root-far versus A64 boundary differences and cache/edit latency limits remain",
                 "Captured action completion includes optional readback; it is not an edit-latency benchmark"])
    save(out/"summary.json",summary)
    save(out/"context.json",dict(raw=str(raw),candidate_commit=summary["candidate_commit"],binary_sha256=digest(binary),
        shaders={p.name:digest(p) for p in sorted((binary.parent/"shaders").glob("*.spv"))},
        note="Static qualification began before the test commits completed. Binary hashes match every accepted receipt. Resume verified stored static captures and reran all action/oracle jobs; scene and original SPIR-V hashes remained unchanged."))
    rejected=[("qualification/analytic-full-atmosphere-multi-general.log","Generated test scene omitted required names; corrected before qualification"),
              ("qualification-v2/analytic-control-atmosphere-general.log","Acceleration test used axis 16; retained axis-64 admission contract"),
              ("appearance/analytic-4-automatic.log","PNG prefix assumption rejected: only first frame rendered; replaced with per-frame readback"),
              ("appearance-v3/analytic-general.log","Late final-frame copy used an expired host slot; copy is now queued during record before host slot teardown"),
              ("window/logs/windowed-interaction.log","Window harness config used CLI spelling instead of profile.output"),
              ("window-v2/logs/windowed-interaction.log","Material control was below the clipped panel; one inspected scroll exposes the actual roughness control")]
    (out/"rejected").mkdir(exist_ok=True)
    for index,(source,reason) in enumerate(rejected):
        target=out/"rejected"/(str(index)+".log");shutil.copyfile(raw/source,target)
    save(out/"rejected.json",[dict(raw=source,reason=reason,log=f"rejected/{i}.log",sha256=digest(out/"rejected"/(str(i)+".log")))
         for i,(source,reason) in enumerate(rejected)])
    print(json.dumps(summary,indent=2))


if __name__=="__main__":main()
