import argparse
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import run_offscreen_perf_v2 as study
import run_offscreen_perf_campaign_v2 as campaign
import review_offscreen_perf_v2 as review
import audit_offscreen_perf_v2 as audit


class ContractTests(unittest.TestCase):
    def test_progress_audit_rejects_stalls_reversal_reset_and_nonrunning(self):
        first = {"lifecycle": "running", "physical_s": 10, "generation": 1, "sequence": 10}
        last = {**first, "physical_s": 20, "sequence": 20}
        row = {"mode": "live", "samples": [first, last], "measurement_final_physical_s": 20}
        self.assertTrue(audit.progress_valid(row))
        for change in ({"physical_s": 10}, {"physical_s": 9}, {"sequence": 9},
                       {"generation": 2}, {"lifecycle": "paused"}, {"physical_s": float("nan")}):
            self.assertFalse(audit.progress_valid({**row, "samples": [first, {**last, **change}]}))
        self.assertTrue(audit.progress_valid({"mode": "renderer-only"}))

    def test_capture_audit_rechecks_images_sources_and_app(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "captures").mkdir()
            image = root / "captures/held-offscreen.png"
            source = root / "recording.json"
            app = root / "app"
            for path in (image, source, app):
                path.write_bytes(b"frozen")
            captures = {"app_sha256": audit.sha(app), "captures": [{
                "name": "held", "host": "offscreen PNG", "image_sha256": audit.sha(image),
                "recording_sha256": audit.sha(source),
                "command": ["rtk", "proxy", str(app), "--fluid25d-recording", str(source)]}]}
            self.assertEqual(audit.capture_failures(root, captures), [])
            self.assertEqual(audit.command_app([str(app)]), app)
            for path in (image, source, app):
                path.write_bytes(b"changed")
            self.assertEqual(len(audit.capture_failures(root, captures)), 3)

    def test_wayland_mapping_is_bounded_and_ambiguity_fails_closed(self):
        window = {"id": 42, "title": "Cubey V2 owned", "is_floating": True}
        with patch.dict(os.environ, {"NIRI_SOCKET": "owned-socket"}), \
             patch.object(study.subprocess, "check_output", side_effect=[b"[]", json.dumps([window]).encode()]), \
             patch.object(study.subprocess, "run") as run, patch.object(study.time, "sleep"):
            result = study.size_owned_window("Cubey V2 owned", 1920, 1080)
            self.assertEqual(result["window_id"], 42)
            self.assertEqual(run.call_count, 2)
            self.assertTrue(all("42" in call.args[0] for call in run.call_args_list))
        for windows in ([], [window, window]):
            with patch.dict(os.environ, {"NIRI_SOCKET": "owned-socket"}), \
                 patch.object(study.subprocess, "check_output", return_value=json.dumps(windows).encode()), \
                 patch.object(study.subprocess, "run") as run:
                with self.assertRaisesRegex(RuntimeError, "unique owned"):
                    study.size_owned_window("Cubey V2 owned", 1920, 1080, timeout=0)
                run.assert_not_called()

    def test_offscreen_has_no_display_and_native_has_no_x11_fallback(self):
        with patch.dict(os.environ, {"DISPLAY": ":88", "WAYLAND_DISPLAY": "wayland-1",
                                    "CUBEY_PRIVATE_XVFB": "1"}, clear=True):
            env = study.environment("offscreen")
            self.assertNotIn("DISPLAY", env)
            self.assertNotIn("WAYLAND_DISPLAY", env)
            self.assertNotIn("CUBEY_PRIVATE_XVFB", env)
            native = study.environment("wayland")
            self.assertNotIn("DISPLAY", native)
            self.assertEqual(native["XDG_SESSION_TYPE"], "wayland")
            self.assertEqual(native["WAYLAND_DISPLAY"], "wayland-1")
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(ValueError, "unavailable"):
                study.environment("wayland")

    def test_render_only_reuses_existing_recipe_and_has_no_encoder(self):
        args = argparse.Namespace(width=1920, height=1080, wall=30, warmup=5,
            render_fps=0, app=Path("release-app"), prefix=Path("profile"),
            camera="overview", host="offscreen")
        argv = study.viewer_command(Path("case"), Path("recording.json"), "owned", args, False, 123)
        self.assertIn("--headless-render-only", argv)
        self.assertIn("--headless", argv)
        self.assertNotIn("--capture", argv)
        self.assertIn("bspline-2x", argv)
        self.assertIn("--run-seconds", argv)
        self.assertIn("--no-validation", argv)
        self.assertEqual(argv[argv.index("--render-fps")+1], "0")
        args.host = "wayland"
        native = study.viewer_command(Path("case"), Path("session"), "owned", args, True, 0)
        self.assertNotIn("--headless", native)
        self.assertIn("--fluid25d-external-session", native)
        self.assertNotIn("--fluid25d-recording", native)

    def test_campaign_is_sequential_and_has_42_primary_and_56_total_trials(self):
        rows = campaign.plan(Path("frozen-study"))
        primary = total = 0
        for name, case, host, options in rows:
            repeats = int(options[options.index("--repeats") + 1])
            if "--modes" in options:
                index = options.index("--modes") + 1
                modes = []
                while index < len(options) and not options[index].startswith("--"):
                    modes.append(options[index])
                    index += 1
            else:
                modes = ["live"]
            count = repeats * len(modes)
            total += count
            if name.startswith("primary-"):
                primary += count
            self.assertIn(host, ("offscreen", "wayland"))
            if host == "wayland":
                self.assertNotIn("worker-only", modes)
            if name.startswith("held-"):
                self.assertEqual(host, "offscreen")
        self.assertEqual((primary, total), (42, 56))

    def test_correction_stage_has_six_affected_repeats_and_four_controls(self):
        rows = campaign.correction_plan(Path("frozen-study"))
        self.assertEqual(sum(int(options[options.index("--repeats") + 1])
                             for _, _, _, options in rows), 10)
        for name, _, host, options in rows:
            self.assertTrue(name.startswith("corrected-"))
            repeats = int(options[options.index("--repeats") + 1])
            if name.endswith("-held"):
                self.assertEqual(host, "offscreen")
                self.assertIn("--recording", options)
                self.assertEqual(repeats, 1)
            else:
                self.assertEqual(options[options.index("--modes") + 1], "live")
                self.assertEqual(repeats, 3 if host == "offscreen" else 1)

    def test_campaign_resume_rejects_partial_duplicate_or_failed_trials(self):
        options = ["--modes", "renderer-only", "live", "--repeats", "3"]
        trials = [{"mode": mode, "repeat": repeat, "status": "completed", "errors": []}
                  for mode in ("renderer-only", "live") for repeat in range(3)]
        self.assertTrue(campaign.complete_phase(trials, options))
        self.assertFalse(campaign.complete_phase(trials[:-1], options))
        self.assertFalse(campaign.complete_phase(trials[:-1] + [trials[0]], options))
        trials[-1]["errors"] = ["failed receipt"]
        self.assertFalse(campaign.complete_phase(trials, options))

    def test_headless_continuation_never_selects_native_windows(self):
        for stage, expected in (("corrected", 8), ("secondary", 12)):
            rows = campaign.selected_plan(Path("frozen-study"), stage, ["offscreen"])
            self.assertTrue(all(host == "offscreen" for _, _, host, _ in rows))
            count = 0
            for _, _, _, options in rows:
                repeats = int(options[options.index("--repeats") + 1])
                # These selected stages contain exactly one workload per phase.
                count += repeats
            self.assertEqual(count, expected)
        slots = audit.expected_slots(Path("frozen-study"), ["offscreen"])
        self.assertEqual(len(slots), 62)
        self.assertEqual(sum(phase.startswith("primary-") for phase, _, _ in slots), 42)
        self.assertTrue(all("wayland" not in phase for phase, _, _ in slots
                            if not phase.startswith("primary-")))

    def test_summary_separates_pacing_service_and_gpu(self):
        row = {"host": "offscreen", "profile": {"frame_ms": {"p95": 16.8},
               "passes_ms": {"cpu:headless.frame_service": {"p95": 3.2},
                             review.GPU: {"p95": 2.9},
                             "cpu:headless.intentional_pacing": {"p95": 13.5}}}}
        values = review.measure(row)
        self.assertEqual(values["loop_p95_ms"], 16.8)
        self.assertEqual(values["service_p95_ms"], 3.2)
        self.assertEqual(values["gpu_p95_ms"], 2.9)
        self.assertEqual(values["intentional_pacing_p95_ms"], 13.5)
        self.assertIsNone(values["pace_x"])
        self.assertEqual(review.spread([3, 4, 5]), {"n": 3, "median": 4, "min": 3, "max": 5})

    def test_report_checks_do_not_turn_x11_into_native_evidence(self):
        row = {"host": "wayland", "mode": "renderer-only", "status": "completed",
               "input_identity": {"sha": "unchanged"}, "input_identity_after": {"sha": "unchanged"},
               "cleanup": [{"exit_code": 0, "active_group_remaining": [], "escalated": False}]}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "viewer.log").write_text("cubey_window_backend: x11\nNVIDIA GeForce RTX 5070 Ti\n")
            checks = review.checks(row, root)
            self.assertFalse(checks["actual_expected_backend"])
            self.assertTrue(checks["actual_expected_device"])
            self.assertTrue(checks["owned_cleanup"])
            self.assertIsNone(checks["native_inputs_unchanged"])

    def test_analysis_keeps_app_hashes_and_correction_results_separate(self):
        row = {"host": "offscreen", "mode": "live", "case": "mountain-patch-2048-8m",
               "repeat": 0, "status": "completed", "errors": [],
               "input_identity": {"sha": "unchanged"}, "input_identity_after": {"sha": "unchanged"},
               "cleanup": [{"exit_code": 0, "active_group_remaining": [], "escalated": False}],
               "completion": {"inputs_unchanged": True, "reference_inputs_unchanged": True},
               "measurement_final_physical_s": 40, "physical_wall_ratio": 1,
               "profile": {"metrics": {"fluid_25d.external.hydraulic_dispatch_count": {"max": 0},
                                        "fluid_25d.external.physical_time_s": {"max": 40}}}}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for phase, app_hash in (("primary-2048-8m-offscreen", "original-app"),
                                    ("corrected-2048-8m-offscreen-live", "corrected-app"),
                                    ("native-wayland-map-race-preflight", "original-app")):
                directory = root / phase / "live-0"
                directory.mkdir(parents=True)
                (directory / "result.json").write_text(json.dumps({**row, "app_sha256": app_hash}))
                (directory / "viewer.log").write_text(
                    "headless_render_only_ready: NVIDIA GeForce RTX 5070 Ti readback=0 encode=0\n")
            result = review.analyze(root)
            self.assertEqual(result["observed_primary_trials"], 1)
            self.assertEqual(result["observed_correction_trials"], 1)
            self.assertEqual(result["observed_total_trials"], 2)
            self.assertEqual(len(result["phases"]), 2)
            self.assertEqual({t["app_sha256"] for t in result["trials"]},
                             {"original-app", "corrected-app"})
            self.assertTrue(all(t["checks"]["viewer_hydraulic_dispatches_zero"] and
                                t["checks"]["display_near_worker_endpoint"] for t in result["trials"]))
            replacement = root / "primary-2048-8m-offscreen-replacement" / "live-0"
            replacement.mkdir(parents=True)
            failed = root / "primary-2048-8m-offscreen" / "live-0" / "result.json"
            failed.write_text(json.dumps({**row, "status": "failed", "errors": ["early exit"],
                                          "app_sha256": "original-app"}))
            (replacement / "result.json").write_text(json.dumps({**row, "app_sha256": "original-app"}))
            (replacement / "viewer.log").write_text(
                "headless_render_only_ready: NVIDIA GeForce RTX 5070 Ti readback=0 encode=0\n")
            (root / "phase-aliases.json").write_text(json.dumps({"aliases": {
                "primary-2048-8m-offscreen-replacement": "primary-2048-8m-offscreen"}}))
            result = review.analyze(root)
            self.assertEqual(result["observed_attempts"], 3)
            self.assertEqual(result["observed_total_trials"], 2)
            self.assertEqual(result["observed_primary_trials"], 1)
            self.assertEqual(len(result["phases"]), 2)
            self.assertEqual(len(result["correctness_failures"]), 1)


if __name__ == "__main__":
    unittest.main()
