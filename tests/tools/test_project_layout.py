"""Project hygiene and current startup boundary; no GPU/native imports."""
import os
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

    def test_cloud_reference_requires_explicit_study_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "projects").mkdir()
            projects = root / "projects/CMakeLists.txt"
            projects.write_text("add_subdirectory(atmosphere)\n")
            cmake = root / "CMakeLists.txt"
            valid = '''option(CUBEY_BUILD_CLOUD_STUDIES "Reference only" OFF)
if (CUBEY_BUILD_CLOUD_STUDIES)
    add_subdirectory(studies/cloud/terrainengine)
endif()
'''
            cmake.write_text(valid)
            self.assertEqual(check.cloud_reference_errors(root), [])
            cmake.write_text(valid.replace('only" OFF', 'only" ON'))
            self.assertEqual(len(check.cloud_reference_errors(root)), 1)
            cmake.write_text(valid.replace("if (CUBEY_BUILD_CLOUD_STUDIES)", "if (CUBEY_BUILD_PROJECTS)"))
            self.assertEqual(len(check.cloud_reference_errors(root)), 1)
            cmake.write_text(valid)
            projects.write_text("add_subdirectory(cloud_ref)\n")
            (root / "projects/cloud_ref").mkdir()
            (root / "projects/cloud_ref/CMakeLists.txt").touch()
            self.assertEqual(len(check.cloud_reference_errors(root)), 2)

    def test_relocations_allow_aliases_but_require_final_targets(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "current").touch()
            (root / "intermediate").touch()
            self.assertEqual(check.relocation_errors(root, {
                "original": "current", "intermediate": "current"}), [])
            self.assertEqual(len(check.relocation_errors(root, {
                "original": "intermediate", "intermediate": "current"})), 1)
            self.assertEqual(len(check.relocation_errors(root, {
                "original": "missing", "current": "current"})), 2)

    def test_reference_helpers_fail_fast_without_a_built_executable(self):
        root = Path(__file__).resolve().parents[2]
        scripts = list((root / "studies/cloud/terrainengine/tools").glob("capture*.sh"))
        scripts += [root / "projects/atmosphere/tools" / name for name in (
            "capture_cloud_ref_parity.sh", "capture_cloud_sampling_perf.sh",
            "profile_cloud_vs_ref.sh")]
        self.assertEqual(len(scripts), 6)
        with tempfile.TemporaryDirectory() as tmp:
            absent = str(Path(tmp) / "not-built")
            env = dict(os.environ, APP=absent, CLOUD_REF_APP=absent,
                       ATMO_APP=absent, ATMOSPHERE_APP=absent)
            for script in scripts:
                with self.subTest(script=script.name):
                    subprocess.run(["bash", "-n", str(script)], check=True)
                    output = Path(tmp) / script.stem
                    result = subprocess.run(["bash", str(script), str(output)],
                                            env=env, capture_output=True, text=True, timeout=5)
                    self.assertEqual(result.returncode, 1)
                    self.assertIn("missing", result.stderr)
                    self.assertIn("dev-cloud-studies", result.stderr)
                    self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
