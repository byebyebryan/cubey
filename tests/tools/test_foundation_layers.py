"""Negative and adapter fixtures for physical foundation include boundaries."""
import argparse
import json
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

    def configure_links(self, extra="", configuration="Debug", generator="Ninja"):
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
{extra}
cubey_write_foundation_target_graph("${{CMAKE_BINARY_DIR}}/targets.json")
''')
        build = self.root / ("build-" + generator.replace(" ", "-") + "-" + configuration)
        result = subprocess.run([CMAKE, "-S", str(self.root), "-B", str(build), "-G", generator,
                                 "-DCMAKE_BUILD_TYPE=" + configuration,
                                 "-DCMAKE_CONFIGURATION_TYPES=Debug;Release"],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return build / "targets.json"

    def test_actual_cmake_target_links_cannot_reverse_the_layers(self):
        graph = self.configure_links()
        self.assertEqual(check_target_graph(graph), [])
        for dependency in ("cubey::render", "cubey_render"):
            for visibility in ("PRIVATE", "PUBLIC", "INTERFACE"):
                with self.subTest(dependency=dependency, visibility=visibility):
                    graph = self.configure_links(
                        f"target_link_libraries(cubey_core {visibility} {dependency})")
                    self.assertEqual(check(self.root)[1], [])
                    self.assertTrue(any("core -> render" in e for e in check_target_graph(graph)))

    def test_aliases_and_interface_wrappers_preserve_target_identity(self):
        for extra in (
            "add_library(renamed_render ALIAS cubey_render)\n"
            "target_link_libraries(cubey_core PRIVATE renamed_render)",
            "add_library(bridge INTERFACE)\n"
            "target_link_libraries(bridge INTERFACE cubey::render)\n"
            "target_link_libraries(cubey_core PUBLIC bridge)",
            "add_library(bridge INTERFACE IMPORTED)\n"
            "set_property(TARGET bridge PROPERTY INTERFACE_LINK_LIBRARIES cubey::render)\n"
            "target_link_libraries(cubey_core PRIVATE bridge)",
            "add_library(bridge_a INTERFACE)\nadd_library(bridge_b INTERFACE)\n"
            "target_link_libraries(bridge_a INTERFACE bridge_b)\n"
            "target_link_libraries(bridge_b INTERFACE bridge_a cubey::render)\n"
            "target_link_libraries(cubey_core PRIVATE bridge_a)",
        ):
            with self.subTest(extra=extra):
                errors = check_target_graph(self.configure_links(extra))
                self.assertTrue(any("core -> render" in e for e in errors), errors)

    def test_configured_generator_expressions_are_evaluated_by_cmake(self):
        self.assertEqual(check_target_graph(self.configure_links(configuration="")), [])
        extra = 'target_link_libraries(cubey_core PRIVATE "$<$<CONFIG:Release>:cubey::render>")'
        self.assertEqual(check_target_graph(self.configure_links(extra)), [])
        errors = check_target_graph(self.configure_links(extra, "Release"))
        self.assertTrue(any("Release: configured target" in e and "core -> render" in e for e in errors))
        errors = check_target_graph(self.configure_links(extra, generator="Ninja Multi-Config"))
        self.assertTrue(any("Release:" in e and "core -> render" in e for e in errors))
        self.assertFalse(any("Debug:" in e for e in errors), errors)
        self.assertEqual(check_target_graph(self.configure_links(
            'target_link_libraries(cubey_core INTERFACE "$<INSTALL_INTERFACE:cubey::render>")')), [])

    def test_directory_wrappers_and_allowed_transitive_layers(self):
        self.write("child/CMakeLists.txt", "target_link_libraries(cubey_core INTERFACE cubey::render)\n")
        errors = check_target_graph(self.configure_links("add_subdirectory(child)"))
        self.assertTrue(any("core -> render" in e for e in errors), errors)
        # Scene -> render -> Vulkan is allowed. Do not reinterpret the lower
        # library's dependencies as direct edges from the scene layer.
        self.write("src/cubey/scene/example.cpp", "void scene_example() {}\n")
        self.assertEqual(check_target_graph(self.configure_links(
            "add_library(cubey_scene STATIC src/cubey/scene/example.cpp)\n"
            "add_library(cubey::scene ALIAS cubey_scene)\n"
            "target_link_libraries(cubey_scene PUBLIC cubey::render)")), [])

    def test_missing_or_unevaluated_link_evidence_fails(self):
        graph = self.configure_links()
        manifest = json.loads(graph.read_text())
        links = graph.parent / manifest["configurations"]["Debug"]["cubey_core"]
        links.unlink()
        self.assertTrue(check_target_graph(graph))
        links.write_text("$<LINK_ONLY:cubey::render>\n\n")
        self.assertTrue(any("unevaluated" in e for e in check_target_graph(graph)))

    def test_opaque_directory_imports_cannot_silently_bypass_the_guard(self):
        self.write("child/CMakeLists.txt",
                   "add_library(hidden INTERFACE IMPORTED)\n"
                   "set_property(TARGET hidden PROPERTY INTERFACE_LINK_LIBRARIES cubey::render)\n"
                   "target_link_libraries(cubey_core INTERFACE hidden)\n")
        errors = check_target_graph(self.configure_links("add_subdirectory(child)"))
        self.assertTrue(any("cannot inspect directory-local target hidden" in e for e in errors), errors)

    def test_compiled_foreign_library_platform_imports_are_terminal(self):
        self.write("vendor/CMakeLists.txt",
                   "project(External LANGUAGES CXX)\n"
                   "add_library(platform INTERFACE IMPORTED)\n"
                   "add_library(external STATIC external.cpp)\n"
                   "target_link_libraries(external PRIVATE platform)\n")
        self.write("vendor/external.cpp", "void external_example() {}\n")
        self.assertEqual(check_target_graph(self.configure_links(
            "add_subdirectory(vendor)\ntarget_link_libraries(cubey_core PRIVATE external)")), [])

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
