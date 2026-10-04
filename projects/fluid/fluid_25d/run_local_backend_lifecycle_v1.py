#!/usr/bin/env python3
"""Opt-in GUI lifecycle checks for built-in Fluid 2.5D and saved recordings.

This exercises the real window/input path with an explicitly scoped Niri
window. It is runtime integration evidence, not human visual acceptance or a
hydraulic conservation test.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import time
import uuid


ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import run_live_view_acceptance_v1 as scoped_view
from convert_synxflow_recording_v1 import convert_case
from test_convert_synxflow_recording_v1 import _make_case


STARTUP_PREFIX = "fluid_25d_backend_startup_metadata: "
ACK_PREFIX = "fluid_25d_backend_ack: "
FRAME_PREFIX = "fluid_25d_backend_frame: "
RUNTIME_PREFIX = "fluid_25d_backend_runtime_metadata: "
CASE_TIMEOUT_S = 30.0
POLL_S = 0.05
PAUSE_OBSERVATION_S = 0.5


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n",
                    encoding="utf-8")


def rtk_output(arguments: list[str], timeout: float = 10.0) -> str:
    result = subprocess.run(["rtk", "proxy", *arguments], cwd=ROOT, text=True,
                            capture_output=True, timeout=timeout)
    if result.returncode != 0:
        raise RuntimeError(f"command failed ({result.returncode}): {arguments}: "
                           f"{result.stdout}{result.stderr}")
    return result.stdout.strip()


def tree_hashes(root: Path) -> dict[str, str]:
    return {path.relative_to(root).as_posix(): sha256(path)
            for path in sorted(root.rglob("*")) if path.is_file()}


def read_events(log_path: Path, prefix: str) -> list[tuple[int, dict]]:
    events = []
    if not log_path.exists():
        return events
    for index, line in enumerate(log_path.read_text(encoding="utf-8", errors="replace").splitlines()):
        if line.startswith(prefix):
            try:
                payload = json.loads(line[len(prefix):])
            except json.JSONDecodeError as error:
                raise RuntimeError(f"invalid JSON after {prefix!r} at log line {index + 1}") from error
            if not isinstance(payload, dict):
                raise RuntimeError(f"non-object JSON after {prefix!r} at log line {index + 1}")
            events.append((index, payload))
    return events


def all_events(log_path: Path) -> dict[str, list[tuple[int, dict]]]:
    return {"startup": read_events(log_path, STARTUP_PREFIX),
            "acks": read_events(log_path, ACK_PREFIX),
            "frames": read_events(log_path, FRAME_PREFIX),
            "runtime": read_events(log_path, RUNTIME_PREFIX)}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def wait_event(log_path: Path, process: subprocess.Popen, category: str,
               predicate, deadline: float, label: str) -> tuple[int, dict]:
    while time.monotonic() < deadline:
        event_set = all_events(log_path)
        for event in event_set[category]:
            if predicate(event[1]):
                return event
        if process.poll() is not None:
            raise RuntimeError(f"viewer exited {process.returncode} before {label}")
        time.sleep(POLL_S)
    raise RuntimeError(f"{label} was not observed within {CASE_TIMEOUT_S:.0f}s")


def exact_windows(title: str) -> list[dict]:
    return [window for window in json.loads(rtk_output(["niri", "msg", "--json", "windows"]))
            if window.get("title") == title]


def owned_window_id(title: str) -> str:
    # Reuse the established title-scoped helper, then independently verify its
    # result before focusing or emitting input.
    helper_id = scoped_view.owned_window_id(title)
    matches = exact_windows(title)
    require(len(matches) == 1, f"expected exactly one owned window titled {title!r}")
    window_id = str(matches[0]["id"])
    require(helper_id == window_id, "scoped helper and current Niri window list disagree")
    return window_id


def focus_owned_window(title: str) -> str:
    window_id = owned_window_id(title)
    focused = json.loads(rtk_output(["niri", "msg", "--json", "focused-window"]))
    if str(focused.get("id")) != window_id:
        rtk_output(["niri", "msg", "action", "focus-window", "--id", window_id])
        focused = json.loads(rtk_output(["niri", "msg", "--json", "focused-window"]))
    require(str(focused.get("id")) == window_id,
            "owned window is not focused; refusing to emit keyboard input")
    require(focused.get("title") == title, "focused window title changed before input")
    return window_id


def send_key(driver: Path, title: str, key: str) -> dict:
    window_id = focus_owned_window(title)
    command = ["rtk", "proxy", str(driver), "-P", key, "-s", "120", "-p", key]
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=5.0)
    record = {"key": key, "focused_window_id": window_id, "focused_title": title,
              "held_ms": 120, "command": command, "exit_code": result.returncode,
              "stdout": result.stdout, "stderr": result.stderr}
    if result.returncode != 0:
        raise RuntimeError(f"keyboard driver failed for {key}: {result.stderr or result.stdout}")
    return record


def wait_for_window(title: str, process: subprocess.Popen, deadline: float) -> str:
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"viewer exited {process.returncode} before its Niri window appeared")
        matches = exact_windows(title)
        if len(matches) > 1:
            raise RuntimeError(f"multiple windows share unique title {title!r}")
        if len(matches) == 1:
            window_id = scoped_view.owned_window_id(title)
            require(window_id == str(matches[0]["id"]), "window ownership helper mismatch")
            return str(matches[0]["id"])
        time.sleep(POLL_S)
    raise RuntimeError(f"owned viewer window {title!r} did not appear within {CASE_TIMEOUT_S:.0f}s")


def capture_owned(title: str, path: Path) -> dict:
    path.parent.mkdir(parents=True, exist_ok=True)
    success = scoped_view.scoped_capture(title, path)
    record = {"path": str(path), "captured": success,
              "sha256": sha256(path) if success and path.is_file() else None}
    return record


def close_normally(process: subprocess.Popen, title: str, deadline: float) -> int:
    focus_owned_window(title)
    scoped_view.close_owned_window(title)
    remaining = max(0.1, min(12.0, deadline - time.monotonic()))
    return process.wait(timeout=remaining)


def cleanup_owned_process(process: subprocess.Popen | None, title: str) -> None:
    if process is None or process.poll() is not None:
        return
    try:
        if len(exact_windows(title)) == 1:
            focus_owned_window(title)
            scoped_view.close_owned_window(title)
        try:
            process.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            # Last-resort cleanup is limited to this harness-created process
            # group, never a title wildcard or unrelated viewer.
            os.killpg(process.pid, signal.SIGINT)
            process.wait(timeout=5.0)
    except Exception:
        # Preserve the primary test failure; leave any cleanup issue in the
        # case result instead of risking a broader process target.
        pass


def assert_clean_log(log_path: Path) -> None:
    text = log_path.read_text(encoding="utf-8", errors="replace").lower()
    require("vulkan validation error" not in text, "Vulkan validation error appeared in viewer log")
    require("fatal error" not in text, "fatal error appeared in viewer log")


def launch_viewer(viewer: Path, title: str, args: list[str], log_path: Path) -> tuple[list[str], subprocess.Popen, object]:
    command = ["rtk", "proxy", "stdbuf", "-oL", "-eL", str(viewer), *args,
               "--title", title, "--frames", "0", "--width", "960", "--height", "640"]
    log_file = log_path.open("w", encoding="utf-8", buffering=1)
    process = subprocess.Popen(command, cwd=ROOT, stdout=log_file, stderr=subprocess.STDOUT,
                               start_new_session=True)
    return command, process, log_file


def run_builtin_case(out: Path, viewer: Path, driver: Path, solver: str) -> dict:
    case_dir = out / f"builtin-{solver}"
    case_dir.mkdir()
    title = f"Cubey local lifecycle {solver} {uuid.uuid4().hex[:10]}"
    log_path = case_dir / "viewer.log"
    command, process, log_file = launch_viewer(
        viewer, title,
        ["--grid-width", "8", "--grid-height", "8", "--fluid25d-scenario", "dry-bed",
         "--fluid25d-solver", solver], log_path)
    started = time.monotonic()
    deadline = started + CASE_TIMEOUT_S
    keys = []
    captures = []
    result = {"kind": "builtin", "solver": solver, "title": title, "command": command,
              "viewer_log": str(log_path)}
    try:
        window_id = wait_for_window(title, process, deadline)
        result["window_id"] = window_id
        _, startup = wait_event(log_path, process, "startup", lambda _: True, deadline,
                                "startup backend metadata")
        result["startup_metadata"] = startup
        require(startup["backend"]["kind"] == "builtin", "startup backend is not built-in")
        require(startup["session"]["lifecycle"] == "ready", "startup is not Ready")
        first_running = wait_event(
            log_path, process, "frames",
            lambda frame: frame.get("reset_generation") == 1 and
            frame.get("lifecycle") == "running" and frame.get("physical_time_s", 0.0) > 0.0,
            deadline, "positive Running frame")
        result["first_running_frame"] = first_running[1]

        previous_ack_id = max((ack.get("command_id", 0)
                               for _, ack in read_events(log_path, ACK_PREFIX)), default=0)
        keys.append(send_key(driver, title, "space"))
        pause_index, pause_ack = wait_event(
            log_path, process, "acks",
            lambda ack: ack.get("command_id", 0) > previous_ack_id and
            ack.get("reset_generation") == 1 and ack.get("state") == "applied" and
            ack.get("lifecycle") == "paused",
            deadline, "Pause acknowledgement")
        pause_time = float(pause_ack["application_time_s"])
        observation_end = time.monotonic() + PAUSE_OBSERVATION_S
        while time.monotonic() < observation_end:
            require(process.poll() is None, "viewer exited during pause observation")
            paused_frames = [(index, frame) for index, frame in read_events(log_path, FRAME_PREFIX)
                             if index > pause_index and frame.get("reset_generation") == 1]
            require(all(abs(float(frame["physical_time_s"]) - pause_time) <= 1e-9
                        for _, frame in paused_frames),
                    "native physical time advanced after completed-boundary Pause acknowledgement")
            time.sleep(POLL_S)
        paused_frames = [(index, frame) for index, frame in read_events(log_path, FRAME_PREFIX)
                         if index > pause_index and frame.get("reset_generation") == 1]
        require(len(paused_frames) >= 2, "did not observe repeated paused boundary headers")
        result["pause_ack"] = pause_ack
        result["paused_boundary_headers"] = [frame for _, frame in paused_frames]
        captures.append(capture_owned(title, case_dir / "paused.png"))

        pause_command_id = int(pause_ack["command_id"])
        keys.append(send_key(driver, title, "space"))
        resume_index, resume_ack = wait_event(
            log_path, process, "acks",
            lambda ack: ack.get("command_id", 0) > pause_command_id and
            ack.get("reset_generation") == 1 and ack.get("state") == "applied" and
            ack.get("lifecycle") == "running",
            deadline, "Resume acknowledgement")
        resumed = wait_event(
            log_path, process, "frames",
            lambda frame: frame.get("reset_generation") == 1 and
            frame.get("lifecycle") == "running" and
            frame.get("physical_time_s", 0.0) > pause_time + 1e-9,
            deadline, "physical advancement after Resume")
        result["resume_ack"] = resume_ack
        result["resumed_frame"] = resumed[1]

        resume_command_id = int(resume_ack["command_id"])
        keys.append(send_key(driver, title, "r"))
        reset_index, reset_ack = wait_event(
            log_path, process, "acks",
            lambda ack: ack.get("command_id", 0) > resume_command_id and
            ack.get("reset_generation") == 2 and ack.get("state") == "applied" and
            ack.get("lifecycle") == "ready" and
            abs(float(ack.get("application_time_s", -1.0))) <= 1e-12,
            deadline, "generation-2 Reset acknowledgement at physical time zero")
        after_reset = wait_event(
            log_path, process, "frames",
            lambda frame: frame.get("reset_generation") == 2 and
            float(frame.get("physical_time_s", 0.0)) > 0.0,
            deadline, "generation-2 positive Running frame")
        require(reset_index < after_reset[0], "Reset acknowledgement did not precede new-generation advancement")
        result["reset_ack"] = reset_ack
        result["first_positive_generation_2_frame"] = after_reset[1]
        captures.append(capture_owned(title, case_dir / "reset.png"))

        result["close_return_code"] = close_normally(process, title, deadline)
        require(result["close_return_code"] == 0, "normal owned-window close did not return code 0")
        event_set = all_events(log_path)
        runtime = event_set["runtime"][-1][1] if event_set["runtime"] else None
        require(runtime is not None, "built-in shutdown emitted no final runtime metadata")
        require(runtime["session"]["lifecycle"] == "completed" and
                runtime["session"]["reset_generation"] == 2,
                "built-in shutdown did not finish Completed in generation 2")
        result["final_runtime_metadata"] = runtime
        result["keys"] = keys
        result["captures"] = captures
        assert_clean_log(log_path)
        result["status"] = "passed"
        write_json(case_dir / "result.json", result)
        return result
    except Exception as error:
        result.update(status="failed", error=f"{type(error).__name__}: {error}", keys=keys,
                      captures=captures)
        result["log_tail"] = log_path.read_text(encoding="utf-8", errors="replace")[-12000:]
        write_json(case_dir / "result.json", result)
        raise
    finally:
        cleanup_owned_process(process, title)
        log_file.close()


def run_recording_case(out: Path, viewer: Path, driver: Path, manifest: Path,
                       immutable_root: Path) -> dict:
    case_dir = out / "recording"
    case_dir.mkdir()
    title = f"Cubey local lifecycle recording {uuid.uuid4().hex[:10]}"
    log_path = case_dir / "viewer.log"
    command, process, log_file = launch_viewer(
        viewer, title,
        ["--fluid25d-recording", str(manifest), "--fluid25d-recording-time-seconds", "0",
         "--fluid25d-recording-speed", "1"], log_path)
    deadline = time.monotonic() + CASE_TIMEOUT_S
    keys = []
    captures = []
    before = tree_hashes(immutable_root)
    result = {"kind": "recording_playback", "title": title, "command": command,
              "viewer_log": str(log_path), "recording_manifest": str(manifest),
              "recording_start_playhead_s": 0.0, "recording_speed": 1.0,
              "immutable_tree_before": before}
    try:
        result["window_id"] = wait_for_window(title, process, deadline)
        _, startup = wait_event(log_path, process, "startup", lambda _: True, deadline,
                                "recording startup metadata")
        result["startup_metadata"] = startup
        require(startup["backend"]["kind"] == "recorded", "recording startup backend is not Recorded")
        require(startup["session"]["physical_time_s"] == 0.0, "recording did not start at viewer time zero")
        completed_index, completed = wait_event(
            log_path, process, "frames",
            lambda frame: frame.get("reset_generation") == 1 and
            frame.get("lifecycle") == "completed" and
            abs(float(frame.get("physical_time_s", -1.0)) - 2.0) <= 1e-9,
            deadline, "Completed recording viewer playhead at saved time 2")
        result["completed_viewer_frame"] = completed
        result["completed_frame_log_index"] = completed_index

        previous_ack_id = max((ack.get("command_id", 0)
                               for _, ack in read_events(log_path, ACK_PREFIX)), default=0)
        keys.append(send_key(driver, title, "r"))
        reset_index, reset_ack = wait_event(
            log_path, process, "acks",
            lambda ack: ack.get("command_id", 0) > previous_ack_id and
            ack.get("reset_generation") == 2 and ack.get("state") == "applied" and
            ack.get("lifecycle") == "ready" and
            abs(float(ack.get("application_time_s", -1.0))) <= 1e-12,
            deadline, "generation-2 recording Reset acknowledgement at viewer time zero")
        zero_frame = wait_event(
            log_path, process, "frames",
            lambda frame: frame.get("reset_generation") == 2 and
            frame.get("lifecycle") == "paused" and
            abs(float(frame.get("physical_time_s", -1.0))) <= 1e-12,
            deadline, "generation-2 paused viewer header at playhead zero")
        require(reset_index < zero_frame[0], "recording Reset header preceded its acknowledgement")
        result["reset_ack"] = reset_ack
        result["reset_zero_playhead_frame"] = zero_frame[1]
        result["reset_semantics"] = (
            "logical viewer playhead; source frame index zero is synchronously selected before "
            "the generation barrier is acknowledged; not native producer time")
        captures.append(capture_owned(title, case_dir / "reset-saved-state-zero.png"))

        stable_end = time.monotonic() + PAUSE_OBSERVATION_S
        while time.monotonic() < stable_end:
            require(process.poll() is None, "recording viewer exited during reset-zero observation")
            gen2_frames = [(index, frame) for index, frame in read_events(log_path, FRAME_PREFIX)
                           if index >= zero_frame[0] and frame.get("reset_generation") == 2]
            require(all(abs(float(frame["physical_time_s"])) <= 1e-12 for _, frame in gen2_frames),
                    "recording playhead advanced before Reset-to-zero remained paused")
            time.sleep(POLL_S)
        result["reset_zero_headers"] = [frame for _, frame in gen2_frames]

        result["close_return_code"] = close_normally(process, title, deadline)
        require(result["close_return_code"] == 0, "normal recording-window close did not return code 0")
        after = tree_hashes(immutable_root)
        require(before == after, "recording fixture bytes changed while the viewer was open")
        result["immutable_tree_after"] = after
        result["fixture_unchanged"] = True
        result["keys"] = keys
        result["captures"] = captures
        assert_clean_log(log_path)
        result["status"] = "passed"
        write_json(case_dir / "result.json", result)
        return result
    except Exception as error:
        result.update(status="failed", error=f"{type(error).__name__}: {error}", keys=keys,
                      captures=captures)
        result["log_tail"] = log_path.read_text(encoding="utf-8", errors="replace")[-12000:]
        write_json(case_dir / "result.json", result)
        raise
    finally:
        cleanup_owned_process(process, title)
        log_file.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--viewer", type=Path, required=True)
    parser.add_argument("--keyboard-driver", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True,
                        help="fresh evidence directory; refuses an existing path")
    args = parser.parse_args()
    source_viewer = args.viewer.resolve(strict=True)
    driver = args.keyboard_driver.resolve(strict=True)
    require(source_viewer.is_file() and os.access(source_viewer, os.X_OK), "viewer must be executable")
    require(driver.is_file() and os.access(driver, os.X_OK), "keyboard driver must be executable")
    out_leaf = args.out.absolute()
    require(out_leaf.parent.is_dir(), "evidence output parent must already exist")
    require(not out_leaf.exists() and not out_leaf.is_symlink(), "evidence output must be fresh")
    out = out_leaf.resolve()
    out.mkdir(parents=False)
    frozen_dir = out / "frozen"
    frozen_dir.mkdir()
    viewer = frozen_dir / "fluid-25d"
    shutil.copy2(source_viewer, viewer)
    require(sha256(viewer) == sha256(source_viewer), "frozen viewer copy hash mismatch")

    provenance = {
        "schema": "cubey.fluid25d.local-gui-lifecycle.v1",
        "created_unix_s": time.time(),
        "host": platform.node(),
        "git_head": rtk_output(["git", "rev-parse", "HEAD"]),
        "git_worktree_status": rtk_output(["git", "status", "--short"]),
        "runner": str(Path(__file__).resolve()),
        "runner_sha256": sha256(Path(__file__).resolve()),
        "scoped_helper": str(HERE / "run_live_view_acceptance_v1.py"),
        "scoped_helper_sha256": sha256(HERE / "run_live_view_acceptance_v1.py"),
        "fixture_builder_sha256": sha256(HERE / "test_convert_synxflow_recording_v1.py"),
        "converter_sha256": sha256(HERE / "convert_synxflow_recording_v1.py"),
        "source_viewer": str(source_viewer),
        "source_viewer_sha256": sha256(source_viewer),
        "frozen_viewer": str(viewer),
        "frozen_viewer_sha256": sha256(viewer),
        "keyboard_driver": str(driver),
        "keyboard_driver_sha256": sha256(driver),
        "gpu_identity": rtk_output(["nvidia-smi", "--query-gpu=name,uuid,driver_version,memory.total",
                                     "--format=csv,noheader"]),
        "niri_version": rtk_output(["niri", "msg", "version"]),
        "test_cases": ["builtin-virtual-pipes-space-pause-resume-r-reset",
                       "builtin-finite-volume-space-pause-resume-r-reset",
                       "recording-completed-r-reset-to-viewer-time-zero"],
        "window": {"frames": 0, "width": 960, "height": 640,
                   "title_scope": "unique exact Niri title; focus id verified before each key"},
        "keyboard": "private wtype-evdev key press, 120ms hold, key release",
        "case_timeout_s": CASE_TIMEOUT_S,
        "limits": "no external worker, no solver promotion, no numerical/conservation claim, "
                  "no human GUI acceptance",
    }
    write_json(out / "provenance.json", provenance)

    results = {"schema": "cubey.fluid25d.local-gui-lifecycle.result.v1",
               "provenance": "provenance.json", "cases": []}
    try:
        for solver in ("virtual-pipes", "finite-volume"):
            result = run_builtin_case(out, viewer, driver, solver)
            results["cases"].append(result)
            write_json(out / "result.json", {**results, "status": "running"})

        fixture_root = out / "recording-fixture"
        fixture_root.mkdir()
        case = _make_case(fixture_root)
        manifest = convert_case(case, fixture_root / "recording")
        results["synthetic_recording"] = {"case_root": str(case),
                                          "manifest": str(manifest),
                                          "fixture_tree_sha256_before": tree_hashes(fixture_root)}
        result = run_recording_case(out, viewer, driver, manifest, fixture_root)
        results["cases"].append(result)
        results["status"] = "passed"
        results["human_gui_acceptance"] = "not assessed; scoped screenshots are automation evidence only"
        write_json(out / "result.json", results)
        print(json.dumps({"status": "passed", "case_count": len(results["cases"]),
                          "out": str(out)}, sort_keys=True), flush=True)
        return 0
    except Exception as error:
        results.update(status="failed", error=f"{type(error).__name__}: {error}",
                       human_gui_acceptance="not assessed")
        write_json(out / "result.json", results)
        print(json.dumps({"status": "failed", "error": str(error), "out": str(out)},
                         sort_keys=True), flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
