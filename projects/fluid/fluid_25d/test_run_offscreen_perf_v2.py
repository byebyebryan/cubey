import argparse
import os
from pathlib import Path
import unittest
from unittest.mock import patch

import run_offscreen_perf_v2 as study


class ContractTests(unittest.TestCase):
    def test_offscreen_has_no_display_and_native_has_no_x11_fallback(self):
        with patch.dict(os.environ, {"DISPLAY": ":88", "WAYLAND_DISPLAY": "wayland-1",
                                    "CUBEY_PRIVATE_XVFB": "1"}, clear=True):
            env = study.environment("offscreen")
            self.assertNotIn("DISPLAY", env)
            self.assertNotIn("WAYLAND_DISPLAY", env)
            self.assertNotIn("CUBEY_PRIVATE_XVFB", env)
            native = study.environment("wayland")
            self.assertNotIn("DISPLAY", native)
            self.assertEqual(native["XDG_SESSION_TYPE"], "wayland")
            self.assertEqual(native["WAYLAND_DISPLAY"], "wayland-1")
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(ValueError, "unavailable"):
                study.environment("wayland")

    def test_render_only_reuses_existing_recipe_and_has_no_encoder(self):
        args = argparse.Namespace(width=1920, height=1080, wall=30, warmup=5,
            render_fps=0, app=Path("release-app"), prefix=Path("profile"),
            camera="overview", host="offscreen")
        argv = study.viewer_command(Path("case"), Path("recording.json"), "owned", args, False, 123)
        self.assertIn("--headless-render-only", argv)
        self.assertIn("--headless", argv)
        self.assertNotIn("--capture", argv)
        self.assertIn("bspline-2x", argv)
        self.assertIn("--run-seconds", argv)
        self.assertIn("--no-validation", argv)
        self.assertEqual(argv[argv.index("--render-fps")+1], "0")
        args.host = "wayland"
        native = study.viewer_command(Path("case"), Path("session"), "owned", args, True, 0)
        self.assertNotIn("--headless", native)
        self.assertIn("--fluid25d-external-session", native)
        self.assertNotIn("--fluid25d-recording", native)


if __name__ == "__main__":
    unittest.main()
