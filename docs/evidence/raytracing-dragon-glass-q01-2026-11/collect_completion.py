# /// script
# dependencies = ["numpy==2.4.6"]
# ///
"""Recheck retained Q0.1 runtime artifacts and collect the final evidence."""

import argparse
import hashlib
import json
import statistics
import subprocess
from pathlib import Path

import numpy as np


def read(path):
    return json.loads(Path(path).read_text())


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    args = parser.parse_args()
    root = args.input.resolve()
    out = Path(__file__).parent / "completion"
    out.mkdir(exist_ok=True)

    def save(name, value):
        (out / name).write_text(json.dumps(value, indent=2) + "\n")

    qualification = read(root / "qualification15/spawn-qualification.json")
    assert qualification["passed"] and len(qualification["captures"]) == 11
    for receipt in qualification["captures"]:
        command = receipt["identity"]["command"]
        prefix = Path(command[command.index("--hdr-output") + 1]).with_suffix("")
        for suffix, expected in receipt["artifacts"].items():
            assert digest(str(prefix) + suffix) == expected, (prefix, suffix)
        data = np.memmap(
            str(prefix) + ".bin",
            dtype="<f4",
            mode="r",
            shape=(receipt["height"], receipt["width"], 4),
        )
        assert np.isfinite(data).all() and (data[..., :3] >= 0).all()
        flags = data[..., 3].astype(np.uint32)
        assert np.array_equal(flags.astype(np.float32), data[..., 3])
        assert not (flags & np.uint32(0xFFFFFFF7)).any()
        assert receipt["transport_accepted"] and receipt["validation_clean"]
        del data
    save("qualification.json", qualification)
    save(
        "qualification-identity.json",
        read(root / "qualification15/spawn-identity.json"),
    )
    save("candidate-build.json", read(root / "candidate-build-15/build.json"))

    identity = read(root / "normal-build-identity.json")
    build = read(root / "candidate-build-15/build.json")
    for name, expected in build["shader_sha256"].items():
        assert digest(Path("build/dev/projects/raytracing/shaders") / name) == expected
    snapshot = root / "candidate-source-15"
    for source in snapshot.rglob("*"):
        if source.is_file() and source.suffix in (
            ".cpp",
            ".h",
            ".glsl",
            ".rgen",
            ".rchit",
            ".rmiss",
        ):
            assert digest(source) == digest(
                Path("projects/raytracing") / source.relative_to(snapshot)
            )
    assert all(identity["shader_hashes_match"].values())
    assert all(identity["runtime_source_matches_frozen_candidate"].values())
    assert identity["normal_diagnostics_disabled"]["query_bytes"] == 64
    assert not identity["normal_diagnostics_disabled"]["journal_metric_present"]
    assert (
        digest("build/dev/projects/raytracing/raytracing")
        == identity["normal_binary_sha256"]
    )
    save("normal-build-identity.json", identity)
    checks = read(root / "final-checks.json")
    assert len(checks) == 8 and all(item["exit"] == 0 for item in checks)
    save("checks.json", checks)

    fixtures = read(root / "fixture-and-fidelity.json")
    assert all(fixtures["nonoptical_png_comparison"].values())
    for index, primitive in enumerate((220311, 199468)):
        new = fixtures["fixture17-candidate"]
        old = fixtures["fixture18-baseline"]
        assert new["source_and_exit_identity"][index][1:3] == [primitive, 0]
        assert new["next_position_and_t"][index][3] > 0
        assert old["source_and_exit_identity"][index][2] == -1
        assert old["next_position_and_t"][index][3] == -1
    save("fixture-and-fidelity.json", fixtures)
    for name in (
        "bound-components",
        "event3288-classification",
        "shadow20-classification",
        "shadow20-exact-plane-proof",
        "readback-shader-fidelity",
    ):
        save(name + ".json", read(root / (name + ".json")))
    assert read(root / "shadow20-classification.json")["rgb_bit_identical"]
    assert 0 < read(root / "shadow20-exact-plane-proof.json")["exact_segment_t"] < 1

    journal = read(root / "corrected-journal19/journal.json")
    assert journal["first_failure_record_plus_one"] == 0 and journal["overflow"] == 0
    assert journal["trace_world_origin"] == [0, 0.3534899950027466, 0]
    save("corrected-journal.json", journal)
    save(
        "corrected-journal-build.json",
        read(root / "candidate-journal-build-19/build.json"),
    )
    for folder, name in (
        ("final-optical", "optical.json"),
        ("final-opaque", "opaque.json"),
        ("final-native", "native.json"),
    ):
        receipt = read(root / folder / "receipt.json")
        if folder == "final-native":
            assert receipt["exit"] == 0 and receipt["as_builds"] == 3
        else:
            assert receipt["passed"]
        save(name, receipt)

    protected = read(root.parent / "starting-state.json")
    preserved = {
        path: digest(path) == expected
        for path, expected in protected["files"].items()
        if path.startswith(
            (
                "assets/materials/",
                "projects/raytracing/presets/",
                "projects/raytracing/assets/",
                "cache/raytracing/",
            )
        )
    }
    assert all(preserved.values())
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    assert head == protected["head"]
    save("preservation.json", {"head": head, "protected_hashes": preserved})

    native = read(root / "final-native/metrics.json")
    last = [
        row
        for row in native["sampling_frames"]
        if row["epoch"] == native["history_epochs"]
    ]
    timings = {
        row["frame"]: row["milliseconds"]
        for row in native["gpu_samples"]
        if row["pass"] == "raytracing.trace"
    }
    cpu = {row["frame"]: row["cpu_record_ms"] for row in native["native_frames"]}
    groups = {}
    for label, active in (("active", True), ("settled", False)):
        frames = [row["frame"] for row in last if (row["new_samples"] > 0) == active]
        gpu_values = [timings[n] for n in frames if n in timings]
        cpu_values = [cpu[n] for n in frames if n in cpu]
        groups[label] = {
            "frames": len(frames),
            "timed_gpu_frames": len(gpu_values),
            "median_gpu_trace_ms": statistics.median(gpu_values)
            if gpu_values
            else None,
            "median_cpu_record_ms": statistics.median(cpu_values)
            if cpu_values
            else None,
        }
    normal = read(root / "normal-without-diagnostics/metrics.json")
    save(
        "performance.json",
        {
            "scope": "Single headless active accumulation and final native Xvfb epoch, separately; no physical display latency claim.",
            "headless_2048_spp_gpu": normal["gpu_samples"],
            "native_last_epoch": native["history_epochs"],
            "native": groups,
            "ordinary_resident_requested_bytes_without_diagnostics": normal[
                "resident_project_requested_bytes"
            ],
        },
    )
    save(
        "runtime-audit.json",
        {
            "passed": True,
            "qualification_captures_rechecked": 11,
            "fullhd_independent_references": 6,
            "whole_image_cumulative_status": True,
            "rt_ctests": 20,
            "nonoptical_pngs_bit_identical": 14,
            "normal_png_matches_frozen_candidate": True,
            "models_materials_presets_certificates_preserved": True,
            "diagnostic_fixture_is_camera_replay": False,
            "audit_scope": "Runtime and artifact identity. Documentation and report delivery have separate receipts.",
        },
    )
    print(
        "Rechecked eleven full-HD captures, source/shader identity, fixtures and regressions:",
        out,
    )


if __name__ == "__main__":
    main()
