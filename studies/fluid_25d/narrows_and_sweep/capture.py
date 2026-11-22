#!/usr/bin/env python3
"""Capture Narrows and Sweep with a backdrop from Cubey's Atmosphere project."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

from solar_sky import SOLAR_HOURS, capture_solar_sky

STUDY = Path(__file__).resolve().parent
ROOT = STUDY.parents[2]
SOLVER = ROOT / "projects/fluid/sim/fluid_25d"
ATMOSPHERE_LIGHTING = ROOT / "src/cubey/render/atmosphere_environment.cpp"
WIDTH, HEIGHT, FPS, FRAMES = 1280, 720, 30, 360
POSTER_FRAME = 275
DEFAULT_ATMOSPHERE = ROOT / "build/dev/projects/atmosphere/atmosphere"


def mesa_manifest(path: Path | None) -> Path:
    if path is not None:
        return path.resolve(strict=True)
    for directory in ("/etc/glvnd/egl_vendor.d", "/usr/share/glvnd/egl_vendor.d"):
        for manifest in sorted(Path(directory).glob("*mesa*.json")):
            return manifest
    raise ValueError("Pass --mesa-egl with the Mesa GLVND vendor manifest")


def file_identity(path: Path) -> dict:
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"bytes": path.stat().st_size, "sha256": digest}


def capture(output: Path, manifest: Path, atmosphere: Path, vulkan_icd: Path | None) -> None:
    output.mkdir(parents=True, exist_ok=False)
    commands = []

    def record(command: list[str]) -> list[str]:
        command = [str(part) for part in command]
        commands.append(command)
        print(shlex.join(command), flush=True)
        return command

    def run(command: list[str], **kwargs) -> None:
        subprocess.run(record(command), cwd=ROOT, check=True, **kwargs)

    def version(command: list[str]) -> str:
        return subprocess.check_output(command, cwd=ROOT, text=True).splitlines()[0]

    sky_shader_dir = atmosphere.parent / "shaders"
    sky_binaries = [atmosphere, *(sky_shader_dir / name for name in (
        "atmosphere.vert.spv", "atmosphere.frag.spv",
        "forward_pbr_post.vert.spv", "forward_pbr_post.frag.spv",
    ))]
    sky_identity = {str(path): file_identity(path) for path in sky_binaries}

    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = ["-std=c++20", "-O2", "-Wall", "-Wextra", "-Wpedantic"]
    pkg = shlex.split(
        subprocess.check_output(["pkg-config", "--cflags", "--libs", "egl", "gl"], text=True)
    )
    simulator = output / "simulate"
    renderer = output / "render"
    oracle = SOLVER / "fluid_25d_finite_volume_oracle.cpp"
    sources = [STUDY / "simulate.cpp", oracle]
    run([*compiler, *flags, "-I", ROOT / "include", "-I", SOLVER, *sources, "-o", simulator])
    run([*compiler, *flags, "-I", ROOT / "include", STUDY / "render.cpp",
         ATMOSPHERE_LIGHTING, "-o", renderer, *pkg])

    render_trace = output / "trace/fields.f32"
    film = output / "film.mp4"
    poster = output / "poster.png"
    ffmpeg = ["ffmpeg", "-hide_banner", "-nostdin", "-v", "error", "-n"]
    delivery = ["-an", "-c:v", "libx264", "-threads", "4", "-pix_fmt", "yuv420p",
                "-movflags", "+faststart"]
    sky_env = dict(os.environ)
    sky_env.setdefault("LP_NUM_THREADS", "4")
    if vulkan_icd is not None:
        sky_env["VK_DRIVER_FILES"] = str(vulkan_icd)
        sky_env.pop("VK_ADD_DRIVER_FILES", None)
    sky_plates = capture_solar_sky(output, atmosphere, WIDTH, HEIGHT, sky_env, run)
    run([simulator, output / "trace"])
    with (output / "hydraulics.json").open("wb") as summary:
        run([sys.executable, STUDY / "measure.py", output / "trace"], stdout=summary)

    render_env = {**os.environ, "LIBGL_ALWAYS_SOFTWARE": "1",
                  "__EGL_VENDOR_LIBRARY_FILENAMES": str(manifest)}
    render_env.setdefault("LP_NUM_THREADS", "4")
    render_command = record([renderer, STUDY, render_trace, WIDTH, HEIGHT, *sky_plates])
    encode_command = record([
        *ffmpeg, "-f", "rawvideo", "-pixel_format", "rgba", "-video_size",
        f"{WIDTH}x{HEIGHT}", "-framerate", FPS, "-i", "pipe:0",
        "-vf", "vflip", *delivery, "-preset", "medium", "-crf", "17", film,
    ])
    with (output / "render.log").open("wb") as log, subprocess.Popen(
        render_command, cwd=ROOT, env=render_env, stderr=log, stdout=subprocess.PIPE
    ) as producer:
        with producer.stdout as raw_frames:
            subprocess.run(encode_command, cwd=ROOT, stdin=raw_frames, check=True)
        if producer.wait():
            raise RuntimeError(f"Narrows and Sweep renderer failed. See {output / 'render.log'}")

    run([*ffmpeg, "-i", film, "-vf", f"select=eq(n\\,{POSTER_FRAME})", "-frames:v", "1", poster])

    source_files = [*sources, STUDY / "render.cpp", STUDY / "relief.frag", STUDY / "relief.vert",
                    STUDY / "sky.vert", STUDY / "sky.frag", STUDY / "solar_sky.py",
                    STUDY / "measure.py", ATMOSPHERE_LIGHTING,
                    ROOT / "include/cubey/render/atmosphere_environment.h",
                    STUDY / "capture.py", SOLVER / "fluid_25d_finite_volume_oracle.h",
                    SOLVER / "fluid_25d_scenarios.h"]
    receipt = {
        "scene": "Narrows and Sweep",
        "sourceBaseRevision": version(["git", "rev-parse", "HEAD"]),
        "simulation": "Cubey CPU finite-volume oracle",
        "renderer": (output / "render.log").read_text().splitlines(),
        "tools": {"compiler": version([*compiler, "--version"]),
                  "ffmpeg": version(["ffmpeg", "-version"])},
        "film": {"width": WIDTH, "height": HEIGHT, "fps": FPS, "frames": FRAMES, "msaa": 4},
        "posterFrame": POSTER_FRAME,
        "simulationSecondsPerFrame": 0.18,
        "camera": {"yawDegrees": [3, 22], "pitchDownDegrees": [57, 39],
                   "pitchSettledFrame": 275,
                   "distanceM": [248, 251], "targetYMetres": [47.5, 43.5],
                   "verticalFovDegrees": 32,
                   "interpolation": "smoothstep"},
        "background": {
            "source": "Cubey Atmosphere",
            "mode": "sky-only-no-ground-occlusion",
            "verticalFovDegrees": 65,
            "solarHours": SOLAR_HOURS,
            "interpolation": "linear between display-light samples",
            "binaries": sky_identity,
            "device": (output / "sky.log").read_text().splitlines(),
            "vulkanIcd": str(vulkan_icd) if vulkan_icd else sky_env.get("VK_DRIVER_FILES"),
        },
        "observedHydraulics": json.loads((output / "hydraulics.json").read_text()),
        "sourceFiles": {str(path.relative_to(ROOT)): file_identity(path) for path in source_files},
        "outputs": {
            str(path.relative_to(output)): file_identity(path)
            for path in (simulator, renderer, film, poster, *sky_plates,
                         *(plate.with_suffix(".png") for plate in sky_plates), render_trace,
                         output / "trace/ledger.csv", output / "trace/sections.csv",
                         output / "trace/full-grid-fringe.csv", output / "hydraulics.json")
        },
        "commands": commands,
    }
    (output / "capture.json").write_text(json.dumps(receipt, indent=2) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="fresh directory for the trace and capture")
    parser.add_argument("--mesa-egl", type=Path, help="Mesa GLVND vendor manifest")
    parser.add_argument("--atmosphere", type=Path, default=DEFAULT_ATMOSPHERE,
                        help="built Cubey Atmosphere executable")
    parser.add_argument("--vulkan-icd", type=Path,
                        help="override the ICD for baking the sky, e.g. Mesa lavapipe")
    args = parser.parse_args()
    try:
        vulkan_icd = args.vulkan_icd.resolve(strict=True) if args.vulkan_icd else None
        capture(args.output.resolve(), mesa_manifest(args.mesa_egl),
                args.atmosphere.resolve(strict=True), vulkan_icd)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))


if __name__ == "__main__":
    main()
