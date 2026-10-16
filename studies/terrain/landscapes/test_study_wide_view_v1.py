import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import study_wide_view_v1 as study
import review_wide_view_v1 as trial
import report_wide_view_v1 as report


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

    def test_trial_adds_only_explicit_shading_option(self):
        for case in study.CASES:
            before = trial.command(case, "default", self.stats())
            after = trial.command(case, "default", self.stats(), filtered=True)
            self.assertEqual(after, before + ["--terrain-readability", "filtered-study"])

    def test_trial_crop_rejects_bad_dimensions(self):
        with patch.object(trial.base.render, "pixels", return_value=b"bad"):
            with self.assertRaises(ValueError):
                trial.crop_difference(Path("a"), Path("b"), [0, 0, 1, 1])

    def test_report_requires_complete_evidence_and_retains_failed_uniform(self):
        with tempfile.TemporaryDirectory(prefix="cubey-wide-report-test-") as folder:
            out=Path(folder); baseline=out/"baseline";baseline.mkdir()
            controls={(c,"default") for c in study.CASES}|{("wide64","clay-no-shadow"),("wide64","hidden-no-shadow")}
            study.review.write(baseline/"captures.json",{"rows":[{"case":c,"variant":v}
                for c,variants in study.MATRIX.items() for v in variants]})
            study.review.write(out/"captures.json",{"rows":[{"case":c,"variant":v,"filtered":f}
                for c,v in controls for f in (False,True)],"default_parity":[
                    {"case":c,"variant":v,"difference":{"max_abs_8bit":0}} for c,v in controls]})
            perf={"rows":[{"case":c,"variant":v,"filtered":f,"repeat":r,
                "whole_gpu_p95_ms":9 if v=="uniform" else 3,"cpu_service_p95_ms":.4,
                "budget_pass":v!="uniform"} for c,v,f in (("local32","default",False),
                ("local32","default",True),("wide64","default",False),
                ("wide64","default",True),("wide64","uniform",False)) for r in range(2)]}
            study.review.write(out/"performance.json",perf)
            study.review.write(out/"close.json",{"rows":[{"filtered":f} for f in (False,True)]})
            (out/"regression-tests.xml").write_text('<testsuite>'+''.join(
                f'<testcase name="{name}"/>' for name in report.REQUIRED_TESTS)+'</testsuite>')
            with patch.object(trial,"verify",return_value={"baseline":str(baseline)}):
                report.validate(out)
                perf["rows"][0]["whole_gpu_p95_ms"]=8.1
                (out/"performance.json").write_text(json.dumps(perf))
                with self.assertRaises(ValueError): report.validate(out)
                perf["rows"]=perf["rows"][1:]
                (out/"performance.json").write_text(json.dumps(perf))
                with self.assertRaises(ValueError): report.validate(out)

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
