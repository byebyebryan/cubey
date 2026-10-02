from __future__ import annotations

from decimal import Decimal
from pathlib import Path
import tempfile
import unittest

import run_hillside_pacing_v5 as pacing


class WindowedPacingV5Tests(unittest.TestCase):
    def make_valid_data(self, markers_enabled: bool = True):
        frames = {}
        metrics = {}
        passes = []
        physical_time = Decimal(0)
        volume = Decimal(0)
        last_rate = Decimal("100")
        remaining = pacing.ADVANCE_SECONDS // int(pacing.DT)
        continuous_wall = Decimal(0)
        continuous_physical = Decimal(0)

        for frame_index in range(pacing.FRAME_COUNT):
            advance_before = remaining
            fixed_steps = min(pacing.SUBSTEPS, remaining) if remaining else 1
            remaining -= fixed_steps if advance_before else 0
            for _ in range(fixed_steps):
                last_rate = pacing._rate_at_step_start(physical_time)
                volume += last_rate * pacing.DT
                physical_time += pacing.DT

            delta_ms = Decimal("10") if advance_before else Decimal("250")
            if advance_before:
                achieved = Decimal(0)
            else:
                continuous_wall += delta_ms / Decimal(1000)
                continuous_physical += Decimal(fixed_steps) * pacing.DT
                achieved = continuous_physical / continuous_wall
            frames[frame_index] = {
                "frame_index": frame_index,
                "delta_ms": delta_ms,
                "width": pacing.WIDTH,
                "height": pacing.HEIGHT,
            }
            metrics[frame_index] = {
                "physical_time_s": physical_time,
                "last_step_source_m3_per_s": last_rate,
                "scheduled_source_volume_m3": volume,
                "manual_supply_override": Decimal(0),
                "fixed_steps": Decimal(fixed_steps),
                "paused": Decimal(1 if remaining else 0),
                "advance_remaining_steps": Decimal(remaining),
                "requested_playback_x": pacing.REQUESTED_PLAYBACK_X,
                "achieved_continuous_playback_x": achieved,
                "dropped_backlog_frames": Decimal(0),
            }
            if markers_enabled and not advance_before:
                passes.extend([
                    {"frame_index": frame_index, "kind": "gpu",
                     "label": pacing.MARKER_UPDATE_LABEL, "start_ms": Decimal(0),
                     "duration_ms": Decimal("0.006")},
                    {"frame_index": frame_index, "kind": "gpu",
                     "label": pacing.MARKER_DRAW_LABEL, "start_ms": Decimal(0),
                     "duration_ms": Decimal("0.003")},
                ])
        return frames, metrics, passes

    def test_accepts_complete_response_trace_and_reports_scope_summaries(self):
        frames, metrics, passes = self.make_valid_data()
        result = pacing.validate_windowed_data(frames, metrics, passes, markers_enabled=True)
        self.assertTrue(result["passed"])
        self.assertEqual(result["rendered_frames"], 900)
        self.assertEqual(result["last_advance_frame_index"], 110)
        self.assertEqual(result["continuous_start_frame_index"], 111)
        self.assertEqual(result["continuous_frames"], 789)
        self.assertGreater(Decimal(result["final_physical_time_s"]), Decimal("3602"))
        self.assertEqual(result["final_last_step_source_m3_per_s"], "150")
        self.assertEqual(result["final_achieved_continuous_playback_x"], "8")
        self.assertEqual(result["continuous_interval_ms"]["samples"], 789)
        self.assertEqual(result["gpu_marker_timing_ms"][pacing.MARKER_DRAW_LABEL]["samples"], 789)
        self.assertIn("not a statistically isolated", result["claim_boundary"])

    def test_marker_off_rejects_marker_gpu_passes(self):
        frames, metrics, passes = self.make_valid_data(markers_enabled=False)
        passes.append({"frame_index": 120, "kind": "gpu",
                       "label": pacing.MARKER_DRAW_LABEL, "start_ms": Decimal(0),
                       "duration_ms": Decimal("0.004")})
        with self.assertRaisesRegex(ValueError, "even though marker rendering was disabled"):
            pacing.validate_windowed_data(frames, metrics, passes, markers_enabled=False)

    def test_missing_render_frame_fails_closed(self):
        frames, metrics, passes = self.make_valid_data()
        del frames[450]
        with self.assertRaisesRegex(ValueError, "each requested frame exactly once"):
            pacing.validate_windowed_data(frames, metrics, passes, markers_enabled=True)

    def test_missing_playback_metric_frame_fails_closed(self):
        frames, metrics, passes = self.make_valid_data()
        del metrics[450]
        with self.assertRaisesRegex(ValueError, "each requested frame exactly once"):
            pacing.validate_windowed_data(frames, metrics, passes, markers_enabled=True)

    def test_backlog_and_manual_override_are_rejected(self):
        frames, metrics, passes = self.make_valid_data()
        metrics[250]["dropped_backlog_frames"] = Decimal(1)
        with self.assertRaisesRegex(ValueError, "dropped pacing backlog"):
            pacing.validate_windowed_data(frames, metrics, passes, markers_enabled=True)

        frames, metrics, passes = self.make_valid_data()
        metrics[251]["manual_supply_override"] = Decimal(1)
        with self.assertRaisesRegex(ValueError, "manual supply override"):
            pacing.validate_windowed_data(frames, metrics, passes, markers_enabled=True)

    def test_supply_integral_and_post_transition_rate_are_independently_checked(self):
        frames, metrics, passes = self.make_valid_data()
        metrics[130]["scheduled_source_volume_m3"] += Decimal(1)
        with self.assertRaisesRegex(ValueError, "independent schedule"):
            pacing.validate_windowed_data(frames, metrics, passes, markers_enabled=True)

        frames, metrics, passes = self.make_valid_data()
        metrics[136]["last_step_source_m3_per_s"] = Decimal("100")
        with self.assertRaisesRegex(ValueError, "last-step source rate"):
            pacing.validate_windowed_data(frames, metrics, passes, markers_enabled=True)

    def test_paused_state_and_continuous_pacing_are_checked(self):
        frames, metrics, passes = self.make_valid_data()
        metrics[10]["paused"] = Decimal(0)
        with self.assertRaisesRegex(ValueError, "paused state"):
            pacing.validate_windowed_data(frames, metrics, passes, markers_enabled=True)

        frames, metrics, passes = self.make_valid_data()
        metrics[899]["achieved_continuous_playback_x"] = Decimal("7.4")
        with self.assertRaisesRegex(ValueError, "achieved playback does not reconcile"):
            pacing.validate_windowed_data(frames, metrics, passes, markers_enabled=True)

    def test_schedule_boundaries_are_half_open(self):
        self.assertEqual(pacing._rate_at_step_start(Decimal("3598")), Decimal("100"))
        self.assertEqual(pacing._rate_at_step_start(Decimal("3600")), Decimal("150"))
        self.assertEqual(pacing._rate_at_step_start(Decimal("5398")), Decimal("150"))
        self.assertEqual(pacing._rate_at_step_start(Decimal("5400")), Decimal("50"))
        self.assertEqual(pacing._rate_at_step_start(Decimal("7198")), Decimal("50"))
        self.assertEqual(pacing._rate_at_step_start(Decimal("7200")), Decimal("100"))

    def test_command_removes_headless_and_keeps_controls_consistent(self):
        profile_prefix = Path("/tmp/hillside-pacing-v5/profile")
        off = pacing.windowed_arguments(False, profile_prefix)
        on = pacing.windowed_arguments(True, profile_prefix)
        self.assertNotIn("--headless", off)
        self.assertNotIn("--headless", on)
        self.assertNotIn("--fluid25d-motion-markers", off)
        self.assertNotIn("--fluid25d-motion-marker-mode", off)
        self.assertIn("--fluid25d-motion-markers", on)
        self.assertEqual(on[on.index("--fluid25d-motion-marker-mode") + 1], "local")
        for args in (off, on):
            self.assertEqual(args[args.index("--frames") + 1], "900")
            self.assertEqual(args[args.index("--fluid25d-fixed-delta-seconds") + 1], "2")
            self.assertEqual(args[args.index("--fluid25d-substeps") + 1], "16")
            self.assertEqual(args[args.index("--fluid25d-natural-flow-source-m3-per-s") + 1], "100")
            self.assertEqual(args[args.index("--fluid25d-presentation-time-scale") + 1], "8")
            self.assertEqual(args[args.index("--fluid25d-hillside-advance-and-continue-seconds") + 1], "3550")
            self.assertEqual(args[args.index("--fluid25d-natural-flow-home-pitch-radians") + 1], "-0.72")
            self.assertEqual(args[args.index("--fluid25d-hillside-camera") + 1], "source")
            self.assertIn("--fluid25d-hillside-depth-cues", args)
            self.assertIn("--fluid25d-hillside-supply-response", args)

    def test_identity_and_required_artifact_checks_reject_mutation_or_missing_files(self):
        identity = {"app_sha256": "a", "shader_map_sha256": "b"}
        pacing.require_stable_identity(identity, dict(identity))
        with self.assertRaisesRegex(ValueError, "identity changed"):
            pacing.require_stable_identity(identity, {**identity, "app_sha256": "different"})

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            prefix = root / "windowed-marker-off"
            paths = pacing._artifact_map(prefix, root / "child.log")
            with self.assertRaisesRegex(FileNotFoundError, "omitted required artifacts"):
                pacing.require_windowed_artifacts(paths, prefix)
            for name in ("child.log", *(prefix.name + suffix for suffix in pacing.REQUIRED_SUFFIXES)):
                paths[name].write_text("header\n")
            pacing.require_windowed_artifacts(paths, prefix)

    def test_marker_off_on_runs_require_one_shared_before_after_identity(self):
        identity = {"app_sha256": "a", "shader_map_sha256": "b"}
        runs = [
            {"label": "marker-off", "passed": True,
             "before": dict(identity), "after": dict(identity)},
            {"label": "marker-on", "passed": True,
             "before": dict(identity), "after": dict(identity)},
        ]
        self.assertEqual(pacing.require_comparable_run_identities(runs), identity)

        changed_build = {**identity, "app_sha256": "different"}
        runs[1]["before"] = changed_build
        runs[1]["after"] = changed_build
        with self.assertRaisesRegex(ValueError, "marker-on identity differs"):
            pacing.require_comparable_run_identities(runs)

        runs[1]["before"] = dict(identity)
        runs[1]["after"] = changed_build
        with self.assertRaisesRegex(ValueError, "marker-on identity differs"):
            pacing.require_comparable_run_identities(runs)


if __name__ == "__main__":
    unittest.main()
