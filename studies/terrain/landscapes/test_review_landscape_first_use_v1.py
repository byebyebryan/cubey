import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import review_landscape_first_use_v1 as r


class FirstUseTests(unittest.TestCase):
    def test_driver_identity_guard(self):
        with patch.object(r.hard, "verify", return_value={"first_use_driver_sha256": "frozen"}), \
             patch.object(r.base.render, "digest", return_value="changed"):
            with self.assertRaisesRegex(ValueError, "driver changed"):
                r.verify(Path("unused"))

    def test_hidden_is_same_camera_not_dry_reload(self):
        shown = r.view_command("overview")
        hidden = r.view_command("hidden")
        self.assertEqual(hidden[:-1], shown)
        self.assertEqual(hidden[-1], "--no-terrain-water-visible")
        self.assertNotIn("mountain-dry", hidden)

    def test_detail_and_wide_remain_explicit(self):
        detail = r.view_command("detail")
        self.assertEqual(detail[detail.index("--terrain-water-span") + 1], "6500")
        wide = r.view_command("wide")
        self.assertIn("mountain-wide", wide)
        self.assertEqual(wide[wide.index("--terrain-water-span") + 1], "45000")

    def test_missing_view_blocks_report(self):
        with tempfile.TemporaryDirectory(prefix="terrain-first-use-test-") as directory:
            out = Path(directory)
            for name, value in {"capture.json": {"views": []}, "performance.json": {"rows": []},
                                "first-use.json": {}, "lifecycle.json": {}, "negative.json": {}}.items():
                (out / name).write_text(json.dumps(value))
            with patch.object(r, "verify", return_value={}), patch.object(r.hard, "qualification"):
                with self.assertRaisesRegex(ValueError, "capture views"):
                    r.qualify(out)


if __name__ == "__main__":
    unittest.main()
