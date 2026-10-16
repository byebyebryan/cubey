"""CPU-only launcher contracts; no optional native import or GPU."""
import unittest
import io
from contextlib import redirect_stderr
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import run_terrain_rain_demo as terrain


class Commands(unittest.TestCase):
    def test_legacy_helper_root_survives_canonical_launcher_move(self):
        self.assertEqual(terrain.HERE, terrain.LEGACY_HELPERS)
        self.assertNotEqual(terrain.HERE, terrain.PROJECT_DIR)
        for relative in ('run_mountain_rain_demo.py', 'validate_terrain_demo_v1.py',
                         'run_offscreen_perf_v2.py', 'terrain_water_world_sites.json',
                         'external_synxflow/session_worker.py'):
            self.assertTrue((terrain.HERE / relative).is_file(), relative)
        self.assertEqual(terrain.RELEASE_APP.parent.name, 'terrain_water')

    def test_runtime_preflight_is_explicit_bounded_and_import_only(self):
        with patch.object(terrain.subprocess, "run",
                          return_value=SimpleNamespace(returncode=0, stdout='{"passed":true,"solver_executed":false}', stderr="")) as run:
            report = terrain.check_native_runtime(Path('/venv/bin/python'), Path('/qualified/flood.so'))
        self.assertTrue(report['passed'])
        command = run.call_args.args[0]
        self.assertEqual(command[:2], ['/venv/bin/python', '-B'])
        self.assertTrue(command[2].endswith('/external_synxflow/check_runtime.py'))
        self.assertNotIn('session_worker.py', ' '.join(command))
        self.assertEqual(run.call_args.kwargs['timeout'], 60)
        self.assertEqual(run.call_args.kwargs['env']['MPLBACKEND'], 'Agg')
        self.assertFalse(terrain.parser().parse_args(['--study','/study']).check_runtime)

    def test_runtime_preflight_never_accepts_execution_or_failed_imports(self):
        for result in (SimpleNamespace(returncode=1, stdout='', stderr='missing runtime library'),
                       SimpleNamespace(returncode=0, stdout='{"passed":true,"solver_executed":true}', stderr='')):
            with patch.object(terrain.subprocess, 'run', return_value=result), self.assertRaises(ValueError):
                terrain.check_native_runtime(Path('/venv/bin/python'), Path('/qualified/flood.so'))

    def test_runtime_preflight_requires_explicit_live_print_mode(self):
        for argv in (['--study','/study','--check-runtime'],
                     ['replay','--study','/study','--print-command','--check-runtime']):
            with patch.object(terrain, 'check_native_runtime') as check, \
                    patch.object(terrain.subprocess, 'Popen') as launch, \
                    redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                terrain.main(argv)
            check.assert_not_called()
            launch.assert_not_called()

    def test_explicit_worker_paths_preserve_venv_and_qualified_policy(self):
        args = terrain.parser().parse_args(['--study','/study','--native-python','/venv/bin/python',
                                          '--extension','/build/flood.so'])
        command = terrain.worker_command(Path('/case'),Path('/session'),30,
                                         native_python=args.native_python,extension=args.extension)
        self.assertEqual(command[:3],['rtk','proxy','/venv/bin/python'])
        self.assertEqual(command[command.index('--extension')+1],'/build/flood.so')
        self.assertEqual(command[command.index('--speed')+1],'30')
        self.assertIn('--paused',command)
        self.assertNotIn('--finite',command)  # Prepared input duration is not a live hard stop.

    def test_default_launch_is_live_1k_release_and_paced(self):
        args = terrain.parser().parse_args(["--study", "/study"])
        self.assertEqual(args.mode, "live")
        self.assertEqual(args.site, "mountain")
        self.assertEqual(args.configuration, "patch-1024-16m")
        self.assertEqual(args.render_fps, 60)
        self.assertEqual(args.app, terrain.RELEASE_APP)
        self.assertEqual(args.surface, "correlated")
        command = terrain.viewer_command(Path("/case"), Path("/session"), "test",
                                         app=args.app, render_fps=args.render_fps)
        self.assertEqual(command[2], str(terrain.RELEASE_APP))
        self.assertEqual(command[command.index("--render-fps") + 1], "60")
        self.assertEqual(command[command.index("--fluid25d-native-terrain-context") + 1], "off")

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
        self.assertEqual(command[command.index("--fluid25d-native-terrain-context") + 1], "off")
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
        self.assertEqual(cmd[cmd.index("--fluid25d-scenic-surface-mode") + 1], "correlated")
        self.assertEqual(cmd[cmd.index("--fluid25d-native-shadow-projection") + 1], "scene")
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
        self.assertEqual(terrain.viewer_command(Path("/case"), Path("/session"), "test")[2],
                         str(terrain.RELEASE_APP))

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

    def test_environment_is_opt_in_and_independent_of_solver(self):
        args=terrain.parser().parse_args(["--study","/study"])
        self.assertEqual(args.environment,"fixed")
        original=terrain.viewer_command(Path("/case"),Path("/session"),"test")
        self.assertNotIn("--fluid25d-environment",original)
        shared=terrain.viewer_command(Path("/case"),Path("/session"),"test",
            environment="shared",environment_time_hours=12,environment_cycle_speed=.5)
        self.assertEqual(shared[shared.index("--fluid25d-environment")+1],"shared")
        self.assertIn("atmosphere.time_paused=false",shared)
        self.assertFalse(any("cloud" in arg for arg in shared))
        self.assertNotIn("--fluid25d-rain-rate",shared)
        worker=terrain.worker_command(Path("/case"),Path("/session"))
        self.assertFalse(any("environment" in arg or "cloud" in arg for arg in worker))
        self.assertEqual(worker[worker.index("--speed")+1],"60")
        for options in ({"environment_time_hours":12},{"environment":"shared","environment_cycle_speed":float("nan")},
                        {"environment":"shared","environment_time_hours":25}):
            with self.assertRaises(ValueError): terrain.viewer_command(Path("/case"),Path("/session"),"test",**options)

    def test_elevated_demo_does_not_offer_cloud_controls(self):
        parser=terrain.parser()
        flags={flag for action in parser._actions for flag in action.option_strings}
        self.assertFalse(any("cloud" in flag for flag in flags))

    def test_frame_by_frame_profiling_is_explicit(self):
        cmd=terrain.viewer_command(Path("/case"),Path("/session"),"test",
            profile=Path("/profile"),profile_sample_interval=1)
        self.assertEqual(cmd[cmd.index("--profile-sample-interval")+1],"1")
        for interval in (0,12001,1.5):
            with self.assertRaises(ValueError):
                terrain.viewer_command(Path("/case"),Path("/session"),"test",profile_sample_interval=interval)

    def test_managed_headless_commands_are_explicit(self):
        command = terrain.viewer_command(Path("/case"), Path("/session"), "test",
            control=Path("/control"), headless_seconds=120, switch_script=Path("/script.json"))
        for flag in ("--headless", "--headless-render-only", "--fluid25d-recording-gpu-validation",
                     "--fluid25d-terrain-demo-control", "--fluid25d-terrain-demo-script"):
            self.assertIn(flag, command)
        self.assertEqual(command[command.index("--run-seconds") + 1], "120")
        with self.assertRaises(ValueError):
            terrain.viewer_command(Path("/case"), Path("/session"), "test",
                live=False, control=Path("/control"))
        with self.assertRaises(ValueError):
            terrain.viewer_command(Path("/case"), Path("/session"), "test",
                switch_script=Path("/script.json"))
        for duration in (0, 3601, float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                terrain.viewer_command(Path("/case"), Path("/session"), "test",
                    headless_seconds=duration)

    def test_unavailable_catalogue_entries_remain_visible_but_disabled(self):
        with patch.object(terrain, "case_identity", side_effect=ValueError("changed input")):
            entries = terrain.catalogue(Path("/study"))
        expected_count = len(terrain.SITES) * len(terrain.CONFIGS)
        self.assertEqual(len(entries), expected_count)
        self.assertEqual(len({entry["id"] for entry in entries}), expected_count)
        self.assertTrue(all(not entry["available"] and entry["error"] == "changed input"
                            for entry in entries))
        self.assertEqual({(entry["width"], entry["spacing_m"]) for entry in entries},
                         {(512, 32), (1024, 16), (2048, 8), (512, 16), (2048, 16)})


if __name__ == "__main__":
    unittest.main()
