#!/usr/bin/env python3
"""One explicit mountain-rain entry point: replay, optional live worker, or banks.

This MIT launcher never imports the GPL worker/extension, installs dependencies,
builds CUDA, bakes masks, or substitutes another producer. Live ownership stays
in the foreground: viewer close detaches; Ctrl-C stops only owned process groups.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import signal
import subprocess
import tempfile
import time

import run_native_presentation_v1 as ref

HERE = Path(__file__).resolve().parent
LIVE_PYTHON = ref.OUTPUT_ROOT / "native-runoff-reuse-v1-20261002-trm3Ve/synxflow-setup/env/.venv/bin/python"
EXTENSION = HERE / "external_synxflow/private-builds/service-cuda117-gcc114-zero-dt-20261004/build/synxflow/apps/cudaFloodSolversPybind/flood.cpython-311-x86_64-linux-gnu.so"
EXTENSION_SHA = "251cea1bd0658acdfc5b5129038d93970bf1769d7cac00bbf1edff99a886315f"
CASE = ref.INPUT_ROOT / "cases/mountain-rain-on-14400s"
CASE_INPUT_SHA = "de6da45d40600460e90abc2b153cec859a0ac45cd5b2b4a7b5b7595f6d3ec05b"
MASK_ROOT = ref.OUTPUT_ROOT / "bank-refinement-v1-20261006-ZmPvk9/bake-motion-moderate"
MASK_SHA = {"rain-on": "a9c66a1f01886e18d955a7b3d94a860702fb06e3ec771f105a0b0ac33fc2fb36",
            "rain-off": "2ee7ad89cf86fab68753cbe30fb4bfc4745132e47b1e8ee84880056052272aa4"}


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=("replay", "live", "banks"))
    p.add_argument("--app", type=Path, default=ref.APP)
    p.add_argument("--out", type=Path, help="fresh launcher leaf under outputs/fluid; otherwise allocated automatically")
    p.add_argument("--camera", choices=ref.CAMERAS)
    p.add_argument("--rain-case", choices=ref.CASES, help="replay/banks only; default rain-off")
    p.add_argument("--speed", type=float, help="replay default 150x, live/banks default 60x")
    p.add_argument("--start", type=float, help="replay saved time; banks defaults to its baked start")
    p.add_argument("--bank", choices=("reference", "bspline-2x", "marching-squares"), default="reference")
    p.add_argument("--style", choices=("readable", "scenic"), default="readable",
                   help="Scenic is opt-in HDR terrain/water; diagnostic views retain Readable shading")
    p.add_argument("--material", choices=("v1", "refined", "terrain", "macro"), default="refined",
                   help="Scenic preset; terrain is an opt-in terrain-only material study")
    p.add_argument("--loop", action="store_true", help="banks only: explicit recorded-window looping")
    p.add_argument("--no-dots", action="store_true")
    p.add_argument("--profile", action="store_true", help="Record viewer GPU/bridge metrics inside the fresh launcher output")
    p.add_argument("--width", type=int, default=1440)
    p.add_argument("--height", type=int, default=900)
    p.add_argument("--native-python", type=Path, default=LIVE_PYTHON)
    p.add_argument("--extension", type=Path, default=EXTENSION)
    p.add_argument("--startup-timeout", type=float, default=30)
    p.add_argument("--print-command", action="store_true", help="full read-only preflight and command preview; no output/process creation")
    return p


def validate_options(a) -> None:
    if a.material != "refined" and a.style != "scenic":
        raise ValueError("nondefault material requires Scenic")
    speed = a.speed if a.speed is not None else (150 if a.mode == "replay" else 60)
    if not math.isfinite(speed) or not .125 <= speed <= 300:
        raise ValueError("speed must be finite and in [0.125,300]")
    if a.start is not None and (not math.isfinite(a.start) or a.start < 0 or a.start > 14400 or a.start % 60):
        raise ValueError("start must be a saved 60-second time in [0,14400]")
    if not 320 <= a.width <= 3840 or not 240 <= a.height <= 2160:
        raise ValueError("window dimensions outside the bounded demo range")
    if not math.isfinite(a.startup_timeout) or not 1 <= a.startup_timeout <= 120:
        raise ValueError("startup timeout must be in [1,120] seconds")
    if a.mode != "banks" and (a.loop or a.bank == "marching-squares"):
        raise ValueError("loop and marching-squares require the bounded banks recording mode")
    if a.mode == "live" and (a.start is not None or a.rain_case is not None):
        raise ValueError("live starts dry/paused with uniform 120 mm/h; use GUI rain controls, not recorded-case/start options")
    if a.mode == "banks" and a.start is not None:
        lo, hi = (4800, 5340) if a.rain_case == "rain-on" else (7260, 7800)
        if not lo <= a.start <= hi:
            raise ValueError("bank start lies outside its baked saved-state window")


def input_tree(directory: Path) -> str:
    if not directory.is_dir() or directory.is_symlink():
        raise ValueError(f"missing/non-directory/symlink native input: {directory}")
    rows = {}
    for p in sorted(directory.rglob("*")):
        if p.is_symlink():
            raise ValueError("native input may not contain symlinks")
        if p.is_file():
            rows[str(p.relative_to(directory))] = ref.sha256_file(p)
    return hashlib.sha256(json.dumps(rows, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def fresh_output(path: Path) -> Path:
    absolute = path.absolute()
    root = ref.OUTPUT_ROOT.resolve()
    if root not in absolute.parents or absolute == root:
        raise ValueError("output must be a fresh leaf under outputs/fluid")
    if any(p.is_symlink() for p in (absolute, *absolute.parents)):
        raise ValueError("launcher output may not traverse symlinks")
    resolved = absolute.resolve()
    if root not in resolved.parents or resolved.exists() or not resolved.parent.is_dir():
        raise ValueError("output must be a fresh leaf below an existing parent")
    return resolved


def preflight(a) -> dict:
    validate_options(a)
    app = a.app.absolute()
    if not app.is_file() or app.is_symlink() or not os.access(app, os.X_OK):
        raise ValueError(f"missing executable: {app}; build the dev app explicitly first")
    if a.out is not None:
        fresh_output(a.out)
    result = {"app": str(app), "app_sha256": ref.sha256_file(app), "compiled_shaders": ref.shader_identity()}
    if a.mode == "live":
        if not a.native_python.is_file() or not os.access(a.native_python, os.X_OK):
            raise ValueError("optional native Python environment missing; no install/build/fallback attempted")
        if not a.extension.is_file() or a.extension.is_symlink() or ref.sha256_file(a.extension) != EXTENSION_SHA:
            raise ValueError("optional audited extension missing or hash mismatch; no build/fallback attempted")
        if input_tree(CASE / "native/input") != CASE_INPUT_SHA:
            raise ValueError("immutable live case input hash changed")
        result.update(extension_sha256=EXTENSION_SHA, case_input_sha256=CASE_INPUT_SHA,
                      native_python=str(a.native_python.absolute()), worker_sha256=ref.sha256_file(HERE / "external_synxflow/session_worker.py"))
    else:
        result["recordings"] = ref.frozen_input_identity()
        if a.mode == "banks":
            case = a.rain_case or "rain-off"
            mask = MASK_ROOT / (case + "-moderate") / "coverage.json"
            if not mask.is_file() or mask.is_symlink() or ref.sha256_file(mask) != MASK_SHA[case]:
                raise ValueError("retained bank sidecar missing or hash mismatch; no baking/fallback attempted")
            result["mask_manifest_sha256"] = MASK_SHA[case]
    return result


def commands(a, out: Path) -> dict[str, list[str]]:
    validate_options(a)
    speed = a.speed if a.speed is not None else (150 if a.mode == "replay" else 60)
    camera = a.camera or ("collection" if a.mode == "banks" else "overview")
    viewer = ["rtk", "proxy", str(a.app.absolute()), "--width", str(a.width), "--height", str(a.height),
              "--frames", "0", "--title", f"Cubey Mountain Rain - {a.mode}",
              "--fluid25d-recording-camera", camera, "--fluid25d-native-presentation", a.style,
              "--fluid25d-native-surface-highlights", "off", "--fluid25d-native-bank-view", a.bank]
    if a.style == "scenic":
        viewer += ["--fluid25d-scenic-material", a.material]
    if not a.no_dots:
        viewer.append("--fluid25d-motion-markers")
    if a.profile:
        viewer += ["--profile-output", str(out / "viewer-profile"), "--profile-warmup-frames", "12"]
    if a.mode == "live":
        session = out / "session"
        viewer += ["--fluid25d-backend", "external", "--fluid25d-external-session", str(session)]
        worker = ["rtk", "proxy", str(a.native_python.absolute()), str(HERE / "external_synxflow/session_worker.py"),
                  "--extension", str(a.extension.absolute()), "--case", str(CASE), "--out", str(session),
                  "--speed", str(speed), "--publish-interval", ".1", "--paused"]
        return {"worker": worker, "viewer": viewer}
    case = a.rain_case or "rain-off"
    start = a.start if a.start is not None else (0 if a.mode == "replay" else (4800 if case == "rain-on" else 7260))
    viewer += ["--fluid25d-backend", "recording", "--fluid25d-recording", str(ref.INPUT_ROOT / "recordings" / case / "recording.json"),
               "--fluid25d-recording-time-seconds", str(start), "--fluid25d-recording-speed", str(speed)]
    if a.mode == "banks":
        viewer += ["--fluid25d-bank-comparison", "--fluid25d-native-display-coverage",
                   str(MASK_ROOT / (case + "-moderate") / "coverage.json")]
        if a.loop:
            viewer.append("--fluid25d-bank-comparison-loop")
    return {"viewer": viewer}


def active_owned_group(group_id: int) -> list[int]:
    """Linux: exclude zombies, require both the owned process group and session."""
    active = []
    for p in Path("/proc").iterdir():
        if not p.name.isdecimal():
            continue
        try:
            stat = (p / "stat").read_text().rsplit(")", 1)[1].split()
            if stat[0] != "Z" and int(stat[2]) == group_id and int(stat[3]) == group_id:
                active.append(int(p.name))
        except (OSError, ValueError, IndexError):
            continue
    return active


def stop_owned(process: subprocess.Popen, grace_s: float = 15) -> dict:
    """Signal only the new session/group created for this child, then reap it.

    rtk proxy can keep a wrapper parent; group targeting includes that wrapper's
    worker/viewer and never relies on machine-wide executable-name matching.
    """
    active = active_owned_group(process.pid)
    sent = bool(active)
    if active:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    escalated = False
    deadline = time.monotonic() + grace_s
    while active_owned_group(process.pid) and time.monotonic() < deadline:
        process.poll()
        time.sleep(.05)
    if active_owned_group(process.pid):
        os.killpg(process.pid, signal.SIGKILL)
        escalated = True
    process.wait(timeout=5)
    deadline = time.monotonic() + 5
    while active_owned_group(process.pid) and time.monotonic() < deadline:
        time.sleep(.05)
    if active_owned_group(process.pid):
        raise RuntimeError("owned process group did not stop")
    return {"pid": process.pid, "exit_code": process.returncode, "signal": "SIGTERM" if sent else None,
            "escalated": escalated, "active_group_remaining": []}


def wait_ready(worker, session: Path, timeout_s: float, interrupted=lambda: False) -> dict:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if interrupted():
            raise KeyboardInterrupt
        if worker.poll() is not None:
            raise RuntimeError("native worker exited before readiness; inspect worker.log")
        path = session / "state.json"
        if path.is_file():
            state = json.loads(path.read_bytes())
            frame = state.get("frame", {})
            if frame.get("lifecycle") == "failed":
                raise RuntimeError("native startup failed: " + frame.get("failure_message", ""))
            if (state.get("schema") == "cubey.fluid25d.external-state.v1" and
                    frame.get("lifecycle") == "paused" and frame.get("reset_generation") == 1 and
                    frame.get("physical_time_s") == 0 and state.get("pacing") > 0 and
                    time.time() - state.get("published_unix_s", 0) < 3):
                return state
        time.sleep(.05)
    raise TimeoutError("native initialization deadline; no viewer/fallback launched; inspect worker.log")


def launch(a, out: Path, identity: dict) -> int:
    argv = commands(a, out)
    ref.write_json_exclusive(out / "launch.json", {"schema": "cubey.fluid25d.mountain-demo.v1", "mode": a.mode,
        "commands": argv, "preflight": identity, "preset": "readable/reference/dots-on/highlights-off unless explicitly overridden",
        "ownership": "foreground launcher; viewer close detaches; Ctrl-C stops owned groups only"})
    owned, cleanup = [], []
    interrupted = [False]
    prior = {sig: signal.getsignal(sig) for sig in (signal.SIGINT, signal.SIGTERM)}
    for sig in prior:
        signal.signal(sig, lambda *_: interrupted.__setitem__(0, True))
    code, failure, detached = 0, "", False
    try:
        with (out / "worker.log").open("xb") as worker_log, (out / "viewer.log").open("xb") as viewer_log:
            worker = None
            if "worker" in argv:
                worker = subprocess.Popen(argv["worker"], cwd=out, stdout=worker_log, stderr=subprocess.STDOUT, start_new_session=True)
                owned.append(worker)
                ready = wait_ready(worker, out / "session", a.startup_timeout, lambda: interrupted[0])
                ref.write_json_exclusive(out / "ready.json", ready)
                print("LIVE ready, paused at dry start. Space resumes the solver; rain controls are in the GUI.", flush=True)
            viewer = subprocess.Popen(argv["viewer"], cwd=out, stdout=viewer_log, stderr=subprocess.STDOUT, start_new_session=True)
            owned.append(viewer)
            while not interrupted[0]:
                if worker is not None and worker.poll() is not None:
                    if worker.returncode:
                        raise RuntimeError("native worker failed; inspect worker.log/session/completion.json")
                    completion = json.loads((out / "session/completion.json").read_bytes())
                    if completion.get("lifecycle") != "stopped":
                        raise RuntimeError("continuous worker exited without explicit stopped completion")
                    break
                if viewer.poll() is not None:
                    if viewer.returncode:
                        raise RuntimeError("viewer failed; inspect viewer.log (no producer fallback)")
                    if worker is None:
                        break
                    if not detached:
                        print("Viewer detached. Worker remains owned here. Reattach with the printed viewer command; Ctrl-C stops it.", flush=True)
                        ref.write_json_exclusive(out / "detached.json", {"worker_pid": worker.pid, "worker_running": True})
                        detached = True
                time.sleep(.05)
            if interrupted[0]:
                code = 130
    except KeyboardInterrupt:
        code = 130
    except Exception as error:
        failure, code = str(error), 1
    finally:
        for process in reversed(owned):
            try:
                cleanup.append(stop_owned(process))
            except Exception as error:
                failure, code = failure or str(error), 1
        # The proxy wrapper may reap before its signalled child finishes writing.
        completion = None
        if "worker" in argv and (out / "session").exists():
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline and not (out / "session/completion.json").is_file():
                time.sleep(.05)
            path = out / "session/completion.json"
            if path.is_file():
                completion = json.loads(path.read_bytes())
            if (not completion or completion.get("lifecycle") != "stopped" or
                    not completion.get("inputs_unchanged") or not completion.get("reference_inputs_unchanged")):
                failure, code = failure or "native shutdown did not retain a clean stopped/input-unchanged receipt", 1
        for sig, handler in prior.items():
            signal.signal(sig, handler)
        ref.write_json_exclusive(out / "result.json", {"exit_code": code, "failure": failure, "viewer_detached": detached,
            "owned_cleanup": cleanup, "completion": completion, "human_visual_acceptance": "deferred"})
    if failure:
        print("Mountain Rain FAILED: " + failure, flush=True)
    return code


def main(argv=None) -> int:
    a = parser().parse_args(argv)
    try:
        identity = preflight(a)
        preview = fresh_output(a.out) if a.out is not None else ref.OUTPUT_ROOT / "mountain-rain-launch-FRESH"
        plan = commands(a, preview)
        print(f"{a.mode.upper()} | {a.style} | {a.bank} | dots {'off' if a.no_dots else 'on'} | no numerical-input changes", flush=True)
        for name, command in plan.items():
            print(name + ": " + shlex.join(command), flush=True)
        if a.print_command:
            print("Preflight PASS; preview is read-only. FRESH is allocated only on launch.", flush=True)
            return 0
        if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
            raise ValueError("no GUI display; use --print-command remotely or the headless review runner")
        if a.out is None:
            out = Path(tempfile.mkdtemp(prefix="mountain-rain-launch-", dir=ref.OUTPUT_ROOT))
        else:
            out = fresh_output(a.out)
            out.mkdir()
        print("Owned output: " + str(out), flush=True)
        if a.out is None:
            for name, command in commands(a, out).items():
                print(name + ": " + shlex.join(command), flush=True)
        return launch(a, out, identity)
    except (ValueError, FileNotFoundError, PermissionError) as error:
        print("Mountain Rain preflight FAILED: " + str(error), flush=True)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
