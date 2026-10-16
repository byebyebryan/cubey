import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import review_landscape_hardening_v1 as r


class ReviewTests(unittest.TestCase):
    def test_driver_identity_is_frozen(self):
        with patch.object(r.base, "verify", return_value={"driver_sha256": "frozen"}), \
             patch.object(r.base.render, "digest", return_value="changed"):
            with self.assertRaisesRegex(ValueError, "driver changed"):
                r.verify(Path("unused"))

    def test_uniform_low_budget_and_explicit_controls(self):
        args = r.apply_options("uniform")
        self.assertEqual(args[args.index("--terrain-water-vertex-budget") + 1], "2000000")
        self.assertIn("--no-terrain-water-context", args)
        self.assertIn("--no-terrain-water-animate", args)
        self.assertIn("--terrain-water-detail-focus-x", args)
        self.assertEqual(r.apply_options("fixed")[3], "4500000")

    def test_duplicate_receipts_cannot_qualify(self):
        with tempfile.TemporaryDirectory(prefix="hardening-review-test-") as directory:
            out = Path(directory)
            (out / "compatibility.json").write_text(json.dumps({"rows": [
                {"case": "partial-auto"} for _ in r.CASES]}))
            with patch.object(r, "verify", return_value={}):
                with self.assertRaisesRegex(ValueError, "compatibility qualification"):
                    r.qualification(out)

    def test_required_tests_cannot_be_omitted(self):
        with tempfile.TemporaryDirectory(prefix="hardening-review-test-") as directory:
            out = Path(directory)
            (out / "compatibility.json").write_text(json.dumps({"rows": [{"case": c} for c in r.CASES]}))
            (out / "regression-tests.xml").write_text('<testsuite><testcase name="one"/></testsuite>')
            with patch.object(r, "verify", return_value={"required_tests": ["one", "two"]}):
                with self.assertRaisesRegex(ValueError, "required groups"):
                    r.qualification(out)


if __name__ == "__main__":
    unittest.main()
