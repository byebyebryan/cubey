#!/usr/bin/env python3
"""Archive reproducible source-query gates, raw GPU samples and protected hashes."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import subprocess
from collections import Counter, defaultdict
from benchmark_tracing import percentile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2)+"\n")


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--raw",type=Path,default=Path("outputs/sdf/source-query-optimization-20261029"))
    p.add_argument("--output",type=Path,default=Path("docs/evidence/sdf-source-query-optimization-2026-10"))
    args=p.parse_args()
    raw=args.raw.resolve()
    out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=True)
    binary=Path("build/release/projects/sdf/sdf")
    binary_hash=digest(binary)
    receipts={
        "baseline.json":"baseline.json",
        "b-numerical-min-height.json":"primitive-numerics.json",
        "shadow-contract.json":"shadow-exhaustion.json",
        "focused-checks.json":"focused-checks.json",
        "final-a-qualification/qualification.json":"automatic-qualification.json",
        "final-b-qualification/qualification.json":"prepared-qualification.json",
        "final-combined-qualification/qualification.json":"combined-qualification.json",
        "final-b-qualification/prepared-fixtures.json":"prepared-frozen-results.json",
        "final-combined-qualification/prepared-fixtures.json":"combined-frozen-results.json",
        "supplemental-contracts/qualification.json":"supplemental-qualification.json",
        "supplemental-contracts/supplemental-contracts.json":"supplemental-contracts.json",
        "authored-history/qualification.json":"authored-history.json",
        "full-publication/qualification.json":"guardian-publication.json",
        "cone-lighting/qualification.json":"cone-lighting.json",
        "final-window/windowed-interaction.json":"native-window.json",
        "a-appearance/qualification.json":"earlier-shadow-appearance.json",
        "a-edges-v4/qualification.json":"earlier-shadow-edge-cases.json",
    }
    for source,target in receipts.items():
        value=json.loads((raw/source).read_text())
        if target in ("automatic-qualification.json","prepared-qualification.json","combined-qualification.json"):
            assert value["complete"] and value["binary_sha256"]==binary_hash,target
        shutil.copy2(raw/source,out/target)
    (out/"focused-checks.log").write_text((raw/"focused-checks.log").read_text().rstrip()+"\n")
    checks=json.loads((out/"focused-checks.json").read_text())
    checks["raw_log_sha256"]=checks["log_sha256"]
    checks["log_sha256"]=digest(out/"focused-checks.log")
    checks["archive_normalization"]="Terminal blank lines removed; original raw log retained unchanged."
    save(out/"focused-checks.json",checks)
    frozen=json.loads(Path("projects/sdf/comparison/fixtures.json").read_text())
    numeric=json.loads((out/"primitive-numerics.json").read_text())
    for group in numeric["groups"].values():
        assert group["max_prepared_cpu_error"]<=frozen["contracts"]["point_tolerance"]
        assert group["max_normal_degrees"]<=frozen["contracts"]["normal_degrees"] and group["nonfinite"]==0
    original=json.loads((raw/"baseline.json").read_text())
    preservation={kind:dict(checked=len(original[kind]),changed=[name for name,h in original[kind].items()
                         if digest(Path(name))!=h]) for kind in ("shaders","content")}
    assert all(not row["changed"] for row in preservation.values())
    protected_cpu=["projects/sdf/"+name for name in ("sdf_authored.cpp","sdf_authored.h","sdf_history.cpp",
        "sdf_regions.cpp","sdf_regions.h","sdf_bounds.cpp","sdf_field.cpp","sdf_field.h")]
    cpu_changes=subprocess.check_output(["git","diff","0737bec9","--",*protected_cpu,"src/cubey","include/cubey"],text=True)
    assert not cpu_changes,"canonical CPU/history/field or shared engine source changed"
    preservation["canonical_cpu_and_engine"]=dict(changed=False,paths=protected_cpu+["src/cubey","include/cubey"])
    # Check the raw final automatic metrics too: no derived buffer is allocated
    # on the default, general, sampled, optical or shadow-only routes.
    auto=json.loads((out/"automatic-qualification.json").read_text())
    for run in auto["runs"]:
        for row in csv.DictReader((raw/"final-a-qualification"/(run["name"]+".metrics.csv")).open()):
            if row["category"]=="sdf.prepared_cones" and row["name"] in ("payload_bytes","allocation_bytes","builds"):
                assert float(row["value"])==0
    summary={}
    all_samples=[]
    load={}
    for folder,label in (("a-timing","shadow-only-timing"),("b-timing","prepared-only-timing"),("final-timing","four-way-timing")):
        receipt=json.loads((raw/folder/"timing.json").read_text())
        assert receipt["complete"]
        if folder=="final-timing":assert receipt["binary_sha256"]==binary_hash
        shutil.copy2(raw/folder/"timing.json",out/(label+".json"))
        encoder=defaultdict(list)
        for row in receipt["runs"]:
            name=f"{row['scene']}-{row['view']}-{row['mode']}-r{row['repeat']}"
            log=(raw/folder/(name+".log")).read_text()
            assert "headless_render_only_ready: NVIDIA GeForce RTX 5070 Ti at 1920x1080" in log,name
            for boundary in ("gpu_before","gpu_after"):
                encoder[row["mode"]].append(float(row[boundary].split(",")[1].split()[0]))
            samples=[(int(r["frame_index"]),float(r["duration_ms"])) for r in
                     csv.DictReader((raw/folder/(name+".passes.csv")).open())
                     if r["kind"]=="gpu" and r["label"]=="sdf.whole_scene"]
            assert sorted(i for i,_ in samples)==list(range(30,180)),name
            assert abs(percentile([v for _,v in samples],.95)-row["passes"]["sdf.whole_scene"]["p95_ms"])<1e-8
            all_samples.append(dict(batch=folder,name=name,frames=samples))
        load[folder]={mode:dict(observations=len(values),min_pct=min(values),max_pct=max(values),
                    mean_pct=statistics.mean(values),distribution=dict(Counter(values)))
                    for mode,values in encoder.items()}
        if folder!="final-timing":continue
        for scene in ("guardian","robot"):
            summary[scene]={}
            for view in ("preset","side","rear"):
                summary[scene][view]={}
                for mode in receipt["modes"]:
                    rows=[r for r in receipt["runs"] if r["scene"]==scene and r["view"]==view and r["mode"]==mode]
                    values=[r["passes"]["sdf.whole_scene"]["p95_ms"] for r in rows]
                    summary[scene][view][mode]=dict(p95_ms=values,min_ms=min(values),max_ms=max(values),
                        median_p95_ms=statistics.median(values),pipeline_setup_ms=[r["selection"]["pipeline_setup_ms"] for r in rows],
                        preparation=[r.get("prepared_resources",{}) for r in rows])
    save(out/"gpu-samples.json",all_samples)
    save(out/"encoder-observations.json",dict(fields="gpu_pct,encoder_pct,sm_MHz,mem_MHz,temp_C,power_W,used_MiB",batches=load,
        interpretation="Independent A/B batches observed encoder 0%; final four-way batch had shared encoder activity. No idle combined-performance claim."))
    gates={}
    final=json.loads((out/"four-way-timing.json").read_text())
    for mode in ("shadow","prepared-cones","combined"):
        gains=[]
        regressions=[]
        for scene in ("guardian","robot"):
            for view in ("preset","side","rear"):
                a=summary[scene][view]["reference"]["p95_ms"]
                b=summary[scene][view][mode]["p95_ms"]
                if scene=="guardian":
                    variation=max(a)-min(a)
                    gains.append(all(x-y>=.5 and (x-y)/x>=.05 and x-y>variation for x,y in zip(a,b)))
                regressions.extend(dict(scene=scene,view=view,repeat=i+1,delta_ms=y-x)
                    for i,(x,y) in enumerate(zip(a,b)) if y-x>.5 and (y-x)/x>.03)
        gates[mode]=dict(repeatable_guardian_gain=any(gains),regressions=regressions,
                        passed=any(gains) and not regressions)
    assert all(row["passed"] for row in gates.values()),gates
    images=out/"images"
    images.mkdir(exist_ok=True)
    for view in ("preset","side","rear"):
        for role,name in (("reference","reference"),("combined","prepared")):
            shutil.copy2(raw/"final-combined-qualification"/f"guardian-{view}-{name}.png",images/f"guardian-{view}-{role}.png")
    shutil.copy2(raw/"final-window/captures/window-source-lit.png",images/"native-source-window.png")
    context=dict(source_head=subprocess.check_output(["git","rev-parse","HEAD"],text=True).strip(),
        candidate_source_head=final["source_head"],binary_sha256=binary_hash,device=final["device"],
        width=1920,height=1080,supersample=1,frames=180,warmup=30,samples_per_run=150,
        timing_runs=len(all_samples),gpu_samples=sum(len(r["frames"]) for r in all_samples),
        preservation=preservation,automatic_resource_boundary_passed=True,
        all_final_guardian_views_within_16_7_ms=all(r["max_ms"]<=16.7 for r in
            [summary["guardian"][v]["combined"] for v in ("preset","side","rear")]),
        gates=gates,summary=summary)
    context["derived_format_version"]=1
    context["encoder_observations"]=load
    context["combined_idle_gpu_qualified"]=False
    context["assembler_sha256"]=digest(Path(__file__))
    save(out/"summary.json",context)
    # All durable receipts and images are content-addressed; raw host output is
    # retained separately for rejected attempts and full profile CSVs.
    save(out/"manifest.json",{str(path.relative_to(out)):dict(bytes=path.stat().st_size,sha256=digest(path))
         for path in sorted(out.rglob("*")) if path.is_file() and path.name!="manifest.json"})
    print(json.dumps(dict(output=str(out),timing_runs=context["timing_runs"],gpu_samples=context["gpu_samples"],
                          gates=gates,protected=preservation),indent=2))


if __name__=="__main__":main()
