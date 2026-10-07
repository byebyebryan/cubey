"""CPU-only command, preflight and owned-process tests. No CUDA or GUI."""
from __future__ import annotations

import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

import run_mountain_rain_demo as demo


class MountainLauncherTests(unittest.TestCase):
    def args(self, *values):
        return demo.parser().parse_args(values)

    def test_three_explicit_modes_and_no_global_defaults(self):
        out = Path("/tmp/unused-preview")
        replay = demo.commands(self.args("replay"), out)
        self.assertEqual(set(replay), {"viewer"})
        self.assertIn("recording", replay["viewer"])
        self.assertIn("150", replay["viewer"])
        self.assertIn("reference", replay["viewer"])
        self.assertNotIn("--fluid25d-native-display-coverage", replay["viewer"])
        self.assertIn("--fluid25d-motion-markers", replay["viewer"])
        live = demo.commands(self.args("live"), out)
        self.assertIn("--paused", live["worker"])
        self.assertNotIn("--finite", live["worker"])
        self.assertIn("60", live["worker"])
        self.assertIn("external", live["viewer"])
        self.assertNotIn("--fluid25d-recording-speed", live["viewer"])
        banks = demo.commands(self.args("banks", "--loop", "--bank", "marching-squares"), out)
        self.assertIn("--fluid25d-bank-comparison", banks["viewer"])
        self.assertIn("--fluid25d-bank-comparison-loop", banks["viewer"])
        self.assertIn("7260", banks["viewer"])

    def test_invalid_combinations_fail_before_process_creation(self):
        for values in (("live", "--start", "60"), ("live", "--rain-case", "rain-on"),
                       ("replay", "--loop"), ("replay", "--bank", "marching-squares"),
                       ("banks", "--start", "7200"), ("replay", "--start", "61"),
                       ("live", "--speed", "nan"), ("live", "--speed", "301"),
                       ("replay", "--width", "10"), ("live", "--startup-timeout", "inf")):
            with self.subTest(values=values), self.assertRaises(ValueError):
                demo.commands(self.args(*values), Path("/tmp/preview"))

    def test_output_scope_and_overwrite_guards(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with patch.object(demo.ref, "OUTPUT_ROOT", root):
                self.assertEqual(demo.fresh_output(root / "fresh"), root / "fresh")
                (root / "existing").mkdir()
                (root / "link").symlink_to(root / "existing", target_is_directory=True)
                for p in (root, root / "existing", root / "link/new", root / "missing/new", root.parent / "outside"):
                    with self.subTest(p=p), self.assertRaises(ValueError):
                        demo.fresh_output(p)

    def test_preview_does_not_create_a_leaf_or_spawn(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            out = root / "fresh"
            with patch.object(demo.ref, "OUTPUT_ROOT", root), patch.object(demo, "preflight", return_value={}), \
                    patch.object(demo.subprocess, "Popen", side_effect=AssertionError("preview spawned")):
                self.assertEqual(demo.main(["replay", "--out", str(out), "--print-command"]), 0)
                self.assertFalse(out.exists())

    def test_missing_native_does_not_install_build_or_fallback(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / "fake-app"
            app.write_bytes(b"not executed")
            app.chmod(0o755)
            a = self.args("live", "--app", str(app), "--native-python", "/definitely/missing/native-python")
            with patch.object(demo.ref, "sha256_file", return_value="hash"), \
                    patch.object(demo.ref, "shader_identity", return_value={}), \
                    patch.object(demo.subprocess, "Popen", side_effect=AssertionError("preflight spawned")):
                with self.assertRaisesRegex(ValueError, "no install/build/fallback"):
                    demo.preflight(a)

    def test_owned_cleanup_leaves_unrelated_process_running(self):
        def child():
            return subprocess.Popen(["rtk", "proxy", sys.executable, "-c", "import time; time.sleep(30)"], start_new_session=True)
        own, other = child(), child()
        try:
            receipt = demo.stop_owned(own, 1)
            self.assertEqual(receipt["active_group_remaining"], [])
            self.assertIsNone(other.poll())
            self.assertTrue(demo.active_owned_group(other.pid))
        finally:
            demo.stop_owned(own, 1)
            demo.stop_owned(other, 1)

    def test_cleanup_includes_child_after_wrapper_exits(self):
        code = "import subprocess,sys; subprocess.Popen([sys.executable,'-c','import time; time.sleep(30)'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);"
        p = subprocess.Popen(["rtk", "proxy", sys.executable, "-c", code], start_new_session=True)
        try:
            p.wait(timeout=5)
            self.assertTrue(demo.active_owned_group(p.pid))
            self.assertEqual(demo.stop_owned(p, 1)["active_group_remaining"], [])
        finally:
            demo.stop_owned(p, 1)

    def test_readiness_fails_for_dead_worker_or_timeout(self):
        dead = subprocess.Popen(["rtk", "proxy", sys.executable, "-c", "pass"], start_new_session=True)
        dead.wait(timeout=5)
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(RuntimeError, "exited before readiness"):
                demo.wait_ready(dead, Path(temp), .2)
            alive = subprocess.Popen(["rtk", "proxy", sys.executable, "-c", "import time; time.sleep(30)"], start_new_session=True)
            try:
                with self.assertRaises(TimeoutError):
                    demo.wait_ready(alive, Path(temp), .1)
            finally:
                demo.stop_owned(alive, 1)

    def test_live_detach_keeps_owned_worker_until_launcher_interrupt(self):
        # Synthetic process protocol only; not evidence of native solver behavior.
        worker_code = """import json,pathlib,signal,time
p=pathlib.Path('session'); p.mkdir()
def stop(*args):
 (p/'completion.json').write_text(json.dumps({'lifecycle':'stopped','inputs_unchanged':True,'reference_inputs_unchanged':True}))
 raise SystemExit(0)
signal.signal(signal.SIGTERM,stop)
(p/'state.json').write_text(json.dumps({'schema':'cubey.fluid25d.external-state.v1','frame':{'lifecycle':'paused','reset_generation':1,'physical_time_s':0},'pacing':60,'published_unix_s':time.time()}))
while True: time.sleep(.05)
"""
        with tempfile.TemporaryDirectory() as temp:
            out, observed = Path(temp), {}
            argv = {"worker": ["rtk", "proxy", sys.executable, "-c", worker_code],
                    "viewer": ["rtk", "proxy", sys.executable, "-c", "pass"]}
            def interrupt_after_detach():
                deadline = time.monotonic() + 3
                while not (out / "detached.json").exists() and time.monotonic() < deadline:
                    time.sleep(.02)
                if (out / "detached.json").exists():
                    pid = json.loads((out / "detached.json").read_text())["worker_pid"]
                    observed["still_active"] = bool(demo.active_owned_group(pid))
                os.kill(os.getpid(), signal.SIGINT)
            thread = threading.Thread(target=interrupt_after_detach)
            thread.start()
            try:
                with patch.object(demo, "commands", return_value=argv):
                    self.assertEqual(demo.launch(self.args("live"), out, {}), 130)
            finally:
                thread.join(timeout=5)
            self.assertTrue(observed["still_active"])
            result = json.loads((out / "result.json").read_text())
            self.assertTrue(result["viewer_detached"])
            self.assertEqual(result["failure"], "")
            self.assertEqual(result["completion"]["lifecycle"], "stopped")
            self.assertFalse(any(row["escalated"] for row in result["owned_cleanup"]))


if __name__ == "__main__":
    unittest.main()
