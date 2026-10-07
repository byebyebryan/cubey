"""Display sidecar ingestion/immutability checks, not solver accuracy tests."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

from convert_synxflow_recording_v1 import convert_case
from test_convert_synxflow_recording_v1 import _make_case, _write_asc


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def make_sidecar(root, recording, values=(0., 1., 0.)):
    root.mkdir()
    source = json.loads(recording.read_text())
    doc = {"schema": "cubey.fluid25d.display_coverage.v1", "encoding": "float32-little-endian",
           "source_manifest_sha256": sha(recording), "grid": source["grid"],
           "subdivision": 4, "variant": "synthetic-control", "temporal_interpolation": False, "frames": []}
    for row, value in zip(source["frames"], values, strict=True):
        path = root / f"{int(row['time_s'])}.f32"
        path.write_bytes(struct.pack("<25f", *([value]*25)))
        doc["frames"].append({"time_s": row["time_s"], "path": path.name, "sha256": sha(path),
                              "source_frame_sha256": row["sha256"], "bytes": 100})
    path = root / "coverage.json"
    path.write_text(json.dumps(doc))
    return path, doc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--video", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cubey-coverage-gpu-") as temporary:
        root = Path(temporary)
        case = _make_case(root)
        # The generic fixture starts dry. Use positive carrier depth here so
        # a zero mask genuinely exercises Composite opacity, not a dry discard.
        _write_asc(case / "native/output/h_0.asc", [.08] * 4)
        recording = convert_case(case, root / "recording")
        coverage, document = make_sidecar(root / "masks", recording)
        base = ["rtk", "proxy", str(args.target.resolve()), "--headless", "--width", "128", "--height", "96",
                "--fluid25d-recording", str(recording), "--fluid25d-native-presentation", "readable",
                "--fluid25d-native-surface-highlights", "off", "--fluid25d-recording-gpu-validation"]

        def run(label, extra, success=True):
            output = root / (label + ".png")
            command = base + ([] if "--output" in extra else ["--output", str(output)]) + extra
            result = subprocess.run(command, text=True, capture_output=True, timeout=40)
            print(json.dumps({"test": label, "exit_code": result.returncode, "command": command}))
            text = result.stdout + result.stderr
            if success and any(s in text for s in ("no Vulkan physical devices found", "vkEnumeratePhysicalDevices",
                    "no Vulkan device with required queues and dynamic rendering found")):
                raise RuntimeError("SKIP_NO_VULKAN")
            if "vulkan validation error" in text.lower():
                raise AssertionError("Vulkan validation error")
            if success:
                assert result.returncode == 0 and "fluid_25d_recording_upload: PASS" in text, text
                assert "hydraulic_dispatches=0" in text
            else:
                assert result.returncode != 0 and "display coverage" in text.lower(), text
            return output, text

        for diagnostic in ("shaded", "contours", "wet-mask", "unlit"):
            before, _ = run("raw-" + diagnostic, ["--fluid25d-native-water-debug", diagnostic])
            after, _ = run("masked-" + diagnostic, ["--fluid25d-native-water-debug", diagnostic,
                "--fluid25d-native-display-coverage", str(coverage)])
            if diagnostic == "shaded":
                assert sha(before) != sha(after), "zero coverage did not change Composite rendering"
            else:
                assert sha(before) == sha(after), "display mask changed native diagnostic truth"
        again, _ = run("masked-repeat", ["--fluid25d-native-display-coverage", str(coverage)])
        assert sha(again) == sha(root / "masked-shaded.png"), "static mask render is not deterministic"
        run("one-mask", ["--fluid25d-native-display-coverage", str(coverage),
                         "--fluid25d-recording-time-seconds", "1"])
        for label, change in (
            ("wrong-recording", lambda d: d.update(source_manifest_sha256="0"*64)),
            ("bad-source-frame", lambda d: d["frames"][0].update(source_frame_sha256="0"*64)),
            ("bad-mask-hash", lambda d: d["frames"][0].update(sha256="0"*64)),
            ("bad-byte-count", lambda d: d["frames"][0].update(bytes=96)),
            ("unsafe-path", lambda d: d["frames"][0].update(path="../escape.f32")),
            ("bad-subdivision", lambda d: d.update(subdivision=3)),
            ("fractional-subdivision", lambda d: d.update(subdivision=2.5)),
            ("interpolation", lambda d: d.update(temporal_interpolation=True)),
            ("duplicate-time", lambda d: d["frames"][1].update(time_s=0)),
            ("missing-time", lambda d: d["frames"].pop(0)),
        ):
            bad = copy.deepcopy(document)
            change(bad)
            path = coverage.parent / (label + ".json")
            path.write_text(json.dumps(bad))
            run(label, ["--fluid25d-native-display-coverage", str(path)], success=False)
        for label, value in (("nan-mask", float("nan")), ("negative-mask", -.1), ("over-mask", 1.1)):
            bad = copy.deepcopy(document)
            payload = coverage.parent / (label + ".f32")
            payload.write_bytes(struct.pack("<25f", *([value]*25)))
            bad["frames"][0].update(path=payload.name, sha256=sha(payload))
            path = coverage.parent / (label + ".json")
            path.write_text(json.dumps(bad))
            run(label, ["--fluid25d-native-display-coverage", str(path)], success=False)
        link = coverage.parent / "link.f32"
        link.symlink_to(coverage.parent / "0.f32")
        bad = copy.deepcopy(document)
        bad["frames"][0]["path"] = link.name
        path = coverage.parent / "symlink.json"
        path.write_text(json.dumps(bad))
        run("symlink", ["--fluid25d-native-display-coverage", str(path)], success=False)
        if args.video:
            output, text = run("multislot", ["--capture", "video", "--frames", "4", "--fps", "4",
                "--output", str(root / "multislot.mp4"), "--fluid25d-recording-frame-interval-seconds", "1",
                "--fluid25d-native-display-coverage", str(coverage), "--profile-warmup-frames", "0",
                "--profile-output", str(root / "profile")])
            assert "saved_s=1.000000000" in text and "saved_s=2.000000000" in text
            assert (root / "multislot.mp4").is_file()
        print("fluid_25d_display_coverage_gpu: PASS rendering effect, raw diagnostics, deterministic masks, strict rejection, bit-exact native fields")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        if str(error) == "SKIP_NO_VULKAN":
            raise SystemExit(77)
        raise
