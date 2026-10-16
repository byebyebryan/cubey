#!/usr/bin/env python3
"""Audit the completed study against its raw slots, profiles and frozen inputs."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import run_offscreen_perf_campaign_v2 as campaign
import run_offscreen_perf_v2 as runner
import review_offscreen_perf_v2 as review


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command_app(command):
    return Path(command[2] if command[:2] == ["rtk", "proxy"] else command[0])


def capture_failures(root, captures):
    failures = []
    for item in captures["captures"]:
        suffix = "-offscreen.png" if item["host"] == "offscreen PNG" else "-wayland.png"
        image = root / "captures" / (item["name"] + suffix)
        command = item["command"]
        recording = Path(command[command.index("--fluid25d-recording") + 1])
        if not image.is_file() or sha(image) != item["image_sha256"]:
            failures.append(item["name"] + ": current capture image changed")
        if not recording.is_file() or sha(recording) != item["recording_sha256"]:
            failures.append(item["name"] + ": captured source recording changed")
        if sha(command_app(command)) != captures["app_sha256"]:
            failures.append(item["name"] + ": current capture app changed")
    return failures


def progress_valid(row):
    if row["mode"] == "renderer-only":
        return True
    samples = row.get("samples", [])
    if len(samples) < 2 or any(s["lifecycle"] != "running" or
            not math.isfinite(s["physical_s"]) for s in samples):
        return False
    if len({s["generation"] for s in samples}) != 1:
        return False
    if any(b["physical_s"] < a["physical_s"] or b["sequence"] < a["sequence"]
            for a, b in zip(samples, samples[1:])):
        return False
    return (samples[-1]["physical_s"] > samples[0]["physical_s"] and
            row["measurement_final_physical_s"] >= samples[-1]["physical_s"])


def expected_slots(study, hosts):
    expected = set()
    for stage, selected in (("primary", ["offscreen", "wayland"]),
                            ("corrected", hosts), ("secondary", hosts)):
        for phase, _, _, options in campaign.selected_plan(study, stage, selected):
            modes = ["live"]
            if "--modes" in options:
                modes = []
                index = options.index("--modes") + 1
                while index < len(options) and not options[index].startswith("--"):
                    modes.append(options[index])
                    index += 1
            for repeat in range(int(options[options.index("--repeats") + 1])):
                expected.update((phase, mode, repeat) for mode in modes)
    return expected


def audit(root, study):
    import run_live_perf_terrain_v1 as v1
    scope = json.loads((root / "study-scope.json").read_text())
    hosts = scope["continuation_hosts"]
    expected = expected_slots(study, hosts)
    aliases = json.loads((root / "phase-aliases.json").read_text())["aliases"]
    baseline = json.loads((root / "baseline-completion.json").read_text())
    captures = json.loads((root / "captures/receipt.json").read_text())
    current_app = captures["app_sha256"]
    actual, failures, retained, proofs = set(), capture_failures(root, captures), [], []
    control_dir = root / "post-fix-control-validation/live-0"
    control = json.loads((control_dir / "result.json").read_text())
    acknowledgements = [item["ack"] for item in control["controls"]]
    if control["status"] != "completed" or control["errors"] or control["app_sha256"] != current_app or \
            [item["lifecycle"] for item in acknowledgements] != ["running", "paused", "running", "ready", "running", "paused", "stopped"] or \
            any(item["state"] != "applied" for item in acknowledgements) or \
            "generation=2 sequence=" not in (control_dir / "viewer.log").read_text() or \
            not review.checks(control, control_dir)["owned_cleanup"]:
        failures.append("post-fix pause/resume/reset or owned-shutdown proof failed")
    if sha(runner.demo.EXTENSION) != runner.demo.EXTENSION_SHA:
        failures.append("current native extension differs from the frozen reference")
    identities, shader_identities = {}, {}
    critical = ("completed", "inputs_unchanged", "native_inputs_unchanged", "owned_cleanup",
                "actual_expected_backend", "actual_expected_device",
                "viewer_hydraulic_dispatches_zero", "display_near_worker_endpoint")

    for path in sorted(root.glob("*/*/result.json")):
        phase = aliases.get(path.parent.parent.name, path.parent.parent.name)
        if not phase.startswith(("primary-", "corrected-", "paced-", "rolling-confirmation", "held-", "soak-")):
            continue
        row = json.loads(path.read_text())
        relative = str(path.parent.relative_to(root))
        if row["status"] != "completed" or row.get("errors"):
            retained.append(relative)
            continue
        slot = (phase, row["mode"], row["repeat"])
        if slot not in expected or slot in actual:
            failures.append(relative + ": unexpected or duplicate completed slot")
        actual.add(slot)
        checks = review.checks(row, path.parent)
        if any(checks[key] is False for key in critical) or checks["viewer_error"]:
            failures.append(relative + ": correctness check failed")
        if row["mode"] == "live" and any(checks[key] is not True for key in
                ("viewer_hydraulic_dispatches_zero", "display_near_worker_endpoint")):
            failures.append(relative + ": missing live field/dispatch proof")
        if not progress_valid(row):
            failures.append(relative + ": missing continuous advancing-worker proof")
        if row["case"] not in identities:
            identities[row["case"]] = runner.terrain.case_identity(study / "cases" / row["case"])
        identity = identities[row["case"]]
        if identity != row["input_identity"] or identity != row["input_identity_after"]:
            failures.append(relative + ": current frozen inputs differ from timed inputs")
        duration = row.get("measurement_wall_s", 0)
        if not row["wall_target_s"] <= duration <= row["wall_target_s"] + 10:
            failures.append(relative + ": incomplete or unbounded timing window")
        expected_app = baseline["app_sha256"] if phase.startswith("primary-") else current_app
        protocol_path = path.parent.parent / "protocol.json"
        protocol = json.loads(protocol_path.read_text())
        if row["app_sha256"] != expected_app or protocol["app_sha256"] != expected_app:
            failures.append(relative + ": incorrect frozen/current app identity")
        if protocol["extension_sha256"] != runner.demo.EXTENSION_SHA or protocol["worker_sha256"] != sha(runner.terrain.NATIVE_SUPPORT / "session_worker.py"):
            failures.append(relative + ": native extension or worker identity changed")
        shader_root = Path(protocol["arguments"]["app"]).parent / "shaders"
        if shader_root not in shader_identities:
            runner.demo.ref.SHADER_ROOT = shader_root
            shader_identities[shader_root] = runner.demo.ref.shader_identity()
        if shader_identities[shader_root] != protocol["shader_identity"]:
            failures.append(relative + ": compiled shaders changed")
        if row["mode"] != "worker-only":
            raw = v1.profile_summary(path.parent / "viewer-profile",
                (row["measurement_start_unix_s"], row["measurement_end_unix_s"]))
            if raw["passes_ms"] != row["profile"]["passes_ms"] or raw["metrics"] != row["profile"]["metrics"] or raw["frame_ms"] != row["profile"]["frame_ms"]:
                failures.append(relative + ": stored timing summary does not reproduce from raw CSV")
            if raw["viewport"] != [(row["requested_viewport"][0], row["requested_viewport"][1])]:
                failures.append(relative + ": framebuffer mismatch")
            if row["host"] == "offscreen" and (row["display_environment"].get("DISPLAY") or row["display_environment"].get("WAYLAND_DISPLAY") or list(path.parent.glob("*.png")) or list(path.parent.glob("*.mp4"))):
                failures.append(relative + ": display or capture work in render-only trial")
        proofs.append({"directory": relative, "result_sha256": sha(path),
                       "protocol_sha256": sha(protocol_path), "app_sha256": expected_app,
                       "raw_profile_sha256": {name: sha(path.parent / ("viewer-profile." + name + ".csv"))
                           for name in ("frames", "passes", "metrics")} if row["mode"] != "worker-only" else {}})
    if actual != expected:
        failures.append("missing slots: " + repr(sorted(expected - actual)))
    if retained != [str(Path(baseline["partial_attempt"]).parent)]:
        failures.append("unexpected incomplete attempts: " + repr(retained))
    for item in captures["legacy_png_regressions"]:
        name = item["name"]
        before = root / "captures" / (name + "-baseline.png")
        after = root / "captures" / (name + "-offscreen.png")
        if sha(command_app(item["baseline_command"])) != baseline["app_sha256"] or item["baseline_app_sha256"] != baseline["app_sha256"]:
            failures.append(name + ": archived capture app changed")
        if sha(before) != item["baseline_image_sha256"] or sha(after) != item["current_image_sha256"] or before.read_bytes() != after.read_bytes():
            failures.append(name + ": legacy PNG byte regression")
    expected_captures = 4 if scope.get("native_captures_deferred") else 6
    if len(captures["captures"]) != expected_captures or len(captures["legacy_png_regressions"]) != 2 or captures.get("headless_only") != bool(scope.get("native_captures_deferred")):
        failures.append("missing amended headless visual/legacy comparison receipts")
    result = {"schema": "cubey.fluid25d.offscreen-perf-audit.v2", "expected_slots": len(expected),
              "completed_slots": len(actual), "retained_incomplete_attempts": retained,
              "capture_receipt_sha256": sha(root / "captures/receipt.json"),
              "post_fix_control_receipt_sha256": sha(control_dir / "result.json"),
              "failures": failures, "proofs": proofs,
              "scope": "Recomputed raw timing summaries, actual backend/device/viewport receipts, current frozen inputs, app/native identities, owned cleanup and exact archived-app PNG comparisons. Performance targets remain separate; no human motion acceptance implied."}
    (root / "audit.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--study", required=True, type=Path)
    args = parser.parse_args()
    result = audit(args.out.resolve(strict=True), args.study.resolve(strict=True))
    print(json.dumps({key: result[key] for key in ("expected_slots", "completed_slots", "failures")}))
    return bool(result["failures"])


if __name__ == "__main__":
    raise SystemExit(main())
