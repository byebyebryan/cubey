"""Host-only guards for the bounded macro review and private rain UI driver."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import review_macro_terrain_v4 as review
import probe_macro_rain_v4 as gui
import run_mountain_rain_demo as demo


class MacroReviewTests(unittest.TestCase):
    def test_macro_launch_is_explicit_and_defaults_stay_refined(self):
        explicit = demo.parser().parse_args(["replay","--style","scenic","--material","macro"])
        demo.validate_options(explicit)
        self.assertEqual(explicit.material,"macro")
        self.assertEqual(demo.parser().parse_args(["replay"]).material,"refined")
        invalid = demo.parser().parse_args(["replay","--material","macro"])
        with self.assertRaises(ValueError):
            demo.validate_options(invalid)

    def test_source_inventory_includes_shared_diffuse_and_gui(self):
        names = review.sources()
        for name in review.EXTRA_SOURCES:
            self.assertIn(name,names)
        self.assertTrue(all(len(value)==64 for value in names.values()))

    def test_private_layout_requires_expected_screen(self):
        with patch.object(gui.ref,"png_dimensions",return_value=(1280,720)):
            with self.assertRaises(ValueError):
                gui.rain_points(Path("screen.png"))
        with patch.object(gui.ref,"png_dimensions",return_value=(1920,1080)):
            points = gui.rain_points(Path("screen.png"))
            self.assertEqual(points["input"],(65,200))
            self.assertEqual(points["apply"],(74,230))

    def test_parity_rejects_changed_water_shader_and_raw_pixels(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            runtime = {"inputs":{"bed":"same"},"compiled_shaders":{"files":{
                "fluid_25d_scenic_terrain.frag.spv":"new","water.spv":"same"}}}
            baseline_runtime = {**runtime,"compiled_shaders":{"files":{
                "fluid_25d_scenic_terrain.frag.spv":"old","water.spv":"same"}}}
            row = {"path":"raw.png","kind":"diagnostic","presentation":"scenic","sha256":"same"}
            for label,identity in (("baseline",baseline_runtime),("legacy-final",runtime),("candidate-final",runtime)):
                phase = out/label
                phase.mkdir()
                (phase/"manifest.json").write_text(json.dumps({"assets":[row],"runtime_identity":identity}))
            with patch.object(review.ref,"runtime_identity",return_value=runtime):
                self.assertEqual(review.parity(out)["readable_raw_pixel_controls"],1)
                changed = {**runtime,"compiled_shaders":{"files":{**runtime["compiled_shaders"]["files"],"water.spv":"changed"}}}
                with patch.object(review.ref,"runtime_identity",return_value=changed):
                    with self.assertRaisesRegex(ValueError,"shader changed"):
                        review.parity(out)
                (out/"candidate-final/manifest.json").write_text(json.dumps({"assets":[{**row,"sha256":"changed"}]}))
                with self.assertRaisesRegex(ValueError,"Readable/raw"):
                    review.parity(out)


if __name__=="__main__":
    unittest.main()
