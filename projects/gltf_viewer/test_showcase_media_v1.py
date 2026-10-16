"""Portable committed-media audit; no GPU, terrain cache, solver or network."""
import hashlib
import json
from pathlib import Path
import struct
import unittest

from package_landscape_showcase_v1 import check_video

ROOT = Path(__file__).resolve().parents[2]
MEDIA = ROOT / "docs/media/showcase"


class ShowcaseMedia(unittest.TestCase):
    def setUp(self):
        manifest = json.loads((MEDIA / "manifest.json").read_bytes())
        self.entry = next(e for e in manifest["entries"] if e["id"] == "gltf-terrain")
        self.recipe = json.loads((ROOT / self.entry["publication"]["recipe"]).read_bytes())

    def test_current_media_integrity_and_faststart(self):
        self.assertEqual(self.recipe["schema"], "cubey.gltf.saved-water-showcase.v1")
        for name, descriptor in self.recipe["media"].items():
            path = ROOT / descriptor["path"]
            self.assertEqual(path.name, name)
            payload = path.read_bytes()
            self.assertEqual(len(payload), descriptor["bytes"])
            self.assertEqual(hashlib.sha256(payload).hexdigest(), descriptor["sha256"])
            if path.suffix == ".png":
                self.assertEqual(payload[:8], b"\x89PNG\r\n\x1a\n")
                self.assertEqual(struct.unpack_from(">II", payload, 16), (1280, 720))
            else:
                offset, atoms = 0, []
                while offset + 8 <= len(payload):
                    size, kind = struct.unpack_from(">I4s", payload, offset)
                    self.assertGreaterEqual(size, 8)
                    atoms.append(kind)
                    offset += size
                self.assertLess(atoms.index(b"moov"), atoms.index(b"mdat"))
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
        self.assertEqual(self.entry["source"]["revision"], protocol["source_revision"])
        self.assertTrue(self.recipe["approved_frame_parity"]["all_480_frames_exact"])
        self.assertEqual(self.recipe["approved_frame_parity"]["fresh"], self.recipe["approved_frame_parity"]["approved"])
        command = self.recipe["source_capture"]["command"]
        for flag in ("--headless", "--require-validation", "--terrain-water-audit", "--no-clouds"):
            self.assertIn(flag, command)
        self.assertEqual(command[command.index("--terrain-landscape") + 1], "mountain-local")
        self.assertEqual(command[command.index("--terrain-landscape-depth") + 1], "fitted")
        self.assertEqual(self.recipe["scene"]["hydraulic_dispatches"], 0)
        self.assertIn("held", self.recipe["scene"]["water_state"])
        self.assertEqual(self.recipe["scene"]["orbit_degrees"], 20)

    def test_legacy_publication_is_not_claimed_for_current_bytes(self):
        self.assertIsNone(self.entry["publication"]["github_attachment_url"])
        self.assertIn("pending", self.entry["publication"]["state"])
        legacy = json.loads((ROOT / self.entry["legacy"]["recipe"]).read_bytes())
        self.assertEqual(legacy["entry"]["publication"]["sha256"],
                         "936661a15f5b49b915d5a09be8a0ae061e4a14b6a8a0b254f28268c285bb7bc2")
        self.assertEqual(legacy["entry"]["publication"]["github_attachment_url"],
                         self.entry["legacy"]["github_attachment_url"])
        root = (ROOT / "README.md").read_text()
        self.assertIn("](docs/media/showcase/gltf-terrain.mp4)", root)
        self.assertNotIn("\n" + self.entry["legacy"]["github_attachment_url"] + "\n", root)
        self.assertEqual(self.entry["license"], legacy["entry"]["license"])
        self.assertEqual(self.recipe["license"], self.entry["license"])
        self.assertEqual(self.entry["license"]["media"], "CC-BY-NC-4.0")


if __name__ == "__main__":
    unittest.main()
