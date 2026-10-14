#!/usr/bin/env python3
"""Frozen patch-only Fluid replay controls; no solver, generator or GUI."""
import argparse
import csv
import json
from pathlib import Path
import sys

import run_terrain_rain_demo as demo

ROOT = demo.demo.ref.ROOT
sys.path.insert(0, str(ROOT / "projects/terrain"))
import review_captured_water_v1 as render

APP = demo.RELEASE_APP
OLD = ROOT / "outputs/fluid/offscreen-perf-20261010-CwWtK3"
ORDINARY = ROOT / "outputs/fluid/site-expansion-20261011-jtIA3s"
WORLD = ROOT / "outputs/fluid/world-water-20261012-Ck824g"
CASES = {
    "ordinary1k": (ORDINARY / "cases/mountain-patch-1024-16m",
                   OLD / "soak-600s-offscreen/live-0/static-recording/recording.json"),
    "ordinary2k": (ORDINARY / "cases/mountain-patch-2048-8m",
                   OLD / "corrected-2048-8m-offscreen-live/live-0/static-recording/recording.json"),
    "world1k": (WORLD / "cases/mountain-patch-1024-16m",
                WORLD / "runs/mountain-patch-1024-16m/recording/recording.json"),
    "valley1k": (WORLD / "cases/valley-interior-patch-1024-16m",
                 WORLD / "runs/valley-interior-patch-1024-16m/recording/recording.json"),
}
VIEWS = [("ordinary1k", v) for v in ("overview", "runoff", "collection")]
VIEWS += [("ordinary2k", "runoff"), ("world1k", "overview"),
          ("world1k", "runoff"), ("world1k", "collection"), ("valley1k", "overview")]


def write(path, data):
    with path.open("x") as file:
        json.dump(data, file, indent=2, allow_nan=False)
        file.write("\n")


def runtime():
    return {"app": render.digest(APP), "shaders": {p.name: render.digest(p)
        for p in sorted((APP.parent / "shaders").glob("*.spv"))}}


def inputs():
    paths = set()
    for case, recording in CASES.values():
        demo.case_identity(case)
        paths.update(p for p in case.rglob("*") if p.is_file())
        paths.update(p for p in recording.parent.iterdir() if p.is_file())
        binding = json.loads((case / "surface-grid.json").read_bytes())
        source = Path(binding["manifest_path"])
        paths.update(p for p in source.parent.iterdir() if p.is_file())
    return {str(p): render.digest(p) for p in sorted(paths)}


def verify(out, phase):
    frozen = json.loads((out / (phase + "-protocol.json")).read_bytes())
    if frozen["runtime"] != runtime() or frozen["inputs"] != inputs():
        raise ValueError("frozen runtime or inputs changed; retain earlier evidence")
    if frozen["harness"] != render.digest(Path(__file__)):
        raise ValueError("capture harness changed")
    return frozen


def command(case_id, camera, baseline=False):
    case, recording = CASES[case_id]
    meta = json.loads(recording.read_bytes())
    args = demo.viewer_command(case, recording, "Fluid patch-only review", app=APP,
        live=False, start=meta["frames"][-1]["time_s"], width=1280, height=720, camera=camera)
    args += ["--headless", "--fluid25d-recording-gpu-validation"]
    if baseline and case_id.startswith(("world", "valley")):
        args += ["--fluid25d-native-terrain-context", "off"]
    return args


def checked(args, log):
    receipt = render.run(args, log)
    raw = log.read_text()
    if "NVIDIA GeForce RTX 5070 Ti" not in raw or "VUID-" in raw:
        raise ValueError("expected hardware Vulkan without validation errors")
    return receipt


def capture(out, phase):
    verify(out, phase)
    rows = []
    for case, camera in VIEWS:
        name = phase + "-" + case + "-" + camera
        image = out / (name + ".png")
        receipt = checked(command(case, camera, phase == "baseline") + ["--output", str(image)],
                          out / (name + ".log"))
        raw = (out / (name + ".log")).read_text()
        if "fluid_25d_recording_upload: PASS" not in raw or "hydraulic_dispatches=0" not in raw:
            raise ValueError("missing exact recorded-field audit")
        row = {"case": case,"camera": camera,"image": image.name, **receipt}
        if phase == "final":
            if "context_vertices\":0" not in raw:
                raise ValueError("patch-only preparation receipt missing")
            row["difference"] = render.pixel_difference(out / ("baseline-"+case+"-"+camera+".png"),image)
        rows.append(row)
        print("captured",name,flush=True)
    write(out / (phase + "-captures.json"), {"rows":rows})
    verify(out, phase)


def performance(out):
    verify(out,"final")
    rows=[]
    for repeat in range(2):
        for case in (("ordinary1k","world1k","ordinary2k") if not repeat else
                     ("ordinary2k","world1k","ordinary1k")):
            prefix=out/f"perf-{case}-{repeat}"
            args=command(case,"runoff")
            # GPU field readback is not part of the continuous timing path.
            args.remove("--fluid25d-recording-gpu-validation")
            receipt=checked(args+["--headless-render-only","--run-seconds","12","--render-fps","60",
                "--width","1920","--height","1080","--no-validation","--profile-output",str(prefix),
                "--profile-warmup-frames","120","--profile-sample-interval","1"],prefix.with_suffix(".log"))
            with prefix.with_suffix(".passes.csv").open() as stream: passes=list(csv.DictReader(stream))
            gpu=[float(r["duration_ms"]) for r in passes if r["kind"]=="gpu" and
                 r["label"]=="fluid_25d native presentation total"]
            cpu=[float(r["duration_ms"]) for r in passes if r["kind"]=="cpu" and r["label"]=="headless.frame_service"]
            if len(gpu)<100 or len(cpu)<100: raise ValueError("incomplete timing window")
            rows.append({"case":case,"repeat":repeat,**receipt,
                "gpu_p95_ms":render.percentile(gpu),"cpu_p95_ms":render.percentile(cpu)})
            print("timed",case,rows[-1]["gpu_p95_ms"],flush=True)
    write(out/"performance.json",{"rows":rows,"scope":"1080p renderer-only, 120 warmup frames, no solver/readback/encode"})
    verify(out,"final")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out",type=Path)
    parser.add_argument("--phase",choices=("freeze-baseline","baseline","freeze-final","final","performance"),required=True)
    args=parser.parse_args();out=args.out.resolve()
    if args.phase.startswith("freeze-"):
        phase=args.phase.removeprefix("freeze-")
        write(out/(phase+"-protocol.json"),{"runtime":runtime(),"inputs":inputs(),
            "harness":render.digest(Path(__file__)),"views":VIEWS,"scope":"hardware headless Fluid viewer; no solver or generator"})
    elif args.phase=="performance": performance(out)
    else: capture(out,args.phase)


if __name__=="__main__": main()
