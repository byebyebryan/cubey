#!/usr/bin/env python3
"""Front-to-three-quarter, afternoon-to-night saved-water film audition.

Uses the existing offscreen Vulkan renderer and frozen fields. Never installs
gallery media, launches a solver/GUI, or uploads/pushes anything.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import review_water_landscape_v1 as base
from package_landscape_showcase_v1 import check_video, probe

FPS, FRAMES = 60, 480
START_HOURS, HOURS_PER_SECOND = 14.0, .875
REFINED_HOURS_PER_SECOND = .6875  # 14:00 -> 19:30; night is the closing beat.
START_YAW = 0.0  # Verified against four cardinal/diagonal helmet captures.
ORBITS = (30, 45)
SAMPLE_FRAMES = (0, 120, 240, 360, 479)


def freeze(out):
    out.mkdir()
    status = subprocess.check_output(["rtk", "proxy", "git", "status", "--porcelain"], cwd=base.ROOT)
    base.write(out / "protocol.json", {
        "source_head": subprocess.check_output(["rtk", "proxy", "git", "rev-parse", "HEAD"],
                                               cwd=base.ROOT, text=True).strip(),
        "source_status_sha256": hashlib.sha256(status).hexdigest(),
        "source_status": status.decode(),
        "capture_recipe_sha256": base.digest(base.ROOT / "docs/media/showcase/gltf-terrain-recipe.json"),
        "runtime": base.runtime(base.GLTF), "inputs": base.identities(),
        "scope": "Frozen front-view/solar presentation audition; no solver, GUI, gallery install or upload."})


def sample(frame, orbit, speed=HOURS_PER_SECOND):
    if not 0 <= frame < FRAMES:
        raise ValueError("frame outside the eight-second film")
    u = frame / (FRAMES - 1)
    return {"frame": frame, "seconds": frame / FPS,
            # before_frame advances the solar state by 1/FPS even on frame 0.
            "solar_hours": START_HOURS + (frame + 1) / FPS * speed,
            "yaw_degrees": START_YAW + orbit * u * u * (3 - 2 * u)}


def command(output, orbit, refined=False):
    args = base.integrated_command("mountain-local", hours=START_HOURS, exposure=0)
    args[args.index("--capture-camera-yaw") + 1] = str(START_YAW)
    args[args.index("--time-speed-hours-per-second") + 1] = str(
        REFINED_HOURS_PER_SECOND if refined else HOURS_PER_SECOND)
    if refined:
        # Reuse shared automatic exposure rather than inventing a night fill.
        index = args.index("--exposure")
        del args[index:index + 2]
        # 64px reflection sky cannot resolve point stars as a visible backdrop.
        # Capture-only control; no global sky default or shader is changed.
        args += ["--star-intensity", "0"]
    return args + ["--terrain-landscape-depth", "fitted", "--capture", "video",
        "--frames", str(FRAMES), "--fps", str(FPS), "--width", "1920", "--height", "1080",
        "--capture-video-orbit-degrees", str(orbit), "--require-validation",
        "--terrain-water-audit", "--output", str(output)]


def verify(out):
    frozen = json.loads((out / "protocol.json").read_bytes())
    if frozen["runtime"] != base.runtime(base.GLTF) or frozen["inputs"] != base.identities():
        raise ValueError("frozen runtime or inputs changed")


def capture(out, refined=False):
    verify(out)
    rows = []
    for orbit in ((45,) if refined else ORBITS):
        name = f"front-{orbit}-{'refined' if refined else 'solar'}"
        movie = out / (name + "-1080p.mp4")
        receipt = base.run(command(movie, orbit, refined), out / (name + ".log"))
        log = (out / (name + ".log")).read_text()
        for required in ("mode=fitted", "GPU bed/depth/decoded-velocity exact", "hydraulic_dispatches=0"):
            if required not in log:
                raise ValueError("missing capture audit: " + required)
        info = probe(movie)
        if (info["width"], info["height"], info["nb_read_frames"]) != (1920, 1080, "480"):
            raise ValueError("incomplete native film")
        frames = []
        for frame in SAMPLE_FRAMES:
            image = out / f"{name}-{frame:03}.png"
            subprocess.run(["rtk", "proxy", "ffmpeg", "-v", "error", "-n", "-i", str(movie),
                "-vf", f"select=eq(n\\,{frame}),scale=960:540:flags=lanczos", "-frames:v", "1",
                str(image)], check=True)
            frames.append({**sample(frame, orbit, REFINED_HOURS_PER_SECOND if refined else HOURS_PER_SECOND),
                           "image": image.name, "sha256": base.digest(image)})
        rows.append({"orbit_degrees": orbit, "movie": movie.name, "sha256": base.digest(movie),
                     "probe": info, "samples": frames, **receipt})
        print("captured", name, flush=True)
    verify(out)
    base.write(out / ("refined-captures.json" if refined else "solar-captures.json"), {"rows": rows,
        "scope": "Same frozen held storm state/depth fix; face-on opening; shared solar clock. Refined version uses automatic exposure, no point stars and a 19:30 night ending. No live hydrology or GUI."})


def package(out, orbit):
    verify(out)
    source = out / f"front-{orbit}-refined-1080p.mp4"
    row = next(r for r in json.loads((out / "refined-captures.json").read_bytes())["rows"]
               if r["orbit_degrees"] == orbit)
    if base.digest(source) != row["sha256"]:
        raise ValueError("audition source changed")
    package_dir = out / "package"
    package_dir.mkdir()
    master, gallery, poster = (package_dir / n for n in (
        "gltf-terrain-master-1080p.mp4", "gltf-terrain.mp4", "gltf-terrain.png"))
    commands = [
        ["rtk", "proxy", "ffmpeg", "-v", "error", "-n", "-i", str(source), "-an",
         "-vf", "setpts=N/(60*TB)", "-r", "60", "-fps_mode", "cfr", "-frames:v", "480",
         "-c:v", "libx264", "-preset", "medium", "-crf", "20", "-pix_fmt", "yuv420p",
         "-movflags", "+faststart", str(master)],
        ["rtk", "proxy", "ffmpeg", "-v", "error", "-n", "-i", str(master), "-an",
         "-vf", "scale=1280:720:flags=lanczos,setpts=N/(60*TB)", "-r", "60", "-fps_mode", "cfr",
         "-c:v", "libx264", "-preset", "medium", "-crf", "24", "-pix_fmt", "yuv420p",
         "-movflags", "+faststart", str(gallery)],
        ["rtk", "proxy", "ffmpeg", "-v", "error", "-n", "-i", str(gallery), "-ss", "4",
         "-frames:v", "1", "-vf", "format=rgb24", str(poster)],
    ]
    for args in commands:
        subprocess.run(args, check=True)
    master_info, gallery_info = probe(master), probe(gallery)
    check_video(master_info, 1920, 1080)
    check_video(gallery_info, 1280, 720)
    current = json.loads((base.ROOT / "docs/media/showcase/gltf-terrain-recipe.json").read_bytes())
    base.write(package_dir / "gltf-terrain-recipe.json", {
        "schema": "cubey.gltf.saved-water-solar-candidate.v1",
        "status": "Validated candidate; owner film review and gallery promotion remain separate",
        "source_protocol": json.loads((out / "protocol.json").read_bytes()),
        "capture_tool_sha256": base.digest(Path(__file__)),
        "source_capture": row, "postprocess_commands": commands,
        "master_probe": master_info, "gallery_probe": gallery_info,
        "scene": {**current["scene"], "solar_hours": None, "solar_start_hours": START_HOURS,
                  "solar_speed_hours_per_second": REFINED_HOURS_PER_SECOND,
                  "solar_first_frame_hours": sample(0, orbit, REFINED_HOURS_PER_SECOND)["solar_hours"],
                  "solar_last_frame_hours": sample(FRAMES - 1, orbit, REFINED_HOURS_PER_SECOND)["solar_hours"],
                  "automatic_exposure": True, "star_intensity": 0,
                  "camera_start_yaw_degrees": START_YAW, "orbit_degrees": orbit,
                  "animated": "camera, shared solar lighting, render-only surface cues"},
        "license": current["license"],
        "media": {p.name: {"path": p.name, "bytes": p.stat().st_size, "sha256": base.digest(p)}
                  for p in (master, gallery, poster)},
        "boundary": "No solver or native GUI; unchanged held water. Existing gallery, GitHub links and publication receipt are untouched. Not an owner-approved or published replacement yet."})
    verify(out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--phase", choices=("freeze", "capture", "capture-refined", "package"), default="capture")
    parser.add_argument("--orbit", type=int, choices=ORBITS, default=45)
    args = parser.parse_args()
    if args.phase == "freeze":
        freeze(args.out.absolute())
    elif args.phase == "package":
        package(args.out.resolve(strict=True), args.orbit)
    else:
        capture(args.out.resolve(strict=True), args.phase == "capture-refined")
