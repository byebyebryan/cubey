"""Project hygiene and current startup boundary; no GPU/native imports."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import check_project_layout as check


class LayoutRules(unittest.TestCase):
    def test_ad_hoc_roots_rejected_but_cpp_tests_allowed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            folder = root / "projects/example"
            folder.mkdir(parents=True)
            (folder / "README.md").write_text("# Example")
            (folder / "example_tests.cpp").write_text("// test")
            self.assertEqual(check.project_errors(root, folder), [])
            (folder / "review_v17.py").write_text("# one-off")
            (folder / "next_pass.md").write_text("# notes")
            self.assertEqual(len(check.project_errors(root, folder)), 2)

    def test_supported_entrypoint_is_explicit(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            folder = root / "projects/terrain_water"
            folder.mkdir(parents=True)
            (folder / "README.md").write_text("# Terrain Water")
            (folder / "run_terrain_rain_demo.py").write_text("# launcher")
            self.assertEqual(check.project_errors(root, folder), [])

    def test_links_check_targets_anchors_and_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "README.md"
            source.write_text("[ok](target.md#hello) [missing](missing.md) [bad](target.md#absent) [escape](../outside)")
            (root / "target.md").write_text("# Hello")
            self.assertEqual(len(check.link_errors(root, source)), 3)

    def test_optional_generated_outputs_are_not_required(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "README.md"
            source.write_text("[optional](outputs/missing/index.html) [site](https://example.org)")
            self.assertEqual(check.link_errors(root, source), [])

    def test_launcher_imports_no_studies_or_optional_runtime(self):
        root = Path(__file__).resolve().parents[2]
        code = """import sys
from projects.terrain_water import run_terrain_rain_demo
bad = [getattr(m, "__file__", "") for m in tuple(sys.modules.values())
       if "/studies/" in str(getattr(m, "__file__", ""))]
assert not bad, bad
assert "numpy" not in sys.modules
assert "synxflow" not in sys.modules
"""
        subprocess.run([sys.executable, "-B", "-c", code], cwd=root, check=True)


if __name__ == "__main__":
    unittest.main()
