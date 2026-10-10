"""Host guards for consolidated Scenic water; GPU tests check actual pixels."""
from contextlib import redirect_stderr
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import review_scenic_water as review
import run_mountain_rain_demo as demo
import test_scenic_water_gpu as gpu


class ScenicWaterTests(unittest.TestCase):
    def test_synthetic_runtime_uses_requested_build_without_archived_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "alternate-build" / "cubey"
            target.parent.mkdir()
            target.write_bytes(b"synthetic executable")
            shaders = target.parent / "shaders"
            shaders.mkdir()
            shader = shaders / "control.spv"
            shader.write_bytes(b"synthetic shader")
            with patch.object(review.ref, "runtime_identity", side_effect=AssertionError("archive used")), \
                 patch.object(review.ref, "frozen_input_identity", side_effect=AssertionError("archive used")):
                first = gpu.runtime_identity(target)
                self.assertEqual(first["executable"], str(target))
                self.assertEqual(first["compiled_shaders"]["files"],
                                 {"control.spv": review.ref.sha256_file(shader)})
                self.assertNotIn("inputs", first)
                review.assert_render_runtime(first, {**first, "inputs": "not synthetic"}, "test")
                shader.write_bytes(b"changed shader")
                with self.assertRaisesRegex(ValueError, "SPIR-V changed"):
                    review.assert_render_runtime(first, gpu.runtime_identity(target), "test")
                shader.write_bytes(b"synthetic shader")
                target.write_bytes(b"changed executable")
                with self.assertRaisesRegex(ValueError, "SPIR-V changed"):
                    review.assert_render_runtime(first, gpu.runtime_identity(target), "test")

    def test_launcher_uses_accepted_scenic_default_without_film_modes(self):
        a = demo.parser().parse_args(["replay"])
        self.assertEqual((a.style, a.material), ("scenic", None))
        command = demo.commands(a, Path("/tmp/owned-scenic-preview"))["viewer"]
        self.assertEqual(command[command.index("--fluid25d-scenic-material")+1], "macro")
        self.assertFalse(hasattr(a, "water_film"))
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            demo.parser().parse_args(["replay", "--water-film", "wet-ground"])
        for mode in ("replay", "live", "banks"):
            a = demo.parser().parse_args([mode, "--style", "scenic", "--material", "macro"])
            demo.validate_options(a)
            command = demo.commands(a, Path("/tmp/owned-scenic-preview"))["viewer"]
            self.assertNotIn("--fluid25d-scenic-water-film", command)
            self.assertIn("--fluid25d-scenic-material", command)

    def test_one_water_fragment_and_pipeline_pair(self):
        root = review.ref.ROOT
        cmake = (root / review.LOCAL / "CMakeLists.txt").read_text()
        cpp = (root / review.SIM / "fluid_25d_scenic.cpp").read_text()
        self.assertNotIn("scenic_water_study", cmake)
        self.assertNotIn("water_bspline_study", cpp)
        self.assertNotIn("water_study", cpp)
        self.assertEqual(cpp.count('"fluid_25d_scenic_water.frag.spv"'), 2)
        self.assertFalse((root / review.SIM / "shaders/fluid_25d_scenic_water_study.frag").exists())

    def test_normal_shader_has_film_and_retained_diagnostics(self):
        shader = (review.ref.ROOT / review.SIM / "shaders/fluid_25d_scenic_water.frag").read_text()
        self.assertIn("fluid25d_film_weight(h,scenic.water_film.x,scenic.water_film.y)", shader)
        self.assertIn("max(roughness,scenic.water_film.z)", shader)
        self.assertIn("film_weight,scenic.water_film.w", shader)
        self.assertIn("uint requested_water_view = uint(scenic.water_view.x)", shader)
        self.assertIn("uint water_view = requested_water_view==19u ? 0u : requested_water_view", shader)
        for text in ("if (h<=params.camera_wet.w) discard;",
                     "if (gl_FragCoord.z>scene_z) discard;",
                     "out_color = vec4(color*coverage,coverage);"):
            self.assertIn(text, shader)
        self.assertNotIn("study_view", shader)

    def test_terrain_wetness_follows_display_reconstruction(self):
        shaders = review.ref.ROOT / review.SIM / "shaders"
        helper = (shaders / "fluid_25d_terrain_wetness.glsl").read_text()
        self.assertIn("if (bspline)", helper)
        self.assertIn("fluid25d_bspline_bed_depth(coordinate,grid).y", helper)
        self.assertIn("fluid25d_triangle_sample", helper)
        self.assertIn("smoothstep(wet_threshold,0.012,h)*(1.0-smoothstep(0.02,0.05,h))", helper)
        for name in ("fluid_25d_scenic_terrain.frag", "fluid_25d_scenic_terrain_legacy.frag"):
            shader = (shaders / name).read_text()
            self.assertIn('#include "fluid_25d_terrain_wetness.glsl"', shader)
            self.assertIn("fluid25d_terrain_wet_depth", shader)
            self.assertIn("params.presentation.x>0.0", shader)
            self.assertIn("fluid25d_terrain_wet_weight(h,params.camera_wet.w)", shader)
            self.assertNotIn("fluid25d_triangle_sample", shader)

    def test_no_film_selector_in_normal_ui_or_configuration(self):
        root = review.ref.ROOT
        cpp = (root / review.SIM / "fluid_25d_recording_app.cpp").read_text()
        header = (root / review.LOCAL / "fluid_25d_project_config.h").read_text()
        self.assertNotIn("Water film (opt-in)", cpp)
        self.assertNotIn("native_scenic_water_film", cpp + header)
        self.assertIn('ImGui::Combo("Water component"', cpp)
        self.assertIn('"Film ground emphasis"', cpp)


if __name__ == "__main__":
    unittest.main()
