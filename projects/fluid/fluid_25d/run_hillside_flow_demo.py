#!/usr/bin/env python3
"""Open the accepted dry-start hillside with continuous playback.

By default this changes only camera/view and windowed pacing. Explicit --dye
adds the conserved tracer pulse without changing hydraulic forcing. It never
uses the advance-then-pause path or a finite frame limit. Close the window to
stop; Space toggles pause, R resets, and the UI can adjust playback speed.
"""
from __future__ import annotations

import argparse
import math
import shlex
import subprocess
from pathlib import Path

import run_hillside_sustained_flow_v2 as study


def arguments(camera: str = "source", view: str = "composite", playback: float = 8,
              domain: int = 256, dye: bool = False, markers: bool = False,
              developed: bool = False) -> list[str]:
    if camera not in ("source", "branch", "overview"):
        raise ValueError("camera must be source, branch, or overview")
    if view not in ("composite", "water-isolation", "flow-inspection", "transport-inspection"):
        raise ValueError("unknown review view")
    if view == "transport-inspection" and not dye:
        raise ValueError("transport inspection requires the opt-in dye pulse")
    if domain not in (256, 512):
        raise ValueError("domain must be 256 or 512")
    if not math.isfinite(playback) or not 1 <= playback <= 8:
        raise ValueError("playback must be between 1 and 8")
    args = study.arguments(domain)
    args.remove("--headless")
    args += ["--fluid25d-catchment-view", view,
             "--fluid25d-presentation-time-scale", str(playback),
             "--fluid25d-natural-flow-home-pitch-radians", "-0.72"]
    if camera == "source" and not markers:
        # Keep the established no-option source-context camera path as the
        # water-only launcher default. Opt-in markers use the new close-source
        # framing so the GUI matches the V4 review captures.
        args.append("--fluid25d-hillside-source-context")
    else:
        args += ["--fluid25d-hillside-camera", camera]
    if dye:
        args += ["--fluid25d-dye-pulse-start-seconds", "3600",
                 "--fluid25d-dye-pulse-duration-seconds", "120"]
    if markers:
        args.append("--fluid25d-motion-markers")
    if developed:
        args += ["--fluid25d-hillside-advance-and-continue-seconds", "3300"]
    return args


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=study.DEFAULT_APP)
    parser.add_argument("--camera", choices=("source", "branch", "overview"), default="source")
    parser.add_argument("--view", choices=("composite", "water-isolation", "flow-inspection",
                                         "transport-inspection"),
                        default="composite")
    parser.add_argument("--domain", type=int, choices=(256, 512), default=256)
    parser.add_argument("--dye", action="store_true",
                        help="add conserved dye at 60–62 simulated minutes; water never stops")
    parser.add_argument("--markers", action="store_true",
                        help="show opt-in depth-averaged-motion markers on the hillside")
    parser.add_argument("--developed", action="store_true",
                        help="compute 3300 simulated seconds with progress, then continue playback")
    parser.add_argument("--playback", type=float, default=8)
    parser.add_argument("--print-command", action="store_true")
    options = parser.parse_args()
    app = options.app.resolve()
    # Check the same immutable inputs used by the bounded evidence runner.
    study.pinned_inputs(options.domain, app)
    command = ["rtk", "proxy", str(app),
               *arguments(options.camera, options.view, options.playback, options.domain,
                          options.dye, options.markers, options.developed)]
    print(shlex.join(command), flush=True)
    if not options.print_command:
        raise SystemExit(subprocess.run(command, cwd=study.ROOT).returncode)


if __name__ == "__main__":
    main()
