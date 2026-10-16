"""Orchestration tests never launch a renderer, compiler or optional solver."""
import json
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

import finish_live_perf_terrain_v1 as finish


class CompletedProcess:
    returncode = 0

    def poll(self):
        return 0

    def wait(self, **kwargs):
        return 0


class Orchestration(unittest.TestCase):
    def test_display_experiment_is_serial_bounded_and_does_not_rebuild(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "follow-on").mkdir()
            (root / "follow-on/finished.json").write_text("{}")
            (root / "campaign-start.json").write_text(json.dumps({"unix_s": time.time()}))
            capture = root / "coverage/mountain-patch-2048-8m-live-0/static-recording/recording.json"
            capture.parent.mkdir(parents=True)
            capture.write_text("{}")
            with patch("sys.argv", ["study", "--study", str(root), "--display-experiment-only"]), \
                 patch.object(finish, "ROOT", root), patch.object(finish.signal, "signal"), patch("builtins.print"), \
                 patch.object(finish.subprocess, "Popen", return_value=CompletedProcess()) as launch:
                self.assertEqual(finish.main(), 0)
            commands = [call.args[0] for call in launch.call_args_list]
            self.assertEqual(len(commands), 7)
            self.assertFalse(any("cmake" in command for command in commands))
            self.assertIn("--surface-subdivision", commands[0])
            self.assertEqual(commands[0][commands[0].index("--surface-subdivision") + 1], "2")
            self.assertEqual(commands[1][commands[1].index("--surface-subdivision") + 1], "1")
            self.assertEqual(commands[2][commands[2].index("--camera") + 1], "runoff")
            self.assertIn("--control-probe", commands[4])
            receipt = json.loads((root / "display-follow-on/finished.json").read_bytes())
            self.assertFalse(receipt["defaults_changed"])
            self.assertFalse(receipt["pushed"])

    def test_waiting_for_primary_study_respects_campaign_deadline(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "campaign-start.json").write_text(json.dumps({"unix_s": time.time() - 9 * 3600}))
            with patch("sys.argv", ["study", "--study", str(root), "--display-experiment-only"]), \
                 patch.object(finish.signal, "signal"), patch.object(finish.subprocess, "Popen") as launch:
                self.assertEqual(finish.main(), 1)
                launch.assert_not_called()
            receipt = json.loads((root / "display-follow-on/finished.json").read_bytes())
            self.assertEqual(receipt["status"], "campaign-budget-skipped")


if __name__ == "__main__":
    unittest.main()
