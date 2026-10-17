"""Portable committed-media audit; no GPU, terrain cache, solver or network."""
import hashlib
import json
from pathlib import Path
import sys
import unittest

from package_landscape_showcase_v1 import check_video

ROOT = Path(__file__).resolve().parents[2]
MEDIA = ROOT / "docs/media/showcase"
sys.path.insert(0, str(ROOT / "tools"))
from repo_docs import media_errors


class ShowcaseMedia(unittest.TestCase):
    def setUp(self):
        manifest = json.loads((MEDIA / "manifest.json").read_bytes())
        self.entry = next(e for e in manifest["entries"] if e["id"] == "gltf-terrain")
        self.recipe = json.loads((ROOT / self.entry["publication"]["recipe"]).read_bytes())

    def test_current_media_integrity_and_faststart(self):
        self.assertEqual(self.recipe["schema"], "cubey.gltf.saved-water-showcase.v2")
        for name, descriptor in self.recipe["media"].items():
            path = ROOT / descriptor["path"]
            self.assertEqual(path.name, name)
            expected = dict(descriptor)
            if path.suffix == ".png":
                expected.update(width=1280, height=720)
            self.assertEqual(media_errors(ROOT, expected), [])
        for key, name in (("publication", "gltf-terrain.mp4"), ("poster", "gltf-terrain.png"),
                          ("master", "gltf-terrain-master-1080p.mp4")):
            self.assertEqual(self.entry[key]["sha256"], self.recipe["media"][name]["sha256"])
        check_video(self.recipe["master"]["probe"], 1920, 1080)
        check_video(self.recipe["gallery"]["probe"], 1280, 720)
        self.assertEqual(self.recipe["poster"]["timestamp_seconds"], 4)
        self.assertEqual(self.recipe["gallery"]["derived_from"], "gltf-terrain-master-1080p.mp4")

    def test_clean_source_and_approved_all_frame_parity(self):
        protocol = self.recipe["source_protocol"]
        self.assertEqual(protocol["source_status_sha256"], hashlib.sha256(b"").hexdigest())
        self.assertEqual(self.entry["source"]["revision"], protocol["source_head"])
        self.assertTrue(self.recipe["approved_frame_parity"]["all_480_frames_exact"])
        self.assertEqual(self.recipe["approved_frame_parity"]["fresh"], self.recipe["approved_frame_parity"]["approved"])
        command = self.recipe["source_capture"]["command"]
        for flag in ("--headless", "--require-validation", "--terrain-water-audit", "--no-clouds"):
            self.assertIn(flag, command)
        self.assertEqual(command[command.index("--terrain-landscape") + 1], "mountain-local")
        self.assertEqual(command[command.index("--terrain-landscape-depth") + 1], "fitted")
        self.assertEqual(self.recipe["scene"]["hydraulic_dispatches"], 0)
        self.assertIn("held", self.recipe["scene"]["water_state"])
        scene = self.recipe["scene"]
        self.assertEqual(scene["orbit_degrees"], 45)
        self.assertEqual(scene["camera_start_yaw_degrees"], 0)
        self.assertEqual(scene["solar_start_hours"], 14)
        self.assertEqual(scene["solar_speed_hours_per_second"], .6875)
        self.assertEqual(scene["solar_last_frame_hours"], 19.5)
        self.assertTrue(scene["automatic_exposure"])
        self.assertNotIn("--exposure", command)
        self.assertEqual(command[command.index("--star-intensity") + 1], "0")

    def test_legacy_publication_is_not_claimed_for_current_bytes(self):
        self.assertIn("published", self.entry["publication"]["state"])
        legacy = json.loads((ROOT / self.entry["legacy"]["recipe"]).read_bytes())
        self.assertEqual(legacy["entry"]["publication"]["sha256"],
                         "936661a15f5b49b915d5a09be8a0ae061e4a14b6a8a0b254f28268c285bb7bc2")
        self.assertEqual(legacy["entry"]["publication"]["github_attachment_url"],
                         self.entry["legacy"]["github_attachment_url"])
        root = (ROOT / "README.md").read_text()
        self.assertIn("](docs/media/showcase/gltf-terrain.mp4)", root)
        self.assertIn("\n" + self.entry["publication"]["github_attachment_url"] + "\n", root)
        self.assertNotEqual(self.entry["publication"]["github_attachment_url"],
                            self.entry["legacy"]["github_attachment_url"])
        self.assertNotIn("\n" + self.entry["legacy"]["github_attachment_url"] + "\n", root)
        self.assertEqual(self.entry["license"], legacy["entry"]["license"])
        self.assertEqual(self.recipe["license"], self.entry["license"])
        self.assertEqual(self.entry["license"]["media"], "CC-BY-NC-4.0")

    def test_publication_receipt_matches_current_bytes_and_documents(self):
        publication = self.entry["publication"]
        receipt = json.loads((ROOT / publication["receipt"]).read_bytes())
        self.assertEqual(receipt["schema"], "cubey.gltf.saved-water-publication.v2")
        self.assertEqual(receipt["source_render_revision"],
                         self.recipe["source_protocol"]["source_head"])
        for key in ("bytes", "sha256", "github_attachment_url", "provenance_comment_url"):
            self.assertEqual(receipt[key], publication[key])
        self.assertEqual(receipt["media_path"], publication["path"])
        self.assertEqual(receipt["capture_recipe"], publication["recipe"])
        self.assertEqual(receipt["capture_recipe_sha256"],
                         hashlib.sha256((ROOT / publication["recipe"]).read_bytes()).hexdigest())
        verification = receipt["verification"]
        public = verification["anonymous_public_page_video"]
        for key in ("bytes", "sha256"):
            self.assertEqual(public[key], publication[key])
        self.assertEqual((public["http_status"], public["content_type"]), (200, "video/mp4"))
        self.assertEqual((public["width"], public["height"], public["frames"], public["fps"],
                          public["duration_seconds"]), (1280, 720, 480, "60/1", 8))
        browser = verification["browser_native_player"]
        self.assertIsNone(browser["media_error"])
        self.assertEqual(browser["download_bytes"], publication["bytes"])
        self.assertEqual(browser["download_sha256"], publication["sha256"])
        for key in ("issue_remains_closed", "original_four_attachment_mappings_unchanged",
                    "fluid_publication_comment_retained", "morning_publication_comment_retained"):
            self.assertTrue(verification[key])
        for path in (ROOT / "README.md", ROOT / "projects/gltf_viewer/README.md",
                     MEDIA / "README.md"):
            self.assertIn(publication["github_attachment_url"], path.read_text())

    def test_morning_recipe_and_receipt_are_retained_as_exact_history(self):
        previous = self.entry["previous_saved_water"]
        for path_key, hash_key in (("recipe", "recipe_sha256"),
                                   ("publication_receipt", "receipt_sha256")):
            self.assertEqual(hashlib.sha256((ROOT / previous[path_key]).read_bytes()).hexdigest(),
                             previous[hash_key])
        old = json.loads((ROOT / previous["recipe"]).read_bytes())
        receipt = json.loads((ROOT / previous["publication_receipt"]).read_bytes())
        self.assertEqual(old["schema"], "cubey.gltf.saved-water-showcase.v1")
        self.assertEqual(old["scene"]["solar_hours"], 10)
        self.assertEqual(old["scene"]["orbit_degrees"], 20)
        self.assertEqual(receipt["sha256"],
                         "8734ec1b86a2390947ad960e0481015aa24f0b218a7f6f4f8f95bcd416fe8233")
        self.assertEqual(previous["github_attachment_url"], receipt["github_attachment_url"])
        self.assertNotIn("\n" + previous["github_attachment_url"] + "\n",
                         (ROOT / "README.md").read_text())
        self.assertEqual(old["license"], self.recipe["license"])


if __name__ == "__main__":
    unittest.main()
