"""Failure, interruption and contention contracts for generated bundle publication."""
import contextlib
import errno
import fcntl
import io
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile
import unittest
from types import ModuleType
from unittest.mock import MagicMock, patch

from terrain_bundle import staged_bundle


class BundleTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.destination = self.root / "asset"

    def old_bundle(self):
        self.destination.mkdir()
        (self.destination / "old.bin").write_bytes(b"qualified previous payload")

    def assert_old(self, path=None):
        self.assertEqual((path or self.destination).joinpath("old.bin").read_bytes(),
                         b"qualified previous payload")

    def backups(self):
        return list(self.root.glob(".asset.previous.*"))

    def test_success_installs_exact_bundle_and_replaces_without_merging(self):
        for existing in (False, True):
            with self.subTest(existing=existing):
                if existing:
                    (self.destination / "obsolete.bin").write_bytes(b"obsolete")
                with staged_bundle(self.destination) as staging:
                    (staging / "new.bin").write_bytes(b"complete new payload")
                    self.assertEqual(self.destination.exists(), existing)
                self.assertEqual(list(self.destination.iterdir()), [self.destination / "new.bin"])
                self.assertEqual((self.destination / "new.bin").read_bytes(), b"complete new payload")
                self.assertFalse(staging.exists())
                self.assertEqual(self.backups(), [])

    def test_named_child_publishes_only_the_selected_region(self):
        with staged_bundle(self.destination, bundle_name="region") as staging:
            (staging / "region").mkdir()
            (staging / "region/field.bin").write_bytes(b"region")
            (staging / "other.txt").write_text("not part of the bundle")
        self.assertEqual((self.destination / "field.bin").read_bytes(), b"region")
        self.assertFalse((self.destination / "other.txt").exists())
        self.assertFalse(staging.exists())

    def test_default_generator_keeps_previous_asset_when_publication_fails(self):
        # Exercise the real entry point and manifest writer. Only the external
        # producer and its float writer are replaced with small deterministic
        # fixtures; no CUDA or NumPy is imported.
        import terrain_diffusion_bake as bake
        self.old_bundle()
        torch = MagicMock()
        torch.cuda.is_available.return_value = True
        world = MagicMock(native_resolution=bake.MODEL_NATIVE_RESOLUTION_M)
        pipeline = MagicMock()
        pipeline.WorldPipeline.from_pretrained.return_value = world
        hub = MagicMock()
        hub.snapshot_download.return_value = str(self.root)
        modules = {"torch": torch, "huggingface_hub": hub,
                   "terrain_diffusion": ModuleType("terrain_diffusion"),
                   "terrain_diffusion.inference": ModuleType("terrain_diffusion.inference"),
                   "terrain_diffusion.inference.world_pipeline": pipeline}
        def write_float_fixture(path, values):
            path.write_bytes(b"fixture elevation")
            return {"sha256": bake.DEFAULT_ASSET_ELEVATION_SHA256, "path": path.name}
        real_replace = os.replace
        def fail_install(source, target):
            if Path(source).name.startswith("asset.tmp."):
                raise OSError(errno.EIO, "fixture install failure")
            return real_replace(source, target)
        with contextlib.ExitStack() as stack:
            stack.enter_context(patch.dict(sys.modules, modules))
            stack.enter_context(patch.object(sys, "path", list(sys.path)))
            stack.enter_context(patch.object(bake, "git_revision", return_value=bake.CODE_REVISION))
            stack.enter_context(patch.object(bake, "_prepare_data_cache"))
            stack.enter_context(patch.object(bake, "_working_directory", return_value=contextlib.nullcontext()))
            stack.enter_context(patch.object(bake, "_seed_process_rngs"))
            stack.enter_context(patch.object(bake, "_generate_seed", return_value={
                "elevation": None, "model_native_origin": [0, 0]}))
            stack.enter_context(patch.object(bake, "write_f32", side_effect=write_float_fixture))
            stack.enter_context(patch("terrain_bundle.os.replace", side_effect=fail_install))
            with self.assertRaisesRegex(OSError, "fixture install failure"):
                bake.bake_default_asset(self.root, self.destination, self.root)
        self.assert_old()
        staging, = self.root.glob("asset.tmp.*")
        self.assertEqual((staging / "elevation.f32").read_bytes(), b"fixture elevation")
        self.assertTrue((staging / "heightfield.json").is_file())
        world.close.assert_called_once()

    def test_generation_failure_or_interrupt_discards_partial_staging_only(self):
        self.old_bundle()
        for error in (ValueError("invalid generated hash"), KeyboardInterrupt()):
            with self.subTest(error=type(error).__name__):
                with self.assertRaises(type(error)):
                    with staged_bundle(self.destination) as staging:
                        (staging / "partial.bin").write_bytes(b"partial")
                        raise error
                self.assert_old()
                self.assertFalse(staging.exists())
                self.assertEqual(self.backups(), [])

    def test_backup_rename_failure_retains_old_and_generated_bundles(self):
        self.old_bundle()
        with patch("terrain_bundle.os.replace", side_effect=OSError(errno.EIO, "backup failure")):
            with self.assertRaises(OSError) as caught:
                with staged_bundle(self.destination) as staging:
                    (staging / "new.bin").write_bytes(b"new")
        self.assert_old()
        self.assertEqual((staging / "new.bin").read_bytes(), b"new")
        self.assertIn(str(staging), " ".join(caught.exception.__notes__))
        self.assertEqual(self.backups(), [])

    def test_install_failure_restores_previous_and_retains_complete_staging(self):
        self.old_bundle()
        real_replace = os.replace
        def fail_install(source, target):
            if Path(source) == staging:
                raise OSError(errno.EIO, "install failure")
            return real_replace(source, target)
        with patch("terrain_bundle.os.replace", side_effect=fail_install):
            with self.assertRaisesRegex(OSError, "install failure"):
                with staged_bundle(self.destination) as staging:
                    (staging / "new.bin").write_bytes(b"complete")
        self.assert_old()
        self.assertEqual((staging / "new.bin").read_bytes(), b"complete")
        self.assertEqual(self.backups(), [])

    def test_first_install_failure_also_retains_generated_bundle(self):
        with patch("terrain_bundle.os.replace", side_effect=OSError(errno.EIO, "install failure")):
            with self.assertRaises(OSError):
                with staged_bundle(self.destination) as staging:
                    (staging / "new.bin").write_bytes(b"complete")
        self.assertFalse(self.destination.exists())
        self.assertEqual((staging / "new.bin").read_bytes(), b"complete")

    def test_later_generation_preserves_earlier_recovery_directories(self):
        recovery = self.root / ".asset.previous.retained"
        recovery.mkdir()
        (recovery / "old.bin").write_bytes(b"qualified previous payload")
        with staged_bundle(self.destination) as staging:
            (staging / "new.bin").write_bytes(b"new")
        self.assert_old(recovery)
        self.assertEqual((self.destination / "new.bin").read_bytes(), b"new")

    def test_rollback_failure_keeps_previous_backup_and_reports_its_path(self):
        self.old_bundle()
        real_replace = os.replace
        def fail_after_backup(source, target):
            if Path(source) != self.destination:
                raise OSError(errno.EIO, "install/rollback failure")
            return real_replace(source, target)
        with patch("terrain_bundle.os.replace", side_effect=fail_after_backup):
            with self.assertRaisesRegex(RuntimeError, "rollback failed") as caught:
                with staged_bundle(self.destination) as staging:
                    (staging / "new.bin").write_bytes(b"complete")
        self.assertFalse(self.destination.exists())
        backup, = self.backups()
        self.assert_old(backup)
        self.assertIn(str(backup), str(caught.exception))
        self.assertEqual((staging / "new.bin").read_bytes(), b"complete")

    def test_interrupt_at_either_rename_restores_previous_bundle(self):
        self.old_bundle()
        real_replace = os.replace
        for phase in ("after-backup", "before-install"):
            def interrupt(source, target):
                if phase == "before-install" and Path(source) == staging:
                    raise KeyboardInterrupt()
                result = real_replace(source, target)
                if phase == "after-backup" and Path(source) == self.destination:
                    raise KeyboardInterrupt()
                return result
            with self.subTest(phase=phase), patch("terrain_bundle.os.replace", side_effect=interrupt):
                with self.assertRaises(KeyboardInterrupt):
                    with staged_bundle(self.destination) as staging:
                        (staging / "new.bin").write_bytes(b"complete")
                self.assert_old()
                self.assertTrue(staging.exists())
                self.assertEqual(self.backups(), [])

    def test_cleanup_failure_does_not_turn_activation_into_failure(self):
        self.old_bundle()
        real_remove = shutil.rmtree
        def fail_backup_cleanup(path, *args, **kwargs):
            if Path(path).name.startswith(".asset.previous."):
                raise PermissionError("cleanup failure")
            return real_remove(path, *args, **kwargs)
        stderr = io.StringIO()
        with patch("terrain_bundle.shutil.rmtree", side_effect=fail_backup_cleanup), contextlib.redirect_stderr(stderr):
            with staged_bundle(self.destination) as staging:
                (staging / "new.bin").write_bytes(b"new")
        self.assertEqual((self.destination / "new.bin").read_bytes(), b"new")
        backup, = self.backups()
        self.assert_old(backup)
        self.assertIn(str(backup), stderr.getvalue())

    def test_rejects_symlink_destination_and_escaping_child(self):
        self.old_bundle()
        alias = self.root / "alias"
        alias.symlink_to(self.destination, target_is_directory=True)
        with self.assertRaises(ValueError):
            with staged_bundle(alias) as staging:
                (staging / "new.bin").write_bytes(b"new")
        self.assert_old()
        for child in ("../escape", "/absolute", ".", "", "nested/child"):
            with self.subTest(child=child), self.assertRaises(ValueError):
                with staged_bundle(self.destination, bundle_name=child):
                    self.fail("invalid child was admitted")

    def child(self, code):
        process = subprocess.Popen([sys.executable, "-B", "-u", "-c", code, str(self.destination)],
                                   cwd=Path(__file__).resolve().parent,
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True)
        def cleanup():
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=5)
        self.addCleanup(cleanup)
        return process

    def line(self, process):
        ready, _, _ = select.select([process.stdout], [], [], 5)
        self.assertTrue(ready, "child did not reach the publication boundary")
        line = process.stdout.readline().strip()
        self.assertTrue(line, "child exited before the publication boundary")
        return line

    def test_cooperating_publisher_waits_for_the_destination_lock(self):
        self.old_bundle()
        lock = self.destination.with_name(".asset.publish.lock")
        with lock.open("a+b") as stream:
            fcntl.flock(stream, fcntl.LOCK_EX)
            process = self.child('''
from pathlib import Path
import sys
import terrain_bundle as bundle
real_lock = bundle.fcntl.flock
def lock(*args):
    print("locking", flush=True)
    return real_lock(*args)
bundle.fcntl.flock = lock
with bundle.staged_bundle(Path(sys.argv[1])) as staging:
    (staging / "new.bin").write_bytes(b"new")
''')
            self.assertEqual(self.line(process), "locking")
            self.assertIsNone(process.poll())
            self.assert_old()
            fcntl.flock(stream, fcntl.LOCK_UN)
        stdout, stderr = process.communicate(timeout=5)
        self.assertEqual(process.returncode, 0, stdout + stderr)
        self.assertEqual((self.destination / "new.bin").read_bytes(), b"new")

    def test_killed_publisher_leaves_recoverable_previous_and_staged_bundles(self):
        self.old_bundle()
        process = self.child('''
import os
from pathlib import Path
import sys
import terrain_bundle as bundle
destination = Path(sys.argv[1])
real_replace = os.replace
def stop_after_backup(source, target):
    result = real_replace(source, target)
    if Path(source) == destination:
        print(str(target), flush=True)
        sys.stdin.readline()
    return result
bundle.os.replace = stop_after_backup
with bundle.staged_bundle(destination) as staging:
    (staging / "new.bin").write_bytes(b"complete")
''')
        backup = Path(self.line(process))
        process.kill()
        process.communicate(timeout=5)
        self.assert_old(backup)
        staging, = self.root.glob("asset.tmp.*")
        self.assertEqual((staging / "new.bin").read_bytes(), b"complete")
        self.assertFalse(self.destination.exists())
        os.replace(backup, self.destination)
        self.assert_old()


if __name__ == "__main__":
    unittest.main()
