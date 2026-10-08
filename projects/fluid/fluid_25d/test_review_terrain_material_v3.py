"""Hermetic guards for terrain audition provenance and compact review."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import review_terrain_material_v3 as review
import run_mountain_rain_demo as demo


class TerrainReviewTests(unittest.TestCase):
    def test_sweep_flags_are_video_only_and_heading_is_independent(self):
        for kind in ("still","diagnostic"):
            self.assertEqual(review.camera_args({"kind":kind},.2,.4),
                             ["--fluid25d-native-camera-yaw","0.2"])
        self.assertIn("--fluid25d-native-camera-sweep",review.camera_args({"kind":"video"},None,.4))

    def test_renderer_source_changes_cannot_use_tool_archive_exception(self):
        with tempfile.TemporaryDirectory() as temp:
            with patch.object(review,"source_files",return_value={"renderer.frag":"current"}):
                with self.assertRaises(ValueError):
                    review.check_phase_sources(Path(temp),{"renderer.frag":"old"})

    def test_phase_labels_cannot_escape_owned_output(self):
        self.assertEqual(review.checked_label("candidate-final"), "candidate-final")
        for label in ("../escape", "/tmp/escape", "nested/leaf", "", "MixedCase", "."):
            with self.assertRaises(ValueError):
                review.checked_label(label)

    def rows(self):
        return [{"height": height, "batch": batch, "material": material,
                 "summary": {"measured_gpu_span_count":108,"gpu_median_ms":.3+extra,
                             "gpu_p95_ms":.6+extra}}
                for height in (720,1080) for batch in range(3)
                for material,extra in (("refined",0),("terrain",.1))]

    def test_incremental_budgets_require_complete_matched_batches(self):
        rows = self.rows()
        self.assertEqual(len(review.performance(rows)),2)
        with self.assertRaises(ValueError):
            review.performance(rows[:-1])
        rows[1]["summary"]["gpu_p95_ms"] = 8
        with self.assertRaises(ValueError):
            review.performance(rows)

    def test_inventory_detects_changes_and_symlinks(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            (out/"capture.png").write_bytes(b"first")
            old = review.inventory(out)
            (out/"capture.png").write_bytes(b"second")
            self.assertNotEqual(old, review.inventory(out))
            (out/"link").symlink_to(out/"capture.png")
            with self.assertRaises(ValueError):
                review.inventory(out)

    def test_verifier_rejects_tampered_artifacts(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            (out/"index.html").write_text("<video src='a.mp4'></video><video src='b.mp4'></video><a href='c.mp4'></a><a href='d.mp4'></a>")
            for name in ("a.mp4","b.mp4","c.mp4","d.mp4"):
                (out/name).write_bytes(b"clip")
            seal = {"schema":"cubey.fluid25d.terrain-review.v3","artifacts":review.inventory(out),
                    "runtime_identity":{},"source_files":{}}
            (out/"review-seal.json").write_text(json.dumps(seal))
            with patch.object(review.ref,"runtime_identity",return_value={}), \
                 patch.object(review.ref,"assert_same_runtime"), patch.object(review,"source_files",return_value={}):
                self.assertEqual(review.verify(out)["status"],"PASS")
                (out/"a.mp4").write_bytes(b"tampered")
                with self.assertRaises(ValueError):
                    review.verify(out)

    def test_launcher_keeps_defaults_and_exposes_opt_in_terrain(self):
        default = demo.parser().parse_args(["replay"])
        self.assertEqual(default.style,"readable")
        command = demo.commands(default,Path("unused"))["viewer"]
        self.assertNotIn("--fluid25d-scenic-material",command)
        study = demo.parser().parse_args(["replay","--style","scenic","--material","terrain"])
        command = demo.commands(study,Path("unused"))["viewer"]
        self.assertEqual(command[command.index("--fluid25d-scenic-material")+1],"terrain")
        with self.assertRaises(ValueError):
            demo.commands(demo.parser().parse_args(["replay","--material","terrain"]),Path("unused"))


if __name__ == "__main__":
    unittest.main()
