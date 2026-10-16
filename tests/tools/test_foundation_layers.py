"""Negative and adapter fixtures for physical foundation include boundaries."""
import argparse
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from check_foundation_layers import check, check_target_graph

CMAKE = "cmake"


class FoundationLayerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cubey layers ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def write(self, path, source=""):
        file = self.root / path
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(source)

    def test_empty_or_incorrect_root_is_not_success(self):
        self.assertTrue(check(self.root)[1])

    def test_cpu_and_render_layer_directions(self):
        self.write("src/cubey/render/example.cpp", "#include <cubey/vulkan/device.h>\n")
        self.assertEqual(check(self.root)[1], [])
        self.write("src/cubey/vulkan/example.cpp", "#include <cubey/render/target.h>\n")
        self.write("src/cubey/asset/example.cpp", "#include <vulkan/vulkan.h>\n")
        self.write("src/cubey/render/example.cpp", "#include <cubey/engine/engine.h>\n")
        errors = check(self.root)[1]
        self.assertEqual(len(errors), 3, errors)
        self.assertTrue(any("vulkan -> render" in e for e in errors))
        self.assertTrue(any("render -> engine" in e for e in errors))
        self.assertTrue(any("CPU layer asset" in e for e in errors))

    def test_scene_recording_adapter_is_narrowly_scoped(self):
        self.write("include/cubey/scene/render_recording.h", "#include <cubey/vulkan/command_recorder.h>\n")
        self.assertEqual(check(self.root)[1], [])
        self.write("include/cubey/scene/scene.h", "#include <cubey/vulkan/device.h>\n#include <vulkan/vulkan.h>\n")
        self.assertEqual(len(check(self.root)[1]), 2)

    def test_relative_includes_cannot_bypass_layers_or_hide_private_headers(self):
        self.write("include/cubey/render/example.h", '#include "../engine/engine.h"\n#include "../../../src/cubey/render/private.h"\n')
        self.write("include/cubey/engine/engine.h")
        self.write("src/cubey/render/private.h")
        errors = check(self.root)[1]
        self.assertEqual(len(errors), 2, errors)
        self.assertTrue(any("public header includes private" in e for e in errors))
        self.assertTrue(any("render -> engine" in e for e in errors))

    def test_foundation_cannot_import_project_implementation(self):
        self.write("src/cubey/engine/example.cpp", '#include "../../../projects/demo/implementation.h"\n')
        self.write("projects/demo/implementation.h")
        self.assertTrue(any("product/study" in e for e in check(self.root)[1]))

    def test_actual_cmake_target_links_cannot_reverse_the_layers(self):
        repository = Path(__file__).resolve().parents[2]
        for name in ("core", "vulkan", "render"):
            self.write(f"src/cubey/{name}/example.cpp", f"void {name}_example() {{}}\n")
        cmake = self.root / "CMakeLists.txt"
        cmake.write_text(f'''cmake_minimum_required(VERSION 3.21)
project(LayerFixture LANGUAGES CXX)
include("{repository.as_posix()}/cmake/CubeyFoundationChecks.cmake")
foreach(module core vulkan render)
    add_library(cubey_${{module}} STATIC src/cubey/${{module}}/example.cpp)
    add_library(cubey::${{module}} ALIAS cubey_${{module}})
endforeach()
target_link_libraries(cubey_vulkan PUBLIC cubey::core)
target_link_libraries(cubey_render PUBLIC cubey::vulkan)
cubey_write_foundation_target_graph("${{CMAKE_BINARY_DIR}}/targets.json")
''')
        def configure():
            result = subprocess.run([CMAKE, "-S", str(self.root), "-B", str(self.root / "build")],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        configure()
        graph = self.root / "build/targets.json"
        self.assertEqual(check_target_graph(graph), [])
        # Physical includes stay clean, but this actual configured link is forbidden.
        cmake.write_text(cmake.read_text().replace("cubey_write_foundation_target_graph(",
            "target_link_libraries(cubey_core PRIVATE cubey::render)\ncubey_write_foundation_target_graph("))
        configure()
        self.assertEqual(check(self.root)[1], [])
        self.assertTrue(any("core -> render" in e for e in check_target_graph(graph)))

    def test_procedural_is_part_of_core_and_new_layers_require_policy(self):
        self.write("include/cubey/procedural/example.h", "#include <cubey/core/math.h>\n")
        self.assertEqual(check(self.root)[1], [])
        self.write("include/cubey/new_layer/example.h")
        self.assertTrue(any("unknown foundation layer" in e for e in check(self.root)[1]))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default=CMAKE)
    args, remaining = parser.parse_known_args()
    CMAKE = args.cmake
    unittest.main(argv=[__file__, *remaining])
