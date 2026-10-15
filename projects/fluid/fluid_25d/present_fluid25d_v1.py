#!/usr/bin/env python3
"""Reproducible, hardware-headless Fluid 2.5D editorial capture; no solver work.

Auditions are not performance trials. Each output frame is rendered natively;
hydraulic planes are held between exact recorded native states, never interpolated.
"""
from __future__ import annotations

import argparse
import hashlib
import html
import json
import math
from pathlib import Path
import re
import shutil
import struct
import subprocess

import run_terrain_rain_demo as demo
from review_rain_response_v1 import closest, verify
from review_terrain_presentation_v1 import run_capture

ROOT = Path(__file__).resolve().parents[3]
MEDIA = ROOT / "docs/media/showcase"
PREVIOUS_REPORT = "http://starship:8787/reports/9072f6efec32e982a19c3cac339e657af57906cd8efb059bdd5e192503fe7817/"


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read(path):
    return json.loads(Path(path).read_bytes())


def write(path, value):
    with Path(path).open("x") as stream:
        json.dump(value, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")


def recipe(name, camera, start, end, seconds, *, dots=False, sweep=0, caption="", easing="legacy"):
    if not 0 <= start < end <= 21600 or not 0 < seconds <= 40 or not 0 <= sweep <= .6:
        raise ValueError("bounded ordered editorial interval required")
    return dict(name=name, camera=camera, start_s=start, end_s=end,
                seconds=seconds, fps=60, frames=round(seconds*60), dots=dots,
                sweep=sweep, easing=easing, caption=caption)


def framing_shots():
    return [dict(name=name, target=[x,z,span], pitch=pitch)
            for name,x,z,span,pitch in (("wide-front",.745,.592,2400,-1.10),
                                      ("stream-network",.76,.60,1800,-1.10),
                                      ("rapids",.741,.562,1200,-1.05),
                                      ("collection-interior",.90,.215,1400,-1.18))]


def lake_framing_shots():
    """Observation-only auditions; the packaged highlight stays unchanged."""
    return [dict(name=name, target=[x,z,span], pitch=pitch, yaw=yaw)
            for name,x,z,span,pitch,yaw in (
                ("lake-inward",.88,.22,2200,-.90,1.57),
                ("lake-catchment",.87,.225,2600,-1.00,1.57),
                ("lake-oblique",.88,.22,2000,-.85,2.1),
                ("central-lakes",.64,.48,2800,-1.00,1.57))]


def auditions():
    return [dict(recipe("audition-" + name, "runoff", 2400, 7200, 8, dots=dots, sweep=sweep,
                        easing="eased" if sweep else "legacy"),target=[.76,.60,1800],pitch=-1.10)
            for name, dots, sweep in (("fixed-water", False, 0), ("moving-water", False, .35),
                                     ("fixed-dots", True, 0), ("moving-dots", True, .35))]


def lake_auditions():
    return [dict(recipe(name, "collection", 1800, 7200, 8, sweep=sweep,
                        easing="eased" if sweep else "legacy"),
                 target=target, pitch=pitch, yaw=2.1)
            for name,target,pitch,sweep in (
                ("lake-fixed", [.88,.22,2000], -.85, 0),
                ("lake-moving", [.88,.22,2000], -.85, .18),
                ("lake-upstream", [.86,.235,2400], -1.00, .16))]


def lake_wide_framing_shots():
    return [dict(name=name, target=[x,z,span], pitch=pitch, yaw=yaw)
            for name,x,z,span,pitch,yaw in (
                ("lake-wide",.85,.29,3200,-.95,2.10),
                ("lake-landscape",.85,.29,3400,-.90,2.40))]


def lake_formation():
    # Six native-rendered seconds; the review edit adds explicit endpoint holds.
    return [dict(recipe(name, "collection", 0, end, 6, sweep=.10, easing="eased"),
                 target=[.825,.315,3200], pitch=-1.02, yaw=1.85)
            for name,end in (("formation-two-hours",7200),("formation-six-hours",21600))]


def patch_framing_shots():
    # No explicit target: overview centers the full terrain's 3D bounds.
    return [dict(name=name,camera="overview",pitch=-1.20,yaw=.35,distance_m=distance)
            for name,distance in (("whole-patch",23000),("whole-patch-loose",25000))]


def patch_formation():
    return [dict(recipe("formation-whole-patch","overview",0,21600,6),
                 **{key:value for key,value in patch_framing_shots()[0].items() if key not in ("name","camera")})]


def patch_orbit():
    return [dict(recipe("formation-low-orbit","overview",0,21600,6,sweep=.60,easing="eased"),
                 pitch=-.55,yaw=.60,distance_m=13500)]


def patch_orbit_framing_shots():
    shot=patch_orbit()[0]
    return [dict(name="orbit-"+name,camera="overview",pitch=shot["pitch"],
                 yaw=shot["yaw"]+offset,distance_m=shot["distance_m"])
            for name,offset in (("start",-.30),("middle",0),("end",.30))]


def patch_presentation_refinement():
    """Opt-in eight-second 15-degree audition, not the packaged showcase."""
    return dict(recipe("joining-refined", "overview", 0, 21600, 8,
                       sweep=.60, easing="eased"),
                pitch=-math.radians(15), yaw=.60, distance_m=13500,
                vertical_shift=.06, opening_frames=24, closing_frames=120,
                width=1920, height=1080, crf=20, maximum_hold_s=317,
                tuning=str(Path(__file__).resolve().parent/'fixtures/presentation-v1/soft-rain.json'))


def patch_solar_refinement():
    """Late afternoon to sunset; cosmetic solar time, not hydraulic time."""
    return dict(patch_presentation_refinement(), name="joining-dusk-1080p",
                solar_start_hour=15.5, solar_end_hour=18.0,
                solar_day=80, solar_latitude=45, solar_azimuth_offset=-95,
                solar_twilight_strength=.18, solar_horizon_warmth=.25)


def story():
    return [dict(recipe("story-rain", "overview", 0, 1800, 6, caption="RAIN ACROSS THE TERRAIN"),
                 target=[.50,.50,4200],pitch=-1.30),
            dict(recipe("story-runoff", "runoff", 1800, 6600, 10, sweep=.26,
                   easing="eased", caption="DOWNHILL STREAMS"),target=[.76,.60,1800],pitch=-1.10),
            dict(recipe("story-collection", "collection", 6600, 9000, 8,
                   caption="BASIN COLLECTION / RAIN STOPS AT 2 HOURS"),target=[.90,.215,1400],pitch=-1.18,yaw=1.0),
            dict(recipe("story-retreat", "runoff", 9000, 21600, 12,
                   caption="RAIN OFF / STREAMS RETREAT"),target=[.76,.60,1800],pitch=-1.10)]


def hero():
    return [dict(recipe("fluid-25d", "runoff", 2400, 6600, 8, sweep=.35, easing="eased"),
                 target=[.76,.60,1800],pitch=-1.10)]


def command(case, recording, shot, output, *, app=demo.RELEASE_APP):
    solar={}
    if "solar_start_hour" in shot:
        if "sun_elevation" in shot:
            raise ValueError("solar clock and manual sun are separate auditions")
        start,end=shot["solar_start_hour"],shot["solar_end_hour"]
        if not 0<=start<end<=24 or shot["frames"]<2:
            raise ValueError("solar audition requires an ordered same-day interval")
        solar=dict(environment="shared",environment_time_hours=start,
                   environment_cycle_speed=(end-start)/((shot["frames"]-1)/shot["fps"]))
    argv = demo.viewer_command(case, recording, "Fluid 2.5D presentation",
        shot.get("width",1280), shot.get("height",720),
        live=False, start=shot["start_s"], camera=shot["camera"], app=app,**solar)
    if solar:
        argv += ["--day-of-year",str(shot.get("solar_day",80)),
                 "--latitude-degrees",str(shot.get("solar_latitude",45)),
                 "--sun-azimuth-offset",str(shot.get("solar_azimuth_offset",0))]
        for key,flag in (("solar_twilight_strength","--twilight-strength"),
                         ("solar_horizon_warmth","--twilight-horizon-warmth")):
            if key in shot:
                argv += [flag,str(shot[key])]
    if not shot["dots"]:
        argv.remove("--fluid25d-motion-markers")
    if shot.get("target"):
        for flag,value in zip(("target-x","target-z","span-m"),shot["target"]):
            argv += ["--fluid25d-native-camera-"+flag,str(value)]
    if "pitch" in shot:
        argv += ["--fluid25d-native-camera-pitch",str(shot["pitch"])]
    if "yaw" in shot:
        argv += ["--fluid25d-native-camera-yaw",str(shot["yaw"])]
    if "distance_m" in shot:
        argv += ["--fluid25d-home-camera-distance-m",str(shot["distance_m"])]
    if "vertical_shift" in shot:
        argv += ["--fluid25d-native-camera-vertical-shift",str(shot["vertical_shift"])]
    if "rain_strength" in shot:
        argv += ["--fluid25d-rain-visual-strength",str(shot["rain_strength"])]
    if "tuning" in shot:
        argv += ["--fluid25d-scenic-tuning",str(shot["tuning"])]
    if "sun_elevation" in shot:
        argv += ["--fluid25d-environment","shared","--time-of-day-mode","manual",
                 "--sun-elevation",str(shot["sun_elevation"]),
                 "--sun-azimuth",str(shot["sun_azimuth"]),"--pause-time"]
    argv[argv.index("--frames")+1] = str(shot["frames"])
    active=shot["frames"]-shot.get("opening_frames",0)-shot.get("closing_frames",0)
    if active<2:
        raise ValueError("endpoint holds require at least two active frames")
    interval = (shot["end_s"]-shot["start_s"])/(active-1)
    if shot.get("opening_frames",0) or shot.get("closing_frames",0):
        # The native CLI stores float32. Round upward only for this opt-in
        # schedule so its final request reaches (and clamps to) the last save.
        rounded=struct.unpack('<f',struct.pack('<f',interval))[0]
        if rounded<interval:
            bits=struct.unpack('<I',struct.pack('<f',rounded))[0]
            rounded=struct.unpack('<f',struct.pack('<I',bits+1))[0]
        interval=rounded
    for part in ("opening","closing"):
        if shot.get(part+"_frames",0):
            argv += ["--fluid25d-recording-"+part+"-frames",str(shot[part+"_frames"])]
    argv += ["--headless", "--capture", "video", "--fps", "60",
             "--fluid25d-recording-frame-interval-seconds", str(interval),
             "--fluid25d-recording-gpu-validation", "--output", str(output)]
    if shot["sweep"]:
        argv += ["--fluid25d-native-camera-sweep", str(shot["sweep"])]
        if shot["easing"] != "legacy":
            argv += ["--fluid25d-native-camera-motion", shot["easing"]]
    return argv


def probe(path):
    output = subprocess.check_output(["rtk", "proxy", "ffprobe", "-v", "error", "-count_frames",
        "-select_streams", "v:0", "-show_entries",
        "stream=codec_name,profile,pix_fmt,width,height,r_frame_rate,avg_frame_rate,nb_read_frames,duration",
        "-of", "json", str(path)], text=True, timeout=60)
    return json.loads(output)["streams"][0]


def check_video(stream, frames, seconds, width=1280, height=720):
    if (stream.get("codec_name") != "h264" or stream.get("pix_fmt") != "yuv420p" or
        (stream["width"], stream["height"]) != (width, height) or
        stream.get("r_frame_rate") != "60/1" or stream.get("avg_frame_rate") != "60/1" or
        stream.get("nb_read_frames") != str(frames) or
        not math.isfinite(float(stream["duration"])) or
        abs(float(stream["duration"])-seconds) > 1/60):
        raise ValueError("encoded media does not match native resolution/60fps editorial contract")


def encode(source, output, crf=24):
    argv = ["rtk", "proxy", "ffmpeg", "-v", "error", "-i", str(source), "-an",
            "-vf", "setpts=N/(60*TB)", "-r", "60", "-fps_mode", "cfr", "-c:v", "libx264",
            "-preset", "medium", "-crf", str(crf), "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(output)]
    subprocess.run(argv, check=True, timeout=180)
    return argv


def check_timeline(raw, shot):
    matches = re.findall(r"fluid_25d_recording_capture: output_frame=(\d+) requested_s=(\S+) saved_s=(\S+)", raw)
    if len(matches) != shot["frames"] or [int(v[0]) for v in matches] != list(range(shot["frames"])):
        raise ValueError("capture misses native output frames")
    requested, saved = [float(v[1]) for v in matches], [float(v[2]) for v in matches]
    if any(not math.isfinite(t) or t<0 for t in (*requested,*saved)):
        raise ValueError("capture timestamps must be finite and nonnegative")
    if any(state>time+1e-3 for time,state in zip(requested,saved)):
        raise ValueError("capture cannot display a future hydraulic state")
    if any(b < a for a, b in zip(saved, saved[1:])) or len(set(saved)) < 10:
        raise ValueError("film does not advance real recorded hydraulic states")
    opening,closing=shot.get("opening_frames",0),shot.get("closing_frames",0)
    active=shot["frames"]-opening-closing
    if active<2:
        raise ValueError("endpoint holds leave no active sequence")
    if any(abs(t-(shot["start_s"]+(shot["end_s"]-shot["start_s"])*
                   min(max(i-opening,0),active-1)/(active-1)))>.02
           for i,t in enumerate(requested)):
        raise ValueError("capture does not match the authored frame-to-physical-time schedule")
    if abs(requested[0]-shot["start_s"]) > 1e-3 or abs(requested[-1]-shot["end_s"]) > .01:
        raise ValueError("capture misses authored physical endpoints")
    if max(a-b for a, b in zip(requested, saved)) > shot.get("maximum_hold_s",75):
        raise ValueError("recorded state too far behind the requested editorial clock")
    return dict(unique_hydraulic_states=len(set(saved)), first_saved_s=saved[0],
                last_saved_s=saved[-1], maximum_hold_physical_s=max(a-b for a,b in zip(requested,saved)))


def render(case, recording, shot, out):
    frames = read(recording)["frames"]
    shot = dict(shot, intended_start_s=shot["start_s"], intended_end_s=shot["end_s"],
                start_s=closest(frames, shot["start_s"])["time_s"],
                end_s=closest(frames, shot["end_s"])["time_s"])
    path, log = out/(shot["name"]+".mp4"), out/(shot["name"]+".log")
    source = out/(shot["name"]+"-source.mp4")
    result = run_capture(command(case, recording, shot, source), log, timeout=300)
    timeline = check_timeline(log.read_text(), shot)
    native_probe = probe(source)
    if native_probe.get("nb_read_frames") != str(shot["frames"]):
        raise ValueError("native renderer did not produce every master frame")
    encoding = encode(source, path, shot.get("crf",24))
    stream = probe(path)
    check_video(stream, shot["frames"], shot["seconds"],shot.get("width",1280),shot.get("height",720))
    preview = out/(shot["name"]+"-frames.png")
    # Editorial contact sheet only, extracted from real rendered frames.
    subprocess.run(["rtk", "proxy", "ffmpeg", "-v", "error", "-i", str(path),
        "-vf", f"fps=4/{shot['seconds']},scale=640:360,tile=2x2", "-frames:v", "1", str(preview)],
        check=True, timeout=60)
    poster = out/(shot["name"]+".png")
    subprocess.run(["rtk", "proxy", "ffmpeg", "-v", "error", "-ss", str(shot["seconds"]*.6),
        "-i", str(path), "-frames:v", "1", "-vf", "format=rgb24", str(poster)], check=True, timeout=60)
    return dict(shot=shot, path=path.name, poster=poster.name, contact_sheet=preview.name,
                source=source.name, source_sha256=digest(source), native_probe=native_probe,
                encode_command=encoding, sha256=digest(path), probe=stream, **timeline, **result)


def report(out, rows, title):
    export = out/"export"
    export.mkdir()
    cards = []
    for row in rows:
        for key in ("path", "poster", "contact_sheet"):
            shutil.copy2(out/row[key], export/row[key])
        shot = row["shot"]
        cards.append(f"""<article><h2>{html.escape(shot['name'])}</h2>
<video controls muted loop playsinline preload="metadata" poster="{row['poster']}" src="{row['path']}"></video>
<p>{shot['seconds']} s · {shot['camera']} · {'dots/trails' if shot['dots'] else 'water only'} ·
{shot['sweep']:.2f} rad {shot['easing']} arc · {row['unique_hydraulic_states']} real hydraulic states</p>
<details><summary>Matched rendered frames</summary><img src="{row['contact_sheet']}" alt="Four native rendered frames"></details></article>""")
    page = f"""<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{html.escape(title)}</title><style>body{{margin:0;background:#131c24;color:#e8eef3;font:17px/1.5 system-ui}}main{{max-width:1380px;margin:auto;padding:32px}}h1{{font-size:2rem}}.grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,480px),1fr));gap:24px}}article{{background:#202d37;border-radius:12px;padding:16px}}h2{{font-size:1.1rem}}video,img{{width:100%;border-radius:6px}}a{{color:#9fdbff}}button{{padding:10px 16px;margin:8px 8px 20px 0;background:#b8e4ed;color:#12232a;border:0;border-radius:6px;cursor:pointer}}</style>
<main><nav><a href="/reports/#project=cubey">Cubey reports</a> · <a href="{PREVIOUS_REPORT}">Earlier rain-response evidence</a></nav>
<h1>{html.escape(title)}</h1><p>Actual Cubey Vulkan rendering of recorded SynxFlow fields on the unchanged Mountain 1k/16m terrain.
Timelapse: physical states are held between native saves. Rain, foam and optional dots are visual cues, not native water parcels. This is not a live-FPS test.</p>
<button id="play">Play all from start</button><button id="pause">Pause all</button><div class="grid">{''.join(cards)}</div>
<p>Review at this size first: can you read flowing water and its terrain relationship? Camera movement must add information, not hide motion.</p></main>
<script>document.querySelector('#play').onclick=()=>{{for(const v of document.querySelectorAll('video')){{v.currentTime=0;v.play().catch(()=>{{}})}}}};document.querySelector('#pause').onclick=()=>{{for(const v of document.querySelectorAll('video'))v.pause()}};</script></html>"""
    with (export/"index.html").open("x") as stream:
        stream.write(page)
    shutil.copy2(out/"captures.json", export/"captures.json")


def capture(a):
    out = a.out.absolute()
    if out.exists() or not out.parent.is_dir() or out.parent != ROOT/"outputs/showcase":
        raise ValueError("use a fresh direct leaf under outputs/showcase")
    out.mkdir()
    case = (a.study/"cases/mountain-patch-1024-16m").resolve(strict=True)
    result, manifest = verify(a.recording, case)
    record = (a.recording/"recording/recording.json").resolve(strict=True)
    frozen = dict(source_revision=subprocess.check_output(["rtk","proxy","git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
        source_dirty_diff_sha256=hashlib.sha256(subprocess.check_output(["rtk","proxy","git","diff","HEAD"],cwd=ROOT)).hexdigest(),
        harness_sha256=digest(Path(__file__)),
        app_sha256=digest(demo.RELEASE_APP), recording_sha256=digest(record),
        bed_sha256=manifest["bed"]["sha256"], identity=demo.case_identity(case),
        shaders={p.name:digest(p) for p in sorted((demo.RELEASE_APP.parent/"shaders").glob("*.spv"))},
        scope="native 720p60 headless recorded-field timelapse, not live solver/scanout performance")
    write(out/"protocol.json", frozen)
    framing_modes = {"framing":framing_shots,"lake-framing":lake_framing_shots,
                     "lake-wide-framing":lake_wide_framing_shots,"patch-framing":patch_framing_shots,
                     "patch-orbit-framing":patch_orbit_framing_shots}
    if a.mode in framing_modes:
        rows = []
        saved = closest(manifest["frames"],5400)["time_s"]
        for shot in framing_modes[a.mode]():
            path = out/(shot["name"]+".png")
            argv = demo.viewer_command(case,record,"Fluid 2.5D framing",1280,720,
                                      live=False,start=saved,camera=shot.get("camera","runoff"),app=demo.RELEASE_APP)
            argv.remove("--fluid25d-motion-markers")
            argv[argv.index("--frames")+1]="1"
            if shot.get("target"):
                for flag,value in zip(("target-x","target-z","span-m"),shot["target"]):
                    argv += ["--fluid25d-native-camera-"+flag,str(value)]
            argv += ["--fluid25d-native-camera-pitch",str(shot["pitch"]),"--headless",
                     "--fluid25d-recording-gpu-validation","--output",str(path)]
            if "yaw" in shot:
                argv += ["--fluid25d-native-camera-yaw",str(shot["yaw"])]
            if "distance_m" in shot:
                argv += ["--fluid25d-home-camera-distance-m",str(shot["distance_m"])]
            receipt=run_capture(argv,out/(shot["name"]+".log"))
            rows.append(dict(shot=shot,path=path.name,sha256=digest(path),saved_s=saved,**receipt))
            print("captured",shot["name"],flush=True)
        write(out/"captures.json",rows)
        return
    shots = (patch_orbit() if a.mode == "patch-orbit" else
             patch_formation() if a.mode == "patch-formation" else
             lake_formation() if a.mode == "lake-formation" else
             lake_auditions() if a.mode == "lake-audition" else auditions() if a.mode == "audition"
             else hero() if a.mode == "hero" else story())
    rows = []
    for shot in shots:
        print("capturing", shot["name"], flush=True)
        rows.append(render(case, record, shot, out))
        if digest(demo.RELEASE_APP) != frozen["app_sha256"]:
            raise ValueError("renderer changed during the capture batch")
        print("qualified", shot["name"], rows[-1]["unique_hydraulic_states"], flush=True)
    write(out/"captures.json", rows)
    report(out, rows, "Fluid 2.5D — cinematic " + a.mode)


def assemble(a):
    out = a.out.resolve(strict=True)
    rows = read(out/"captures.json")
    if len(rows)!=4 or [row["shot"]["name"] for row in rows] != [shot["name"] for shot in story()]:
        raise ValueError("need the four qualified story segments")
    for row in rows:
        if digest(out/row["path"])!=row["sha256"]:
            raise ValueError("qualified segment changed")
        check_video(probe(out/row["path"]),row["shot"]["frames"],row["shot"]["seconds"])
    if any(left["last_saved_s"] > right["first_saved_s"]+.001 for left,right in zip(rows,rows[1:])):
        raise ValueError("story reverses native physical time across a cut")
    publication = out/"fluid-25d-story.mp4"
    argv = ["rtk","proxy","ffmpeg","-v","error"]
    for row in rows:
        argv += ["-i",str(out/row["path"])]
    filters=[]
    for index,row in enumerate(rows):
        title=row["shot"]["caption"]
        filters.append(f"[{index}:v]drawbox=x=0:y=0:w=iw:h=88:color=black@0.38:t=fill:enable='lt(t,3)',"
            f"drawtext=text='{title}':x=24:y=16:fontcolor=white:fontsize=24:enable='lt(t,3)',"
            "drawtext=text='Recorded native water / accelerated physical time':x=24:y=52:fontcolor=white:fontsize=17:enable='lt(t,3)'"
            f"[v{index}]")
    filters.append("".join(f"[v{i}]" for i in range(4))+"concat=n=4:v=1:a=0[v]")
    argv += ["-filter_complex",";".join(filters),"-map","[v]","-an","-r","60","-fps_mode","cfr",
             "-c:v","libx264","-preset","medium","-crf","24","-pix_fmt","yuv420p",
             "-movflags","+faststart",str(publication)]
    subprocess.run(argv,check=True,timeout=300)
    stream=probe(publication)
    check_video(stream,2160,36)
    poster=out/"fluid-25d-story.png"
    subprocess.run(["rtk","proxy","ffmpeg","-v","error","-ss","18","-i",str(publication),
                    "-frames:v","1","-vf","format=rgb24",str(poster)],check=True,timeout=60)
    write(out/"story-edit.json",dict(command=argv,path=publication.name,poster=poster.name,
                                    sha256=digest(publication),probe=stream,segments=rows))
    shutil.copy2(publication,out/"export"/publication.name)
    shutil.copy2(poster,out/"export"/poster.name)
    # Assemble a focused, reproducible review page; comparisons stay in the audition report.
    page=out/"export"/"index.html"
    content=page.read_text()
    heading='<h1>Fluid 2.5D — cinematic story</h1>'
    player='<section><h2>Complete 36-second project film</h2><video controls muted playsinline preload="metadata" poster="fluid-25d-story.png" src="fluid-25d-story.mp4"></video><p>Rain → streams → basin collection → retreat. Four purposeful cuts; no hydraulic interpolation.</p></section>'
    page.write_text(content.replace(heading,heading+player))
    print("qualified complete story",stream["nb_read_frames"],flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("audition", "lake-audition", "lake-formation", "patch-formation", "patch-orbit", "story", "framing", "lake-framing", "lake-wide-framing", "patch-framing", "patch-orbit-framing", "hero", "assemble"))
    parser.add_argument("--study", type=Path, required=True)
    parser.add_argument("--recording", type=Path, required=True, help="qualified sealed rain-response study root")
    parser.add_argument("--out", type=Path, required=True)
    args=parser.parse_args()
    assemble(args) if args.mode=="assemble" else capture(args)


if __name__ == "__main__":
    main()
