"""Optional NumPy/SciPy tests; no native runtime import or GPU execution."""
import unittest
import tempfile
from pathlib import Path
import subprocess
import sys
from unittest.mock import Mock, patch
import numpy as np

import survey_terrain_sites_v1 as survey
from survey_terrain_sites_v1 import water_metrics, bounds_for, prepare_presets, run_capture
from terrain_water_sites import DEFAULT_CATALOGUE


class Diagnostics(unittest.TestCase):
    def test_metrics_separate_slow_deep_pool_and_fast_stream(self):
        fields = np.zeros((3, 4, 4), dtype="<f4")
        fields[0, :2, :2] = 2
        fields[0, 3, :] = .2
        fields[1, 3, :] = .4
        result = water_metrics(fields.tobytes(), (4, 4), 16)
        self.assertAlmostEqual(result["water_volume_m3"], 8.8*256, places=3)
        self.assertEqual(result["largest_slow_deep_patch_km2"], 4*256/1e6)
        self.assertEqual(result["flowing_km2"], 4*256/1e6)
        self.assertEqual(result["wet_speed_p95_m_per_s"], 2)

    def test_dry_and_invalid_states(self):
        fields = np.zeros((3, 3, 3), dtype="<f4")
        self.assertEqual(water_metrics(fields.tobytes(), (3, 3), 32)["water_volume_m3"], 0)
        for value in (-1, float("nan")):
            fields[0, 0, 0] = value
            with self.assertRaises(ValueError):
                water_metrics(fields.tobytes(), (3, 3), 32)
        fields[:] = 0
        fields[1, 0, 0] = 1
        with self.assertRaises(ValueError):
            water_metrics(fields.tobytes(), (3, 3), 32)

    def test_source_and_climate_intersection(self):
        a = {"grid": {"sample_origin_x_m": -100, "sample_origin_z_m": -100,
                      "width": 11, "height": 11, "sample_spacing_m": 20}}
        b = {"grid": {"sample_origin_x_m": -80, "sample_origin_z_m": -80,
                      "width": 9, "height": 9, "sample_spacing_m": 20}}
        self.assertEqual(bounds_for(a, b), (-80, -80, 80, 80))

    def test_preset_preparation_is_gated_before_any_case_write(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaisesRegex(ValueError, "finish default-grid confirmation"):
                prepare_presets(root, DEFAULT_CATALOGUE, None)
            self.assertEqual(list(root.iterdir()), [])


class CaptureCleanup(unittest.TestCase):
    def test_capture_cleanup_on_success_failure_and_timeout(self):
        command = ["rtk", "proxy", "renderer"]
        for result in (0, 7, subprocess.TimeoutExpired(command, 180)):
            with self.subTest(result=result), tempfile.TemporaryDirectory() as tmp:
                process = Mock()
                if isinstance(result, Exception):
                    process.wait.side_effect = result
                else:
                    process.wait.return_value = result
                cleanup = {"active_group_remaining": []}
                with patch.object(survey.subprocess, "Popen", return_value=process) as popen, \
                        patch.object(survey.demo, "stop_owned", return_value=cleanup) as stop:
                    if result == 0:
                        self.assertEqual(run_capture(command, Path(tmp) / "capture.log", {}), cleanup)
                    else:
                        error = subprocess.TimeoutExpired if isinstance(result, Exception) else subprocess.CalledProcessError
                        with self.assertRaises(error):
                            run_capture(command, Path(tmp) / "capture.log", {})
                    self.assertTrue(popen.call_args.kwargs["start_new_session"])
                    process.wait.assert_called_once_with(timeout=180)
                    stop.assert_called_once_with(process, grace_s=5)

    @unittest.skipUnless(Path("/proc").is_dir(), "Linux owned-group cleanup")
    def test_timeout_reaps_rtk_child_and_descendant(self):
        child = "import time; time.sleep(60)"
        program = ("import os,subprocess,sys,time; "
                   f"subprocess.Popen([sys.executable, '-c', {child!r}]); "
                   "print(os.getpgrp(), flush=True); time.sleep(60)")
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "capture.log"
            with self.assertRaises(subprocess.TimeoutExpired):
                run_capture(["rtk", "proxy", sys.executable, "-u", "-c", program], log, None, timeout=2)
            group = int(log.read_text().strip())
            self.assertEqual(survey.demo.active_owned_group(group), [])


if __name__ == "__main__":
    unittest.main()
