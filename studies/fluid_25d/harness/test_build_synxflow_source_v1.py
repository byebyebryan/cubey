"""CPU-only checks for explicit immutable source-artifact selection."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from build_synxflow_source_v1 import GITHUB_COMMIT, SDIST_SHA256, SOURCE_PINS


class SourcePinTests(unittest.TestCase):
    def test_equal_versions_have_distinct_explicit_pins(self):
        self.assertEqual(SOURCE_PINS["pypi-1.0.1"], (SDIST_SHA256, "synxflow-1.0.1"))
        self.assertEqual(SOURCE_PINS["github-1.0.1"][1], "SynxFlow-" + GITHUB_COMMIT)
        self.assertNotEqual(SOURCE_PINS["github-1.0.1"][0], SDIST_SHA256)
        for checksum, directory in SOURCE_PINS.values():
            self.assertEqual(len(checksum), 64)
            self.assertTrue(all(character in "0123456789abcdef" for character in checksum))
            self.assertNotIn("/", directory)

    def test_wrong_archive_rejected_before_toolchain_or_output_creation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "wrong.tar.gz"
            archive.write_bytes(b"not an approved source artifact")
            for pin in SOURCE_PINS:
                output = root / pin
                result = subprocess.run([
                    sys.executable, str(Path(__file__).with_name("build_synxflow_source_v1.py")),
                    "--sdist", str(archive), "--source-pin", pin, "--cuda-root", str(root / "missing-cuda"),
                    "--python", str(root / "missing-python"), "--out", str(output),
                ], text=True, capture_output=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("explicitly selected SynxFlow1.0.1 pin", result.stderr)
                self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
