"""CPU-only launcher contracts; no optional native import or GPU."""
import unittest
from pathlib import Path

import run_terrain_rain_demo as terrain


class Commands(unittest.TestCase):
    def test_live_is_explicit_and_does_not_change_solver_or_material_defaults(self):
        cmd = terrain.viewer_command(Path("/case"), Path("/session"), "test")
        self.assertIn("--fluid25d-external-session", cmd)
        self.assertNotIn("--fluid25d-recording", cmd)
        self.assertNotIn("--fluid25d-solver", cmd)
        self.assertEqual(cmd[cmd.index("--fluid25d-native-bank-view") + 1], "bspline-2x")
        self.assertEqual(cmd[cmd.index("--fluid25d-scenic-surface-mode") + 1], "legacy")
        self.assertIn("--paused", terrain.worker_command(Path("/case"), Path("/session")))

    def test_profile_bounds_and_correlated_are_explicit(self):
        cmd = terrain.viewer_command(Path("/case"), Path("/capture/recording.json"), "test",
            profile=Path("/profile"), live=False, start=20, surface="correlated")
        self.assertNotIn("--fluid25d-external-session", cmd)
        self.assertIn("--fluid25d-recording", cmd)
        self.assertEqual(cmd[cmd.index("--profile-max-frames") + 1], "12000")
        self.assertEqual(cmd[cmd.index("--profile-sample-interval") + 1], "10")
        self.assertEqual(cmd[cmd.index("--fluid25d-recording-time-seconds") + 1], "20")

    def test_renderer_artifact_override_does_not_mutate_default(self):
        original = terrain.demo.ref.APP
        command = terrain.viewer_command(Path("/case"), Path("/session"), "test",
                                         app=Path("/release/fluid_25d"))
        self.assertEqual(command[:3], ["rtk", "proxy", "/release/fluid_25d"])
        self.assertEqual(terrain.demo.ref.APP, original)
        self.assertEqual(terrain.viewer_command(Path("/case"), Path("/session"), "test")[2], str(original))


if __name__ == "__main__":
    unittest.main()
