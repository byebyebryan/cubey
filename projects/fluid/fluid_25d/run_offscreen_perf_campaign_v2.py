#!/usr/bin/env python3
"""Sequential, resumable approved V2 campaign; never overlap GPU trials."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import signal
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent


def plan(study: Path):
    mountain = (
        ("1024-16m", study / "coverage/mountain-patch-1024-16m-live-0/static-recording/recording.json"),
        ("2048-8m", study / "release-mountain-patch-2048-8m/mountain-patch-2048-8m-live-0/static-recording/recording.json"),
    )
    rows = []
    for suffix, recording in mountain:
        case = "mountain-patch-" + suffix
        for host in ("offscreen", "wayland"):
            modes = ["renderer-only", "renderer-plus-worker", "live"]
            if host == "offscreen":
                modes.insert(0, "worker-only")
            rows.append((f"primary-{suffix}-{host}", case, host,
                         ["--modes", *modes, "--recording", str(recording), "--repeats", "3"]))
    for suffix, _ in mountain:
        for host in ("offscreen", "wayland"):
            rows.append((f"paced-{suffix}-{host}", "mountain-patch-" + suffix, host,
                         ["--render-fps", "60", "--repeats", "1"]))
    rows.append(("rolling-confirmation", "rolling-patch-1024-16m", "offscreen",
                 ["--render-fps", "60", "--repeats", "3"]))
    for width, height in ((2560, 1440), (3840, 2160)):
        rows.append((f"held-{height}p-offscreen", "mountain-patch-1024-16m", "offscreen",
                     ["--modes", "renderer-only", "--recording", str(mountain[0][1]),
                      "--width", str(width), "--height", str(height), "--repeats", "3"]))
    rows.append(("soak-600s-offscreen", "mountain-patch-1024-16m", "offscreen",
                 ["--render-fps", "60", "--wall", "600", "--repeats", "1"]))
    return rows


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--study", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    if not args.out.is_dir() or not args.study.is_dir():
        parser.error("use existing approved study and V2 output roots")
    campaign = []
    started = time.time()
    for name, case, host, extra in plan(args.study):
        destination = args.out / name
        command = [sys.executable, str(HERE / "run_offscreen_perf_v2.py"),
                   "--study", str(args.study), "--out", str(destination),
                   "--case", case, "--host", host, "--wall", "45", "--warmup", "5",
                   "--prewarm", "1800", *extra]
        row = {"name": name, "command": command, "started_unix_s": time.time()}
        if destination.exists():
            receipt = destination / "results.json"
            results = json.loads(receipt.read_text()) if receipt.exists() else {}
            if not results.get("trials") or any(t["status"] != "completed" for t in results["trials"]):
                raise RuntimeError(f"refuse to overwrite or skip incomplete campaign: {destination}")
            row.update(status="retained-completed", trials=len(results["trials"]))
        else:
            write(args.out / "campaign-progress.json", {"current": row, "completed": campaign})
            print("campaign start: " + name, flush=True)
            # Every nested trial owns and reaps only its own process groups.
            result = subprocess.Popen(command, start_new_session=True)
            try:
                result.wait(timeout=14400)
            except (subprocess.TimeoutExpired, KeyboardInterrupt):
                # Give the runner's finally blocks time to stop/reap its own
                # separately sessioned worker and viewer. Never SIGKILL the
                # coordinator and silently orphan the GPU workloads.
                result.send_signal(signal.SIGINT)
                result.wait(timeout=120)
                row["interrupted"] = True
            row.update(status="completed" if result.returncode == 0 else "failed",
                       exit_code=result.returncode, ended_unix_s=time.time())
        campaign.append(row)
        write(args.out / "campaign-progress.json", {"current": None, "completed": campaign})
        if row["status"] == "failed":
            return 1
    write(args.out / "campaign-completion.json", {"started_unix_s": started,
          "ended_unix_s": time.time(), "campaigns": campaign,
          "scope": "sequential GPU workloads; no native 4K window on 1440-high output"})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
