"""Synthetic backend-startup integration; not external numerical acceptance."""

from __future__ import annotations

import argparse
from contextlib import ExitStack
import json
from pathlib import Path
import subprocess
import tempfile

from convert_synxflow_recording_v1 import convert_case
from test_convert_synxflow_recording_v1 import _make_case


PREFIX = "fluid_25d_backend_startup_metadata: "


def run(target: Path, root: Path, name: str, flags: list[str]) -> dict | None:
    command = ["rtk", "proxy", str(target), "--headless", "--frames", "2",
               "--width", "96", "--height", "64", "--output",
               str(root / f"{name}.png"), *flags]
    result = subprocess.run(command, text=True, capture_output=True, timeout=30)
    (root / f"{name}.stdout").write_text(result.stdout)
    (root / f"{name}.stderr").write_text(result.stderr)
    print(json.dumps({"command": command, "exit_code": result.returncode}))
    combined = result.stdout + result.stderr
    if any(message in combined for message in (
        "no Vulkan physical devices found", "vkEnumeratePhysicalDevices",
        "no Vulkan device with required queues and dynamic rendering found",
    )):
        return None
    if result.returncode or "vulkan validation error" in combined.lower():
        raise RuntimeError(f"{name} failed: {combined}")
    if not (root / f"{name}.png").is_file():
        raise RuntimeError(f"{name} produced no capture")
    lines = [line[len(PREFIX):] for line in result.stdout.splitlines()
             if line.startswith(PREFIX)]
    if len(lines) != 1:
        raise RuntimeError(f"{name} did not emit exactly one startup descriptor")
    metadata = json.loads(lines[0])
    if metadata["schema"] != "cubey.fluid25d.backend-session.v1":
        raise RuntimeError("unsupported startup contract")
    if metadata["session"]["frame_sequence"] != 0 or metadata["session"]["lifecycle"] != "ready":
        raise RuntimeError("startup metadata falsely claims a completed runtime frame")
    return metadata


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--out", type=Path, help="optional fresh evidence directory")
    args = parser.parse_args()
    target = args.target.resolve(strict=True)
    with ExitStack() as stack:
        if args.out:
            root = args.out.absolute()
            if root.exists() or root.is_symlink():
                raise ValueError("evidence output must be a fresh directory")
            root.mkdir()
        else:
            root = Path(stack.enter_context(tempfile.TemporaryDirectory(
                prefix="cubey-backend-startup-")))
        grid = ["--grid-width", "8", "--grid-height", "8", "--fluid25d-scenario", "dry-bed"]
        descriptors = {}
        for name, flags in (
            ("vp", grid),
            ("vp-repeat", [*grid, "--fluid25d-backend", "builtin"]),
            ("fv", [*grid, "--fluid25d-backend", "builtin", "--fluid25d-solver", "finite-volume"]),
        ):
            metadata = run(target, root, name, flags)
            if metadata is None:
                return 77
            require(metadata["backend"]["kind"] == "builtin", "wrong built-in kind")
            require(metadata["fields"]["water_ledger"], "built-in ledger missing")
            require(not metadata["fields"]["tracer_depth_equivalent_m"], "dry bed advertises dye")
            require(not metadata["capabilities"]["solver"]["set_rain"], "generic rain command fabricated")
            require(not metadata["capabilities"]["solver"]["step"], "single-step adapter fabricated")
            require(not any(metadata["capabilities"]["playback"].values()), "built-in playback controls fabricated")
            require(metadata["fields"]["momentum_x_m2_per_s"] == (name == "fv"), "native momentum mislabeled")
            require(metadata["fields"]["face_discharge_m3_per_s"] == (name != "fv"), "face discharge mislabeled")
            descriptors[name] = metadata
        require(descriptors["vp"]["grid"] == descriptors["vp-repeat"]["grid"], "selector changes numerical identity")
        require(descriptors["vp"]["session"]["session_id"] != descriptors["vp-repeat"]["session"]["session_id"],
                "separate runs reuse a session")
        manifest = convert_case(_make_case(root), root / "recording")
        stream = json.loads(manifest.read_text())
        producer_id = "a" * 32
        stream.update(schema="cubey.fluid25d.stream.v1", session_id=producer_id, revision=1,
                      producer={"state": "completed", "pid": 123, "message": "synthetic completed prefix",
                                "native_running": False, "latest_native_time_s": 2.0})
        # A completed prefix retains its final case-result audit, unlike a
        # still-running prefix. Neither audit belongs to immutable physics.
        stream["provenance"]["source_sha256"]["pre_solver_audit"] = "3" * 64
        for frame in stream["frames"]:
            frame["published_unix_s"] = 1000.0
        stream_path = manifest.parent / "stream.json"
        stream_path.write_text(json.dumps(stream) + "\n")
        for name, path_flag in (("recording", "--fluid25d-recording"), ("external", "--fluid25d-stream")):
            metadata = run(target, root, name, ["--fluid25d-backend", name, path_flag,
                                              str(manifest if name == "recording" else stream_path),
                                              "--fluid25d-recording-time-seconds", "1",
                                              "--fluid25d-recording-gpu-validation"])
            if metadata is None:
                return 77
            require(metadata["backend"]["kind"] == ("recorded" if name == "recording" else "external"),
                    "wrong presentation backend kind")
            require(metadata["session"]["physical_time_s"] == 1.0, "startup playhead is mislabeled")
            require(metadata["session"]["session_id"] != producer_id, "viewer assumes producer ownership")
            require(not any(metadata["capabilities"]["solver"].values()), "viewer advertises solver controls")
            require(metadata["capabilities"]["playback"]["pause"], "playback capability missing")
            require(not metadata["fields"]["water_ledger"], "recording fabricates native ledger")
            require(metadata["fields"]["momentum_x_m2_per_s"], "stored momentum missing")
            descriptors[name] = metadata
            # Shared native presentation path: Readable Auto and explicit Off
            # agree for both saved recordings and external publication fixtures.
            policy_images = []
            for policy in ("auto", "off", "on"):
                policy_name = f"{name}-highlights-{policy}"
                policy_metadata = run(target, root, policy_name, [
                    "--fluid25d-backend", name, path_flag,
                    str(manifest if name == "recording" else stream_path),
                    "--fluid25d-recording-time-seconds", "1",
                    "--fluid25d-recording-gpu-validation", "--fluid25d-native-presentation", "readable",
                    "--fluid25d-native-surface-highlights", policy, "--fluid25d-motion-markers"])
                if policy_metadata is None:
                    return 77
                require(policy_metadata["grid"] == metadata["grid"], "surface cue alters backend grid")
                require(policy_metadata["fields"] == metadata["fields"], "surface cue alters field contract")
                policy_images.append((root / f"{policy_name}.png").read_bytes())
            require(policy_images[0] == policy_images[1], "shared viewer Readable Auto is not Off")
        require(descriptors["recording"]["grid"] == descriptors["external"]["grid"],
                "changing publication mode changes immutable input identity")
        (root / "result.json").write_text(json.dumps({"synthetic": True, "passed": True,
                                                      "descriptors": descriptors}, indent=2) + "\n")
        print("backend startup integration: PASS (synthetic; no external solver run)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
