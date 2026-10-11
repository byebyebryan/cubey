"""CPU-only launcher contracts; no optional native import or GPU."""
import unittest
from pathlib import Path

import run_terrain_rain_demo as terrain


class Commands(unittest.TestCase):
    def test_default_launch_is_live_1k_release_and_paced(self):
        args = terrain.parser().parse_args(["--study", "/study"])
        self.assertEqual(args.mode, "live")
        self.assertEqual(args.site, "mountain")
        self.assertEqual(args.configuration, "patch-1024-16m")
        self.assertEqual(args.render_fps, 60)
        self.assertEqual(args.app, terrain.RELEASE_APP)
        command = terrain.viewer_command(Path("/case"), Path("/session"), "test",
                                         app=args.app, render_fps=args.render_fps)
        self.assertEqual(command[2], str(terrain.RELEASE_APP))
        self.assertEqual(command[command.index("--render-fps") + 1], "60")

    def test_replay_finer_grid_and_pacing_overrides_remain_explicit(self):
        args = terrain.parser().parse_args(["replay", "--study", "/study",
            "--recording", "/capture/recording.json", "--configuration", "patch-2048-8m",
            "--render-fps", "0", "--app", "/custom/fluid_25d"])
        self.assertEqual(args.mode, "replay")
        self.assertEqual(args.configuration, "patch-2048-8m")
        self.assertEqual(args.render_fps, 0)
        self.assertEqual(args.app, Path("/custom/fluid_25d"))
        command = terrain.viewer_command(Path("/case"), args.recording, "test",
                                         live=False, render_fps=args.render_fps)
        self.assertEqual(command[command.index("--render-fps") + 1], "0")
        self.assertIn("--fluid25d-recording", command)
        self.assertNotIn("--fluid25d-external-session", command)
        for fps in (-1, 0x100000000):
            with self.assertRaises(ValueError):
                terrain.viewer_command(Path("/case"), Path("/session"), "test", render_fps=fps)

    def test_live_does_not_change_global_solver_or_material_defaults(self):
        cmd = terrain.viewer_command(Path("/case"), Path("/session"), "test")
        self.assertIn("--fluid25d-external-session", cmd)
        self.assertNotIn("--fluid25d-recording", cmd)
        self.assertNotIn("--fluid25d-solver", cmd)
        self.assertEqual(cmd[cmd.index("--fluid25d-native-bank-view") + 1], "bspline-2x")
        self.assertEqual(cmd[cmd.index("--fluid25d-scenic-surface-mode") + 1], "legacy")
        self.assertIn("--paused", terrain.worker_command(Path("/case"), Path("/session")))
        self.assertNotIn("--render-fps", cmd)

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

    def test_lower_display_density_keeps_bspline_and_simulation_untouched(self):
        command = terrain.viewer_command(Path("/case"), Path("/session"), "test", surface_subdivision=1)
        self.assertNotIn("--fluid25d-native-bank-view", command)
        self.assertEqual(command[command.index("--fluid25d-native-water-sampling") + 1], "bspline")
        self.assertEqual(command[command.index("--fluid25d-native-surface-subdivision") + 1], "1")
        self.assertNotIn("--fluid25d-grid", command)
        with self.assertRaises(ValueError):
            terrain.viewer_command(Path("/case"), Path("/session"), "test", surface_subdivision=4)

    def test_camera_uses_existing_recorded_view_controls(self):
        command = terrain.viewer_command(Path("/case"), Path("/session"), "test", camera="collection")
        self.assertEqual(command[command.index("--fluid25d-recording-camera") + 1], "collection")


if __name__ == "__main__":
    unittest.main()
