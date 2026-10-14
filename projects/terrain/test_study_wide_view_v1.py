import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import study_wide_view_v1 as study


class StudyTests(unittest.TestCase):
    def stats(self):
        return {"rows": [{"id": name, "site_center_xz_m": [100, 200],
                          "boundary_focus_xz_m": [300, 400]}
                         for name in {v[0] for v in study.CASES.values()}]}

    def test_matrix_and_headless_camera_lock(self):
        self.assertEqual(sum(map(len, study.MATRIX.values())), 29)
        for case, variants in study.MATRIX.items():
            reference = study.command(case, "default", self.stats())
            self.assertIn("--headless", reference)
            self.assertIn("--no-terrain-water-animate", reference)
            for variant in variants:
                self.assertEqual(study.command(case, variant, self.stats()),
                                 reference + study.VARIANTS[variant])
        boundary = study.command("boundary32", "default", self.stats())
        self.assertEqual(boundary[boundary.index("--terrain-water-focus-x") + 1], "300")
        self.assertEqual(boundary[boundary.index("--terrain-water-detail-focus-x") + 1], "100")

    def test_shadow_ablation_keeps_geometry_and_water(self):
        self.assertEqual(study.VARIANTS["no-shadow"],
                         ["--terrain-water-shadow-strength", "0"])
        self.assertNotIn("--no-terrain-water-visible", study.VARIANTS["no-shadow"])
        self.assertEqual(study.VARIANTS["uniform"],
                         ["--terrain-water-detail", "uniform-control"])

    def test_freeze_rejects_input_harness_and_runtime_drift(self):
        with tempfile.TemporaryDirectory(prefix="cubey-wide-view-test-") as folder:
            out = Path(folder)
            (out / "protocol.json").write_text(json.dumps({
                "inputs": {"field": "abc"}, "harness": "def", "runtime": {"app": "ghi"}}))
            with patch.object(study.review, "identities", return_value={"field": "abc"}), \
                 patch.object(study.review.render, "digest", return_value="def"), \
                 patch.object(study.review.render, "runtime", return_value={"app": "ghi"}):
                study.verify(out)
                with patch.object(study.review, "identities", return_value={"field": "changed"}):
                    with self.assertRaises(ValueError): study.verify(out)
                with patch.object(study.review.render, "digest", return_value="changed"):
                    with self.assertRaises(ValueError): study.verify(out)
                with patch.object(study.review.render, "runtime", return_value={"app": "changed"}):
                    with self.assertRaises(ValueError): study.verify(out)
                    study.verify(out, runtime=False)


if __name__ == "__main__":
    unittest.main()
