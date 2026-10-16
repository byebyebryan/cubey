import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import study_world_water_v1 as study


class StudyContracts(unittest.TestCase):
    def test_controller_timeout_unwinds_separately_owned_child_past_wrapper_exit(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            controller = """
import json
from pathlib import Path
import subprocess
import sys
import time
sys.path.insert(0, sys.argv[1])
import run_mountain_rain_demo as demo
root = Path(sys.argv[2])
child = subprocess.Popen(['rtk', 'proxy', sys.executable, '-c',
                          'import time; time.sleep(60)'], start_new_session=True)
(root / 'child.json').write_text(json.dumps({'pid': child.pid}))
try:
    time.sleep(60)
finally:
    cleanup = demo.stop_owned(child)
    (root / 'child-cleanup.json').write_text(json.dumps(cleanup))
"""
            command = ["rtk", "proxy", sys.executable, "-c", controller,
                       str(study.terrain.HERE), str(root)]
            try:
                with self.assertRaises(subprocess.TimeoutExpired):
                    study.run_controller(command, root / "controller.log", timeout=1)
                child = json.loads((root / "child.json").read_bytes())
                self.assertTrue((root / "child-cleanup.json").is_file(),
                                (root / "controller.log").read_text())
                cleanup = json.loads((root / "child-cleanup.json").read_bytes())
                self.assertEqual(cleanup["active_group_remaining"], [])
                self.assertEqual(study.terrain.runtime.active_owned_group(child["pid"]), [])
                controller_cleanup = json.loads((root / "controller.cleanup.json").read_bytes())
                self.assertFalse(controller_cleanup["escalated"])
                self.assertEqual(controller_cleanup["active_group_remaining"], [])
            finally:
                # A failing regression must not leave its own fixture running.
                if (root / "child.json").exists():
                    pid = json.loads((root / "child.json").read_bytes())["pid"]
                    if study.terrain.runtime.active_owned_group(pid):
                        os.killpg(pid, signal.SIGKILL)

    def test_controller_failure_is_checked_unless_explicitly_expected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            command = ["rtk", "proxy", sys.executable, "-c", "raise SystemExit(3)"]
            with self.assertRaises(subprocess.CalledProcessError):
                study.run_controller(command, root / "failed.log", timeout=5)
            result = study.run_controller(command, root / "expected.log", timeout=5, check=False)
            self.assertEqual(result.returncode, 3)
            for name in ("failed", "expected"):
                cleanup = json.loads((root / f"{name}.cleanup.json").read_bytes())
                self.assertEqual(cleanup["active_group_remaining"], [])

    def test_changed_frozen_inputs_or_runtime_reject(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            study.write(root / "frozen.json", {"protected_inputs":{"source":"a"}, "runtime":{"app":"b"}})
            with patch.object(study, "protected_inputs", return_value={"source":"a"}), \
                    patch.object(study, "runtime", return_value={"app":"b"}):
                self.assertEqual(study.verify(root)["runtime"], {"app":"b"})
            with patch.object(study, "protected_inputs", return_value={"source":"changed"}), \
                    patch.object(study, "runtime", return_value={"app":"b"}):
                with self.assertRaises(ValueError):
                    study.verify(root)

    def test_lifecycle_requires_all_fifteen_admitted_cases(self):
        with patch.object(study.terrain, "catalogue", return_value=[]):
            with self.assertRaises(ValueError):
                study.lifecycle(Path("/not-created"))

    def test_performance_uses_frozen_absolute_app_not_invocation_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            recording = out / "runs/one-patch-1024-16m/recording/recording.json"
            recording.parent.mkdir(parents=True)
            recording.write_text("{}")
            def runner(command, log, timeout):
                self.assertEqual(command[command.index("--app") + 1], str(study.terrain.RELEASE_APP))
                self.assertTrue(Path(command[command.index("--app") + 1]).is_absolute())
                target = Path(command[command.index("--out") + 1])
                target.mkdir()
                study.write(target / "results.json", {"trials":[{"mode":"live", "profile":{
                    "passes_ms":{"gpu:fluid_25d native presentation total":{"p95":5}}}}]})
            with patch.object(study, "load_sites", return_value={"one":{}}), \
                    patch.object(study, "run_controller", side_effect=runner) as run:
                study.performance(out)
                self.assertEqual(run.call_count, 2)

    def test_publish_reuses_exact_bundle_but_never_overwrites_changed_copy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            out = root / "outputs"
            source = out / "visual/one/capture"
            source.mkdir(parents=True)
            (source / "water-capture.json").write_bytes(b"trusted exporter fixture")
            with patch.object(study, "ROOT", root), \
                    patch.object(study, "load_sites", return_value={"one":{"manifest":"source.json"}}):
                study.publish_captures(out)
                study.publish_captures(out)
                asset = json.loads((out / "consumer-assets.json").read_bytes())["assets"][0]
                Path(asset["manifest"]).write_bytes(b"changed")
                with self.assertRaises(ValueError):
                    study.publish_captures(out)


if __name__ == "__main__":
    unittest.main()
