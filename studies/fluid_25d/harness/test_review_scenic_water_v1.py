"""CPU-only Scenic evidence guards. No renderer, GUI or CUDA launch."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import review_scenic_water_v1 as review


class ScenicReviewTests(unittest.TestCase):
    def test_inventory_rejects_escape_symlink_and_detects_edits(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            leaf = root/"final"
            leaf.mkdir()
            artifact = leaf/"a.png"
            artifact.write_bytes(b"pixels")
            first = review.artifact_inventory(root,["final"],[])
            self.assertEqual(set(first),{"final/a.png"})
            artifact.write_bytes(b"other")
            self.assertNotEqual(first,review.artifact_inventory(root,["final"],[]))
            for path in ("../outside",str(artifact),"missing"):
                with self.assertRaises(ValueError): review.safe_artifact(root,path)
            (leaf/"link").symlink_to(artifact)
            with self.assertRaises(ValueError): review.artifact_inventory(root,["final"],[])

    def test_verifier_checks_artifacts_sources_inputs_and_gallery_links(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root/"index.html").write_text("<a href='data.json'>proof</a>")
            (root/"data.json").write_text("{}")
            seal = {"schema":"cubey.fluid25d.scenic-review.v1","leaves":[],
                    "files":["index.html","data.json"],"runtime":{},"source_files":{"code":"sha"},
                    "inputs":{"terrain":"sha"},"artifacts":review.artifact_inventory(root,[],["index.html","data.json"])}
            (root/"review-seal.json").write_text(json.dumps(seal))
            with patch.object(review.ref,"runtime_identity",return_value={}), \
                 patch.object(review.ref,"assert_same_runtime"), \
                 patch.object(review,"source_files",return_value=seal["source_files"]), \
                 patch.object(review.ref,"frozen_input_identity",return_value=seal["inputs"]):
                self.assertEqual(review.verify_review(root)["status"],"PASS")
                with patch.object(review,"source_files",return_value={}), self.assertRaisesRegex(ValueError,"source"):
                    review.verify_review(root)
                with patch.object(review.ref,"frozen_input_identity",return_value={}), self.assertRaisesRegex(ValueError,"input"):
                    review.verify_review(root)
                (root/"data.json").write_text("edited")
                with self.assertRaisesRegex(ValueError,"inventory/hash"):
                    review.verify_review(root)

    def test_gallery_is_compact_and_keeps_readable_tradeoff_explicit(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            review.gallery(root,"stills","videos","timings")
            page = (root/"index.html").read_text()
            self.assertEqual(page.count("<video "),2)
            self.assertIn("Readable remains the launcher default",page)
            self.assertIn("decorative, not measured waves",page)
            self.assertIn("not evidence of a permanent calm lake",page)
            with self.assertRaises(FileExistsError): review.gallery(root,"stills","videos","timings")
            with self.assertRaises(ValueError): review.gallery(root,"../escape","videos","timings")


if __name__ == "__main__":
    unittest.main()
