"""Report gates must not confuse a completed worker with a healthy live viewer."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

import review_live_perf_terrain_v1 as review


class Review(unittest.TestCase):
    def test_completed_trial_is_not_frame_budget_acceptance(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            row = {"mode": "live", "physical_wall_ratio": 60, "measurement_final_physical_s": 7200,
                   "cleanup": [{"active_group_remaining": [], "escalated": False}],
                   "completion": {"inputs_unchanged": True, "reference_inputs_unchanged": True},
                   "profile": {"frame_ms": {"p95": 50}, "metrics": {
                       "fluid_25d.external.publication_freshness_s": {"p95": .2},
                       "fluid_25d.external.advancing_state_age_s": {"p95": .2},
                       "fluid_25d.external.physical_time_s": {"max": 7170}}}}
            gates = review.gates(row, root)
            self.assertTrue(gates["pace_at_least_54x"])
            self.assertFalse(gates["frame_p95_at_most_16_7ms"])
            self.assertTrue(gates["display_near_worker_endpoint"])
            (root / "viewer.log").write_text("VIEWER ERROR: lost producer")
            row["profile"]["metrics"]["fluid_25d.external.physical_time_s"]["max"] = 0
            gates = review.gates(row, root)
            self.assertTrue(gates["viewer_reported_failure"])
            self.assertFalse(gates["display_near_worker_endpoint"])
            (root / "viewer.log").write_text("fluid_25d: backend adapter: dem_asc is missing provenance")
            self.assertTrue(review.gates(row, root)["viewer_reported_failure"])

    def test_progress_plot_distinguishes_scope_and_escapes_labels(self):
        rows = [{"case": "mountain-patch-1024-16m", "mode": "worker-only",
                 "samples": [{"wall_s": 120, "physical_s": 7200}]}]
        svg = review.progress_plot(rows, "mountain")
        self.assertIn("stroke-dasharray", svg)
        self.assertIn("7200 physical s", svg)
        self.assertEqual(review.progress_plot(rows, "alpine"), "")

    def test_missing_evidence_is_not_an_acceptance_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            row = {"mode": "live"}
            result = review.gates(row, Path(temporary))
            self.assertIsNone(result["clean_owned_shutdown"])
            self.assertIsNone(result["warm_ack_p95_at_most_250ms"])
            self.assertIsNone(result["viewer_reported_failure"])
            row["warm_ack_ms"] = {"p95": 251}
            self.assertFalse(review.gates(row, Path(temporary))["warm_ack_p95_at_most_250ms"])
            row["warm_ack_ms"]["p95"] = 249
            self.assertTrue(review.gates(row, Path(temporary))["warm_ack_p95_at_most_250ms"])

    def test_held_renderer_is_not_expected_to_follow_contention_worker(self):
        with tempfile.TemporaryDirectory() as temporary:
            row = {"mode": "renderer-plus-worker", "measurement_final_physical_s": 100,
                   "profile": {"metrics": {"fluid_25d.external.physical_time_s": {"max": 7200}}}}
            self.assertIsNone(review.gates(row, Path(temporary))["display_near_worker_endpoint"])
            row["mode"] = "live"
            self.assertFalse(review.gates(row, Path(temporary))["display_near_worker_endpoint"])

    def test_matrix_keeps_missing_and_incomplete_trials_visible(self):
        rows = [{"case": "mountain-patch-2048-8m", "mode": "live", "status": "timeout",
                 "physical_wall_ratio": 8.3, "measurement_final_physical_s": 5064}]
        matrix = review.coverage_matrix(rows)
        self.assertIn("timeout", matrix)
        self.assertIn("5064", matrix)
        self.assertIn("pending", matrix)
        self.assertIn("32.768 km", matrix)
        self.assertEqual(matrix.count("<tr>"), 16)

    def test_report_curation_preserves_trial_receipts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            trial = root / "coverage/mountain-patch-1024-16m-live-0"
            trial.mkdir(parents=True)
            row = {"case": "mountain-patch-1024-16m", "mode": "live", "status": "completed",
                   "errors": [], "captures": [{"path": "final.png", "observed_physical_s": 7200}]}
            raw = json.dumps(row).encode()
            (trial / "result.json").write_bytes(raw)
            with contextlib.redirect_stdout(io.StringIO()):
                review.render(root)
            page = (root / "index.html").read_text()
            self.assertIn("Start here: the three 1k terrain options", page)
            self.assertIn("<details><summary>coverage", page)
            self.assertIn("All other live captures · 0", page)
            self.assertIn("No physical-monitor FPS claim", page)
            self.assertIn("Study still in progress", page)
            self.assertEqual((trial / "result.json").read_bytes(), raw)
            (root / "verdict.json").write_text(json.dumps({"summary": "Keep <script>literal text</script>",
                                                           "findings": ["measured, not universal"]}))
            with contextlib.redirect_stdout(io.StringIO()):
                review.render(root)
            page = (root / "index.html").read_text()
            self.assertIn("Review verdict", page)
            self.assertIn("&lt;script&gt;literal text&lt;/script&gt;", page)
            self.assertNotIn("Keep <script>", page)
            (root / "study-completion.json").write_text('{"execution_review_complete":true}')
            with contextlib.redirect_stdout(io.StringIO()):
                review.render(root)
            self.assertIn("Study execution and review complete", (root / "index.html").read_text())

    def test_isolation_uses_repeats_not_pooled_frame_claims(self):
        rows = [{"case": "mountain", "mode": mode,
                 "profile": {"frame_ms": {"p95": value}, "passes_ms": {
                     "gpu:fluid_25d native presentation total": {"p95": value}}}}
                for mode, value in (("renderer-only", 2), ("renderer-only", 4),
                                    ("renderer-plus-worker", 4), ("renderer-plus-worker", 8))]
        text = review.isolation_table(rows)
        self.assertIn("3.00 [2.00–4.00] / n=2", text)
        self.assertIn("+100.0%", text)
        self.assertIn("not isolated bridge overhead", text)
        self.assertEqual(review.isolation_table([]), "")
        rows[0]["_directory"] = "other-display-density/one"
        text = review.isolation_table(rows)
        self.assertIn("other-display-density / mountain", text)
        self.assertIn("not measured", text)

    def test_held_capture_is_never_labelled_live(self):
        row = {"case": "mountain", "mode": "renderer-only", "_directory": "display-1x/trial-0",
               "captures": [{"path": "final.png", "observed_physical_s": 7200}]}
        text = review.gallery(row)
        self.assertIn("not a running water simulation", text)
        self.assertNotIn("worker running", text)
        row["mode"] = "live"
        self.assertIn("worker running", review.gallery(row))

    def test_build_comparison_does_not_pool_debug_and_release(self):
        rows = [{"case": "mountain-patch-1024-16m", "mode": "live",
                 "_directory": phase + "/trial", "profile": {"frame_ms": {"p95": value}}}
                for phase, value in (("coverage", 70), ("release-mountain-patch-1024-16m", 50),
                                      ("release-mountain-patch-1024-16m", 54))]
        text = review.build_table(rows)
        self.assertIn("Debug baseline / n=1", text)
        self.assertIn("Release experiment / n=2", text)
        self.assertIn("52.0 [50.0–54.0]", text)
        self.assertIn("not a repeated baseline", text)

    def test_display_comparison_keeps_camera_and_subdivision_separate(self):
        rows = [{"case": "mountain", "mode": "renderer-only", "camera": camera,
                 "surface_subdivision": subdivision, "_directory": "display-test/trial",
                 "identity": {"spec": {"rows": 2048, "cols": 2048}},
                 "profile": {"frame_ms": {"p95": value}}}
                for camera, subdivision, value in (("overview", 1, 40), ("overview", 1, 44),
                                                     ("overview", 2, 50), ("runoff", 1, 60))]
        text = review.display_table(rows)
        self.assertIn("42.00 [40.00–44.00] / n=2", text)
        self.assertIn("8.38 M", text)
        self.assertIn("33.52 M", text)
        self.assertIn("60.00 [60.00–60.00] / n=1", text)
        self.assertIn("derived per display layer", text)
        self.assertEqual(review.isolation_table(rows), "")
        self.assertEqual(review.display_table([]), "")


if __name__ == "__main__":
    unittest.main()
