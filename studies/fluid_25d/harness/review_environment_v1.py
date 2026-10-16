#!/usr/bin/env python3
"""Frozen shared-environment review. Hardware offscreen Vulkan, no solver/GUI."""
import argparse
import csv
import json
from pathlib import Path
import re
import subprocess

import review_patch_only_v1 as patch

render = patch.render
ROOT = patch.ROOT
TIMES = {"daylight": None, "noon": 12, "sunset": 18, "night": 0}
TREATMENTS = {
    "reference": [],
    "clear": [],
    "background": ["--clouds", "--no-fluid25d-cloud-reflections", "--fluid25d-cloud-shadow-strength", "0"],
    "reflection": ["--clouds", "--no-fluid25d-cloud-background", "--fluid25d-cloud-shadow-strength", "0"],
    "shadow": ["--clouds", "--no-fluid25d-cloud-background", "--no-fluid25d-cloud-reflections", "--fluid25d-cloud-shadow-strength", ".35"],
    "weather": ["--clouds", "--fluid25d-cloud-shadow-strength", ".18"],
}


def runtime():
    return {"fluid": patch.runtime(), "terrain": render.runtime()}


def inputs():
    return {**patch.inputs(), **{str(ROOT / p): h for p,h in render.inputs().items()
                                if p != "source_elevation_sha256"}}


def freeze(out):
    patch.write(out / "environment-protocol.json", {"runtime": runtime(), "inputs": inputs(),
        "harness": render.digest(Path(__file__)), "times": TIMES, "treatments": TREATMENTS,
        "scope": "frozen native fields; shared environment only; no solver or terrain generation"})


def verify(out):
    frozen=json.loads((out/"environment-protocol.json").read_bytes())
    if frozen["runtime"]!=runtime() or frozen["inputs"]!=inputs() or frozen["harness"]!=render.digest(Path(__file__)):
        raise ValueError("frozen environment runtime/input/harness drift")
    return frozen


def command(camera, time="daylight", treatment="clear", case="world1k", quality="full"):
    args=patch.command(case,camera)
    if treatment=="reference": return args
    args += ["--fluid25d-environment","shared"]
    if TIMES[time] is not None:
        args += ["--time-of-day-mode","solar","--time-hours",str(TIMES[time]),
                 "--day-of-year","80","--latitude-degrees","45","--pause-time"]
    args += TREATMENTS[treatment]
    if "--clouds" in args:
        args += ["--cloud-weather-preset","broken-cumulus","--cloud-quality",quality]
    return args


def checked(args, log, native=True):
    receipt=patch.checked(args,log)
    raw=log.read_text()
    audit="fluid_25d_recording_upload: PASS" if native else "terrain_water_capture_immutable: GPU bed/depth/decoded-velocity exact"
    if audit not in raw: raise ValueError("exact imported-field audit missing")
    return receipt


def frames(out):
    verify(out)
    rows=[]
    matrix=[(camera,time,treatment) for camera in ("runoff","collection")
            for time in TIMES for treatment in ("clear","weather")]
    matrix += [("collection","daylight",t) for t in ("background","reflection","shadow","reference")]
    for camera,time,treatment in matrix:
        name=f"{camera}-{time}-{treatment}"
        receipt=checked(command(camera,time,treatment)+["--output",str(out/(name+".png"))],out/(name+".log"))
        rows.append({"camera":camera,"time":time,"treatment":treatment,"image":name+".png",**receipt})
        print("captured",name,flush=True)
    # Fixed reference must not follow the opt-in weather path.
    control=out/"reference-ordinary-runoff.png"
    receipt=checked(command("runoff",treatment="reference",case="ordinary1k")+["--output",str(control)],out/"reference-ordinary-runoff.log")
    diff=render.pixel_difference(out/"baseline-ordinary1k-runoff.png",control)
    if diff["max_abs_8bit"]: raise ValueError("fixed startup rendering changed")
    low=[]
    for treatment in ("clear","weather"):
        name="low-oblique-"+treatment
        receipt=checked(command("collection","daylight",treatment)+[
            "--fluid25d-native-camera-pitch","-.40","--fluid25d-native-camera-span-m","4500",
            "--fluid25d-native-camera-target-x",str(474/511),
            "--fluid25d-native-camera-target-z",str(105/511),
            "--output",str(out/(name+".png"))],out/(name+".log"))
        low.append({"treatment":treatment,"image":name+".png",**receipt})
    patch.write(out/"environment-frames.json",{"rows":rows,"low_oblique":low,"fixed_reference_difference":diff})
    verify(out)


def terrain(out):
    verify(out)
    rows=[]
    for name,hour,clouds in (("reference",None,False),("day",12,False),("night",0,False),("weather",12,True)):
        args=render.command("lake",reference=False)+["--width","1280","--height","720","--frames","1","--terrain-water-audit"]
        if name!="reference":
            args += ["--terrain-water-environment","shared","--time-of-day-mode","solar",
                     "--time-hours",str(hour),"--day-of-year","80","--latitude-degrees","45","--pause-time"]
        if clouds: args += ["--clouds","--cloud-weather-preset","broken-cumulus"]
        receipt=checked(args+["--output",str(out/("terrain-"+name+".png"))],out/("terrain-"+name+".log"),False)
        rows.append({"name":name,"image":"terrain-"+name+".png",**receipt})
        print("captured terrain",name,flush=True)
    patch.write(out/"environment-terrain.json",{"rows":rows})
    verify(out)


def motion(out):
    verify(out)
    rows=[]
    for name, frames, fps, cycle in (("weather-motion",144,24,0),("day-night-cycle",960,30,.75)):
        args=command("collection","daylight","weather",quality="half")
        if cycle:
            args += ["--time-of-day-mode","solar","--time-hours","12","--day-of-year","80",
                     "--latitude-degrees","45","--time-speed-hours-per-second",str(cycle),
                     "--set","atmosphere.time_paused=false"]
        else:
            meta=json.loads(patch.CASES["world1k"][1].read_bytes())
            pairs=[(a,b) for a,b in zip(meta["frames"],meta["frames"][1:]) if b["time_s"]-a["time_s"]>frames]
            args[args.index("--fluid25d-recording-time-seconds")+1]=str(pairs[-1][0]["time_s"])
        receipt=checked(args+["--capture","video","--frames",str(frames),"--fps",str(fps),
            "--fluid25d-recording-frame-interval-seconds","1",
            "--output",str(out/(name+".mp4"))],out/(name+".log"))
        raw=(out/(name+".log")).read_text()
        saved=re.findall(r"fluid_25d_recording_capture:.*saved_s=(\S+)",raw)
        if len(saved)!=frames or len(set(saved))!=1: raise ValueError("motion changed native fields")
        clocks=re.findall(r"terrain_surface_environment: wall_s=(\S+) time_hours=(\S+).*sky_generation=(\d+) cloud_generation=(\d+)",raw)
        # Generation logs run at the existing 4 Hz probe cadence, not every video frame.
        if len(clocks)<10 or float(clocks[-1][0])<frames/fps-.5: raise ValueError("environment did not advance on held fields")
        if cycle and len({r[1] for r in clocks})<50: raise ValueError("day/night cycle stayed fixed")
        probe=subprocess.run(["rtk","proxy","ffprobe","-v","error","-count_frames","-select_streams","v:0",
            "-show_entries","stream=nb_read_frames,width,height,r_frame_rate","-of","json",str(out/(name+".mp4"))],
            check=True,capture_output=True,text=True,timeout=30)
        stream=json.loads(probe.stdout)["streams"][0]
        if stream["nb_read_frames"]!=str(frames): raise ValueError("incomplete encoded clip")
        rows.append({"name":name,"path":name+".mp4","saved_time_s":float(saved[0]),
            "frames":frames,"fps":fps,"environment_clock_end_s":float(clocks[-1][0]),
            "probe":stream,"scope":"native fields held; environment changes only",**receipt})
        print("captured",name,flush=True)
    patch.write(out/"environment-motion.json",{"rows":rows})
    verify(out)


def performance(out):
    verify(out)
    matrix=[("world1k","reference","full"),("world1k","clear","full"),
        ("world1k","weather","half"),("world1k","weather","full"),
        ("ordinary2k","reference","full"),("ordinary2k","weather","half")]
    rows=[]
    for repeat in range(2):
        for case,treatment,quality in (matrix if repeat==0 else reversed(matrix)):
            name=f"perf-{case}-{treatment}-{quality}-{repeat}"
            prefix=out/name
            args=command("collection","daylight",treatment,case,quality)
            args.remove("--fluid25d-recording-gpu-validation")
            # Every shared run crosses a whole day/night cycle, including probe bursts.
            if treatment!="reference":
                args += ["--time-of-day-mode","solar","--time-hours","12","--time-speed-hours-per-second",".75",
                         "--set","atmosphere.time_paused=false"]
            receipt=patch.checked(args+["--headless-render-only","--run-seconds","36","--render-fps","60",
                "--width","1920","--height","1080","--no-validation","--profile-output",str(prefix),
                "--profile-warmup-frames","120","--profile-sample-interval","1"],prefix.with_suffix(".log"))
            with prefix.with_suffix(".passes.csv").open() as file: passes=list(csv.DictReader(file))
            gpu=[float(r["duration_ms"]) for r in passes if r["kind"]=="gpu" and r["label"]=="fluid_25d native presentation total"]
            cpu=[float(r["duration_ms"]) for r in passes if r["kind"]=="cpu" and r["label"]=="headless.frame_service"]
            if len(gpu)<500 or len(cpu)<500: raise ValueError("incomplete timing window")
            rows.append({"case":case,"treatment":treatment,"quality":quality,"repeat":repeat,
                "gpu_p95_ms":render.percentile(gpu),"gpu_p99_ms":render.percentile(gpu,.99),
                "gpu_max_ms":max(gpu),"cpu_p95_ms":render.percentile(cpu),"samples":len(gpu),**receipt})
            print("timed",name,rows[-1]["gpu_p95_ms"],flush=True)
    patch.write(out/"environment-performance.json",{"rows":rows,"scope":"1080p renderer only; full cycles; frame-by-frame sampling; no readback/encoding/solver"})
    verify(out)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out",type=Path)
    parser.add_argument("--phase",choices=("freeze","frames","terrain","motion","performance"),required=True)
    args=parser.parse_args();globals()[args.phase](args.out.resolve())


if __name__=="__main__":main()
