"""Launch the matched dragon showcase from any working directory.

Choose forward hybrid, fixed-quality RT, or the progressive sampled comparison.
Native renderer arguments can follow --. Model preparation stays explicit.
"""
import argparse
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[3]
PRESETS = ROOT / "projects/raytracing/presets/optics"
MODEL = ROOT / "cache/raytracing/stanford-dragon/optical-v1/dragon-optical.glb"
MODES = {
    "rt": ("dragon-demo.json", "rt", "demo"),
    "forward": ("dragon-forward.json", "forward", "sampled"),
    "sampled": ("dragon-glass.json", "rt", "sampled"),
}
# Identical camera values are applied to all three rendering paths.
VIEWS = {
    "whole": {"yaw": .45, "pitch": .35, "distance": 1.75,
              "target-x": 0, "target-y": .32, "target-z": 0},
    "detail": {"yaw": .45, "pitch": .35, "distance": 1.25,
               "target-x": .1, "target-y": .25, "target-z": 0},
    "front": {"yaw": 1.10, "pitch": .35, "distance": 1.75,
              "target-x": 0, "target-y": .32, "target-z": 0},
}


def command(binary, mode, view, rt_samples=None, extra=()):
    if rt_samples is not None and mode != "rt":
        raise ValueError("--rt-samples applies to fixed-quality RT mode")
    preset, renderer, transport = MODES[mode]
    args = [str(Path(binary).resolve()), "--config", str(PRESETS / preset),
            "--renderer", renderer, "--transport-mode", transport,
            "--no-shadows", "--fov", "45"]
    for name, value in VIEWS[view].items():
        args += ["--" + name, str(value)]
    if mode == "forward":
        args += ["--forward-transmission-source", "hybrid"]
    elif mode == "rt":
        args += ["--demo-samples", str(rt_samples or 4)]
    return args + list(extra)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=MODES, default="rt",
                        help="rt: fixed frame quality; forward: approximation; sampled: progressive")
    parser.add_argument("--view", choices=VIEWS, default="whole")
    parser.add_argument("--rt-samples", type=int, choices=(2, 4),
                        help="fixed RT samples per frame (default: 4)")
    parser.add_argument("--binary", type=Path,
                        default=ROOT / "build/dev/projects/raytracing/raytracing")
    parser.add_argument("--print-command", action="store_true",
                        help="print the native command without launching")
    parser.add_argument("renderer_args", nargs=argparse.REMAINDER,
                        help="additional native arguments after --; relative paths use the repo root")
    args = parser.parse_args()
    extra = args.renderer_args
    if extra[:1] == ["--"]:
        extra = extra[1:]
    try:
        native = command(args.binary, args.mode, args.view, args.rt_samples, extra)
    except ValueError as error:
        parser.error(str(error))
    if args.print_command:
        print(shlex.join(native))
        return 0
    if not args.binary.is_file():
        parser.error("build the executable first: cmake --build build/dev --target cubey_project_raytracing")
    if not MODEL.is_file():
        parser.error("prepare the optical model explicitly; see projects/raytracing/README.md")
    return subprocess.run(native, cwd=ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
