import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import review_prepared_landscapes_v1 as r


class ReviewTests(unittest.TestCase):
    def test_named_selection_and_view_controls(self):
        args = r.command("mountain-local", wide=True, water=False)
        self.assertIn("--headless", args)
        self.assertIn("--terrain-landscape", args)
        self.assertNotIn("--terrain-site", args)
        self.assertNotIn("--terrain-heightfield", args)
        self.assertIn("--no-terrain-water-visible", args)
        self.assertEqual(args[args.index("--terrain-water-span") + 1], "45000")

    def test_locked_reference_pose(self):
        with patch.object(r.previous, "pose", return_value=(1234.0, 11457.6)):
            args = r.command("mountain-local", locked=True)
        self.assertEqual(args[args.index("--terrain-water-focus-height")+1], "1234.0")
        self.assertEqual(args[args.index("--terrain-water-span")+1], "11457.6")

    def test_frozen_runtime_and_assets(self):
        with tempfile.TemporaryDirectory(prefix="landscape-review-test-") as directory:
            out = Path(directory)
            (out/"protocol.json").write_text(json.dumps({"runtime":{"app":"frozen"},
                "inputs":{"field":"frozen"},"harness_sha256":"helper"}))
            with patch.object(r.render,"runtime",return_value={"app":"frozen"}), \
                 patch.object(r,"identities",return_value={"field":"frozen"}), \
                 patch.object(r.render,"digest",return_value="helper"):
                r.verify(out)
                with patch.object(r,"identities",return_value={"field":"changed"}):
                    with self.assertRaises(ValueError):
                        r.verify(out)

    def test_report_rejects_duplicate_timing_slots(self):
        with tempfile.TemporaryDirectory(prefix="landscape-review-test-") as directory:
            out = Path(directory)
            values = {"capture.json":{}, "lifecycle.json":{}, "negative.json":{},
                "performance.json":{"rows":[{"id":"mountain-local","repeat":0,"pass":True}]*8}}
            for name, value in values.items():
                (out/name).write_text(json.dumps(value))
            with patch.object(r,"verify",return_value={}):
                with self.assertRaisesRegex(ValueError,"performance qualification"):
                    r.report(out)


if __name__ == "__main__":
    unittest.main()
