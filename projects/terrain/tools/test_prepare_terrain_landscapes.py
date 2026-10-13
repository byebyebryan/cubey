import json
from pathlib import Path
import tempfile
import unittest
import prepare_terrain_landscapes as p


class PrepareTests(unittest.TestCase):
    def test_copy_and_exact_reuse(self):
        with tempfile.TemporaryDirectory(prefix="landscape-prepare-test-") as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            (source / "payload").write_bytes(b"retained")
            target = root / "cache/package"
            def validate(folder):
                if (folder / "payload").read_bytes() != b"retained":
                    raise ValueError("changed")
            self.assertEqual(p.copy_once(source, target, validate), "installed")
            self.assertEqual(p.copy_once(source, target, validate), "reused")
            (target / "payload").write_bytes(b"user-change")
            with self.assertRaises(ValueError):
                p.copy_once(source, target, validate)
            self.assertEqual((target / "payload").read_bytes(), b"user-change")

    def test_bad_source_never_published(self):
        with tempfile.TemporaryDirectory(prefix="landscape-prepare-test-") as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            def reject(folder):
                raise ValueError("bad source")
            with self.assertRaises(ValueError):
                p.copy_once(source, root / "target", reject)
            self.assertFalse((root / "target").exists())

    def test_paths_and_duplicate_keys(self):
        with tempfile.TemporaryDirectory(prefix="landscape-prepare-test-") as directory:
            root = Path(directory)
            for name in ("../escape", "/tmp/escape", ""):
                with self.assertRaises(ValueError):
                    p.child(root, name)
            (root / "cache").mkdir()
            (root / "cache/link").symlink_to(root)
            with self.assertRaises(ValueError):
                p.child(root / "cache", "link/escape")
            path = root / "metadata.json"
            path.write_text('{"key":1,"key":2}')
            with self.assertRaises(ValueError):
                p.read(path)

    def test_payload_checks_hash_and_size(self):
        with tempfile.TemporaryDirectory(prefix="landscape-prepare-test-") as directory:
            root = Path(directory)
            path = root / "payload"
            path.write_bytes(b"1234")
            record = {"path": "payload", "byte_count": 4, "sha256": p.digest(path)}
            p.fields(root, [record])
            path.write_bytes(b"4321")
            with self.assertRaises(ValueError):
                p.fields(root, [record])
            path.write_bytes(b"0")
            with self.assertRaises(ValueError):
                p.fields(root, [record])


if __name__ == "__main__":
    unittest.main()
