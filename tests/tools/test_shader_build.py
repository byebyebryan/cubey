"""Exercise shader dependency discovery with real CMake/Ninja/glslang builds."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CMAKE = "cmake"


class ShaderBuildTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cubey shader dependencies ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / "build with spaces"
        (self.root / "nested.glsl").write_text("const float VALUE = 0.25;\n")
        (self.root / "direct.glsl").write_text('#include "nested.glsl"\n')
        (self.root / "unused.glsl").write_text("// not included\n")
        (self.root / "dependent.comp").write_text('''#version 450
#extension GL_GOOGLE_include_directive : require
#include "direct.glsl"
layout(local_size_x=1) in;
layout(set=0,binding=0,rgba32f) uniform writeonly image2D output_image;
void main(){imageStore(output_image,ivec2(gl_GlobalInvocationID.xy),vec4(VALUE));}
''')
        (self.root / "independent.comp").write_text('''#version 450
layout(local_size_x=1) in;
void main() {}
''')
        (self.root / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.21)
project(ShaderFixture LANGUAGES NONE)
include("{ROOT.as_posix()}/cmake/CubeyShaders.cmake")
add_custom_target(probe)
cubey_add_glsl_shaders(probe SOURCES dependent.comp independent.comp)
''')

    def configure(self, *options, success=True):
        result = subprocess.run([CMAKE, "-S", str(self.root), "-B", str(self.build),
                                 "-G", "Ninja", *options],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def build_shaders(self, success=True):
        result = subprocess.run([CMAKE, "--build", str(self.build), "--target", "probe"],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def artifact(self, name="dependent"):
        return self.build / "shaders" / (name + ".comp.spv")

    def digest(self, name="dependent"):
        return hashlib.sha256(self.artifact(name).read_bytes()).hexdigest()

    def test_nested_edit_rebuilds_only_affected_shader_and_noop_stays_idle(self):
        self.configure()
        self.build_shaders()
        before = self.digest()
        independent_time = self.artifact("independent").stat().st_mtime_ns
        (self.root / "nested.glsl").write_text("const float VALUE = 0.75;\n")
        self.build_shaders()
        self.assertNotEqual(before, self.digest())
        self.assertEqual(independent_time, self.artifact("independent").stat().st_mtime_ns)
        dependent_time = self.artifact().stat().st_mtime_ns
        (self.root / "unused.glsl").write_text("// still not included\n")
        self.build_shaders()
        self.assertEqual(dependent_time, self.artifact().stat().st_mtime_ns)
        self.assertEqual(independent_time, self.artifact("independent").stat().st_mtime_ns)

    def test_removed_include_updates_dependencies(self):
        self.configure()
        self.build_shaders()
        (self.root / "direct.glsl").write_text("const float VALUE = 0.5;\n")
        self.build_shaders()
        timestamp = self.artifact().stat().st_mtime_ns
        (self.root / "nested.glsl").unlink()
        self.build_shaders()
        self.assertEqual(timestamp, self.artifact().stat().st_mtime_ns)

    def test_missing_live_include_fails(self):
        self.configure()
        self.build_shaders()
        (self.root / "nested.glsl").unlink()
        self.build_shaders(success=False)

    def test_inactive_preprocessor_include_is_not_a_dependency(self):
        shader = self.root / "dependent.comp"
        shader.write_text(shader.read_text().replace('#include "direct.glsl"',
            '#if 0\n#include "missing-inactive.glsl"\n#endif\n#include "direct.glsl"'))
        self.configure()
        self.build_shaders()
        timestamp = self.artifact().stat().st_mtime_ns
        (self.root / "missing-inactive.glsl").write_text("not valid GLSL\n")
        self.build_shaders()
        self.assertEqual(timestamp, self.artifact().stat().st_mtime_ns)

    def test_compiler_without_depfiles_fails_at_configure(self):
        compiler = self.root / "old compiler"
        compiler.write_text("#!" + sys.executable + "\nprint('compiler without dependency option')\n")
        compiler.chmod(0o700)
        result = self.configure("-DGLSLANG_VALIDATOR_EXE=" + str(compiler), success=False)
        self.assertIn("requires glslangValidator with --depfile support", result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default=CMAKE)
    args, remaining = parser.parse_known_args()
    CMAKE = args.cmake
    unittest.main(argv=[__file__, *remaining])
