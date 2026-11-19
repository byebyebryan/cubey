"""Check the public launcher's three routes and matched scene/camera contract."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from io import StringIO
from unittest.mock import patch

import dragon_demo
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

    def test_native_route_preserves_the_existing_argv(self):
        expected = [
            str(Path("native").resolve()), "--config", str(PRESETS / "dragon-demo.json"),
            "--renderer", "rt", "--transport-mode", "demo", "--no-shadows", "--fov", "45",
            "--yaw", "0.45", "--pitch", "0.35", "--distance", "1.75",
            "--target-x", "0", "--target-y", "0.32", "--target-z", "0",
            "--demo-samples", "4",
        ]
        self.assertEqual(command("native", "rt", "whole"), expected)
        self.assertEqual(command("native", "rt", "whole", upscaler="native"), expected)

    def test_dlss_quality_is_the_experimental_fixed_rt_route(self):
        for samples in (None, 2, 4):
            argv = command("native", "rt", "whole", rt_samples=samples,
                           upscaler="dlss-quality")
            self.assertEqual(argv[argv.index("--transport-mode") + 1], "demo")
            self.assertEqual(argv[argv.index("--demo-samples") + 1], str(samples or 4))
            route = argv.index("--experimental-glass")
            self.assertEqual(argv[route:route + 4], [
                "--experimental-glass", "dragon", "--upscaler", "dlss-quality",
            ])
            self.assertIn("--no-shadows", argv)

    def test_dlss_quality_rejects_other_launcher_modes(self):
        for mode in ("forward", "sampled"):
            with self.subTest(mode=mode), self.assertRaisesRegex(
                    ValueError, "applies only to --mode rt"):
                command("native", mode, "whole", upscaler="dlss-quality")

        with tempfile.TemporaryDirectory() as cwd:
            result = subprocess.run(
                [sys.executable, str(ROOT / "projects/raytracing/tools/dragon_demo.py"),
                 "--mode", "sampled", "--upscaler", "dlss-quality", "--print-command"],
                cwd=cwd, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("applies only to --mode rt", result.stderr)

    def test_opaque_auto_preset_keeps_off_optics_and_leaves_modes_unset(self):
        auto = json.loads((PRESETS / "dragon-fixed-opaque-auto.json").read_text())
        off = json.loads((PRESETS / "dragon-fixed-opaque-off.json").read_text())
        off.pop("anti-aliasing")
        self.assertEqual(auto, off)
        self.assertNotIn("anti-aliasing", auto)
        self.assertNotIn("upscaler", auto)

    def test_model_override_is_repository_rooted_and_print_only_skips_validation(self):
        relative_model = Path("cache/other-checkout/dragon-optical.glb")
        argv = command("native", "rt", "whole", model=relative_model)
        self.assertEqual(argv[argv.index("--model") + 1], str((ROOT / relative_model).resolve()))

        with tempfile.TemporaryDirectory() as cwd:
            missing_binary = Path(cwd) / "missing-renderer"
            missing_model = Path("cache/missing-model.glb")
            result = subprocess.run(
                [sys.executable, str(ROOT / "projects/raytracing/tools/dragon_demo.py"),
                 "--mode", "forward", "--view", "detail", "--binary", str(missing_binary),
                 "--model", str(missing_model), "--print-command", "--",
                 "--headless", "--width", "640"],
                cwd=cwd, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(str(PRESETS / "dragon-forward.json"), result.stdout)
        self.assertIn(f"--model {(ROOT / missing_model).resolve()}", result.stdout)
        self.assertIn("--forward-transmission-source hybrid", result.stdout)
        self.assertIn("--headless --width 640", result.stdout)

    def test_model_validation_and_subprocess_execution(self):
        launcher = ROOT / "projects/raytracing/tools/dragon_demo.py"
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            model = tmp / "dragon.glb"
            model.write_bytes(b"placeholder model")
            binary = tmp / "renderer"
            binary.write_text(
                "#!/usr/bin/env python3\n"
                "import json, sys\n"
                "print(json.dumps(sys.argv[1:]))\n"
                "raise SystemExit(7)\n")
            binary.chmod(0o755)

            result = subprocess.run(
                [sys.executable, str(launcher), "--mode", "rt", "--upscaler", "dlss-quality",
                 "--binary", str(binary), "--model", str(model)],
                cwd=tmp, capture_output=True, text=True)
            self.assertEqual(result.returncode, 7, result.stderr)
            executed = json.loads(result.stdout)
            self.assertEqual(executed[executed.index("--model") + 1], str(model))
            route = executed.index("--experimental-glass")
            self.assertEqual(executed[route:route + 4], [
                "--experimental-glass", "dragon", "--upscaler", "dlss-quality",
            ])

            missing = subprocess.run(
                [sys.executable, str(launcher), "--binary", str(binary),
                 "--model", str(tmp / "missing.glb")],
                cwd=tmp, capture_output=True, text=True)
            self.assertEqual(missing.returncode, 2)
            self.assertIn("model not found:", missing.stderr)

    def test_default_model_validation_message_is_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "renderer"
            binary.write_text("placeholder")
            stderr = StringIO()
            with patch.object(dragon_demo, "MODEL", Path(tmp) / "missing-default.glb"), \
                    patch.object(sys, "argv", ["dragon_demo.py", "--binary", str(binary)]), \
                    redirect_stderr(stderr), self.assertRaises(SystemExit) as error:
                dragon_demo.main()
        self.assertEqual(error.exception.code, 2)
        self.assertIn("prepare the optical model explicitly", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
