#!/usr/bin/env python3
"""Launch continuous rainfall on the pinned mountain crop, not a timed pulse."""

from __future__ import annotations

import argparse
from pathlib import Path
import shlex
import subprocess

import run_hillside_rain_v1 as study


def arguments(case: str = "main", camera: str = "travel", *, markers: bool = False,
              developed: bool = False) -> list[str]:
    if case not in study.RATES:
        raise ValueError("unknown predefined rain case")
    args = study.arguments(study.RATES[case], headless=False, camera=camera, markers=markers)
    args += ["--fluid25d-presentation-time-scale", "8"]
    if developed:
        args += ["--fluid25d-hillside-advance-and-continue-seconds", "3600"]
    return args


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=study.v2.DEFAULT_APP)
    parser.add_argument("--case", choices=tuple(study.RATES), default="main")
    parser.add_argument("--camera", choices=("overview", "travel", "collection"), default="travel")
    parser.add_argument("--markers", action="store_true")
    parser.add_argument("--developed", action="store_true", help="execute the first physical hour, then continue; no prefill")
    parser.add_argument("--print-command", action="store_true")
    options = parser.parse_args()
    app = options.app.resolve()
    study.identity(app)
    command = ["rtk", "proxy", str(app), *arguments(options.case, options.camera,
                                                   markers=options.markers, developed=options.developed)]
    print(shlex.join(command), flush=True)
    return 0 if options.print_command else subprocess.run(command, cwd=study.ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
