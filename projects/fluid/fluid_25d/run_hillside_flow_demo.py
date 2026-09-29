#!/usr/bin/env python3
"""Open the accepted dry-start hillside with continuous, water-only playback.

This launcher changes only camera/view and windowed pacing. It never uses the
optional advance-then-pause path or a finite frame limit. Close the window to
stop; Space toggles pause, R resets, and the UI can adjust playback speed.
"""
from __future__ import annotations

import argparse
import math
import shlex
import subprocess
from pathlib import Path

import run_hillside_sustained_flow_v2 as study


def arguments(camera: str = "source", view: str = "composite", playback: float = 8) -> list[str]:
    if camera not in ("source", "overview"):
        raise ValueError("camera must be source or overview")
    if view not in ("composite", "water-isolation", "flow-inspection"):
        raise ValueError("only the water-only review views are available")
    if not math.isfinite(playback) or not 1 <= playback <= 8:
        raise ValueError("playback must be between 1 and 8")
    args = study.arguments(256)
    args.remove("--headless")
    args += ["--fluid25d-catchment-view", view,
             "--fluid25d-presentation-time-scale", str(playback),
             "--fluid25d-natural-flow-home-pitch-radians", "-0.72"]
    if camera == "source":
        args.append("--fluid25d-hillside-source-context")
    return args


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=study.DEFAULT_APP)
    parser.add_argument("--camera", choices=("source", "overview"), default="source")
    parser.add_argument("--view", choices=("composite", "water-isolation", "flow-inspection"),
                        default="composite")
    parser.add_argument("--playback", type=float, default=8)
    parser.add_argument("--print-command", action="store_true")
    options = parser.parse_args()
    app = options.app.resolve()
    # Check the same immutable inputs used by the bounded evidence runner.
    study.pinned_inputs(256, app)
    command = ["rtk", "proxy", str(app),
               *arguments(options.camera, options.view, options.playback)]
    print(shlex.join(command), flush=True)
    if not options.print_command:
        raise SystemExit(subprocess.run(command, cwd=study.ROOT).returncode)


if __name__ == "__main__":
    main()
