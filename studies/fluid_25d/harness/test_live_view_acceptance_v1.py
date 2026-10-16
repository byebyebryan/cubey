"""Pure harness checks; no native, GUI, driver, or dependency installation."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import run_live_view_acceptance_v1 as acceptance


class LiveViewAcceptanceTests(unittest.TestCase):
    def test_profile_is_json_native_and_retains_wall_time_and_viewport(self):
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary) / "profile"
            Path(str(prefix) + ".frames.csv").write_text(
                "frame_index,delta_ms,width,height\n60,1,960,640\n61,2,960,640\n62,3,960,640\n")
            measured = acceptance.profile(prefix)
            self.assertEqual(measured, json.loads(json.dumps(measured)))
            self.assertEqual(measured["extents"], [[960, 640]])
            self.assertEqual(measured["count"], 3)
            self.assertAlmostEqual(measured["observed_wall_s"], .006)
            self.assertEqual(measured["p95_delta_ms"], 3)

    def test_percentile_and_finite_wall_clock_commands(self):
        self.assertEqual(acceptance.percentile([3, 1, 2]), 3)
        with self.assertRaises(AssertionError):
            acceptance.percentile([])
        command = acceptance.viewer_command(Path("viewer"), Path("source"), "own title", Path("profile"), True, True)
        self.assertEqual(command[command.index("--frames") + 1], "0")
        self.assertIn("--fluid25d-stream-follow-latest", command)
        self.assertNotIn("--fps", command)  # Capture fps is not mailbox window pacing.

    def test_resolves_only_unique_owned_window_and_never_closes_focus(self):
        with patch.object(acceptance.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 0, json.dumps([
                {"id": 42, "title": "our title"}, {"id": 9, "title": "unrelated"}]))
            self.assertEqual(acceptance.owned_window_id("our title"), "42")
            self.assertIsNone(acceptance.owned_window_id("missing"))
            with self.assertRaises(RuntimeError):
                acceptance.close_owned_window("missing")
            self.assertFalse(any("close-window" in call.args[0] for call in run.call_args_list))
        with patch.object(acceptance, "owned_window_id", return_value="42"), patch.object(acceptance.subprocess, "run") as run:
            acceptance.close_owned_window("our title")
            self.assertEqual(run.call_args.args[0][-2:], ["--id", "42"])


if __name__ == "__main__":
    unittest.main()
