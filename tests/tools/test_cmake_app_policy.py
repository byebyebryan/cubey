"""Exercise application policy through real CMake configurations; no Vulkan build."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CMAKE = "cmake"
CTEST = "ctest"


class AppPolicyTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cubey policy ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / "build"
        for directory in ("apps/active", "shared runtime", "studies/optional"):
            (self.root / directory).mkdir(parents=True)
        (self.root / "apps/active/main.cpp").write_text("int main() { return 0; }\n")
        (self.root / "shared runtime/runtime.cpp").write_text("void runtime() {}\n")
        (self.root / "shared runtime/runtime.h").write_text("void runtime();\n")
        (self.root / "studies/optional/main.cpp").write_text("// VkRenderPass\n")
        (self.root / "apps/active/CMakeLists.txt").write_text('''cubey_add_app_target(active
    SOURCES main.cpp "${CMAKE_SOURCE_DIR}/shared runtime/runtime.cpp")
''')
        (self.root / "studies/optional/CMakeLists.txt").write_text(
            "cubey_add_app_target(optional SOURCES main.cpp)\n")
        (self.root / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.21)
project(PolicyFixture LANGUAGES CXX)
enable_testing()
add_library(cubey_fixture INTERFACE)
add_library(cubey::cubey ALIAS cubey_fixture)
add_library(cubey::host ALIAS cubey_fixture)
add_library(cubey_project_warnings INTERFACE)
include("{ROOT.as_posix()}/cmake/CubeyApps.cmake")
include("{ROOT.as_posix()}/cmake/CubeyRepositoryTests.cmake")
option(ACTIVE "Enable active frontend" ON)
option(STUDY "Enable optional study" OFF)
if(ACTIVE)
    add_subdirectory(apps/active)
endif()
if(STUDY)
    add_subdirectory(studies/optional)
endif()
cubey_add_render_app_policy_test()
''')

    def configure(self, *options):
        result = subprocess.run([CMAKE, "-S", str(self.root), "-B", str(self.build), *options],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return json.loads(subprocess.check_output(
            [CTEST, "--test-dir", str(self.build), "--show-only=json-v1"], text=True))["tests"]

    def run_policy(self, expected):
        result = subprocess.run([CTEST, "--test-dir", str(self.build), "--output-on-failure"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode == 0, expected, result.stdout + result.stderr)

    def test_frontend_and_shared_implementation_are_scanned(self):
        tests = self.configure()
        self.assertEqual([test["name"] for test in tests], ["render_apps_use_dynamic_rendering"])
        command = tests[0]["command"]
        self.assertIn(str(self.root / "apps/active"), command)
        self.assertIn(str(self.root / "shared runtime"), command)
        self.assertNotIn(str(self.root / "studies/optional"), command)
        self.run_policy(expected=True)
        (self.root / "shared runtime/runtime.h").write_text("// VkFramebuffer\n")
        self.run_policy(expected=False)

    def test_study_only_build_scans_the_enabled_study(self):
        tests = self.configure("-DACTIVE=OFF", "-DSTUDY=ON")
        command = tests[0]["command"]
        self.assertIn(str(self.root / "studies/optional"), command)
        self.assertNotIn(str(self.root / "apps/active"), command)
        self.run_policy(expected=False)

    def test_no_application_targets_registers_no_render_scan(self):
        self.assertEqual(self.configure("-DACTIVE=OFF"), [])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default=CMAKE)
    parser.add_argument("--ctest", default=CTEST)
    args, remaining = parser.parse_known_args()
    CMAKE, CTEST = args.cmake, args.ctest
    unittest.main(argv=[__file__, *remaining])
