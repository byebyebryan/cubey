"""Check the public launcher's three routes and matched scene/camera contract."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from dragon_demo import MODES, PRESETS, ROOT, VIEWS, command


class DragonDemoTests(unittest.TestCase):
    def test_all_views_match_across_routes(self):
        for view, camera in VIEWS.items():
            effective = []
            for mode in MODES:
                argv = command(ROOT / "build/dev/projects/raytracing/raytracing", mode, view)
                config = json.loads(Path(argv[argv.index("--config") + 1]).read_text())
                for name in (*camera, "renderer", "transport-mode", "fov"):
                    value = argv[argv.index("--" + name) + 1]
                    config[name] = float(value) if name in camera or name == "fov" else value
                config["shadows"] = False
                effective.append(config)
                self.assertEqual({name: config[name] for name in camera}, camera)
            for key in ("model", "material", "support-material", "dragon-support", "environment",
                        "sun-azimuth", "sun-elevation", "intensity", "exposure", "shadows", "fov"):
                self.assertEqual(len({str(c[key]) for c in effective}), 1, key)

    def test_modes_keep_their_distinct_transport(self):
        for mode, (_, renderer, transport) in MODES.items():
            argv = command("native", mode, "whole")
            self.assertEqual(argv[argv.index("--renderer") + 1], renderer)
            self.assertEqual(argv[argv.index("--transport-mode") + 1], transport)
            self.assertIn("--no-shadows", argv)
            if mode == "forward":
                self.assertEqual(argv[argv.index("--forward-transmission-source") + 1], "hybrid")
        two = command("native", "rt", "whole", 2)
        self.assertEqual(two[two.index("--demo-samples") + 1], "2")
        with self.assertRaises(ValueError):
            command("native", "sampled", "whole", 2)
        self.assertTrue(json.loads((PRESETS / "dragon-glass.json").read_text())["shadows"])

    def test_preview_is_root_independent_and_forwards_native_arguments(self):
        with tempfile.TemporaryDirectory() as cwd:
            result = subprocess.run([sys.executable, str(ROOT / "projects/raytracing/tools/dragon_demo.py"),
                                     "--mode", "forward", "--view", "detail", "--print-command", "--",
                                     "--headless", "--width", "640"], cwd=cwd, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(str(PRESETS / "dragon-forward.json"), result.stdout)
        self.assertIn("--forward-transmission-source hybrid", result.stdout)
        self.assertIn("--headless --width 640", result.stdout)


if __name__ == "__main__":
    unittest.main()
