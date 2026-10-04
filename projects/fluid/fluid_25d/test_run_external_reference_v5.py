"""Fast standard-library tests for the reference runner's safe CLI contract."""

from __future__ import annotations

import importlib.util
import contextlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest import mock


RUNNER_PATH = Path(__file__).with_name("run_external_reference_v5.py")
SPEC = importlib.util.spec_from_file_location("external_reference_v5", RUNNER_PATH)
assert SPEC is not None and SPEC.loader is not None
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class OutputLeafValidationTest(unittest.TestCase):
    def test_accepts_single_fresh_leaf_name(self):
        self.assertEqual(RUNNER._validate_output_leaf_name("paper-run-1"), "paper-run-1")

    def test_rejects_ambiguous_or_traversing_output_paths(self):
        for name in ("", ".", "..", "../other", "nested/run", "/tmp/run"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                RUNNER._validate_output_leaf_name(name)


class SampleTimeValidationTest(unittest.TestCase):
    def test_parses_ordered_times_within_horizon(self):
        self.assertEqual(
            RUNNER._parse_sample_times("60,120,300,600", 600.0),
            (60, 120, 300, 600),
        )

    def test_rejects_empty_noninteger_duplicate_unordered_and_future_times(self):
        for text, horizon in (
            ("", 600.0),
            ("0,60", 600.0),
            ("60,x", 600.0),
            ("60,60", 600.0),
            ("120,60", 600.0),
            ("60,7200", 600.0),
        ):
            with self.subTest(text=text), self.assertRaises(ValueError):
                RUNNER._parse_sample_times(text, horizon)


class FreshOutputAndPortabilityTest(unittest.TestCase):
    def test_existing_output_leaf_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as temp:
            cases = Path(temp) / "reference-cases"
            with mock.patch.object(RUNNER, "CASES_ROOT", cases):
                leaf = RUNNER._new_output_leaf("case-one")
                marker = leaf / "marker.txt"
                marker.write_text("preserve this receipt\n")
                with self.assertRaises(FileExistsError):
                    RUNNER._new_output_leaf("case-one")
                self.assertEqual(marker.read_text(), "preserve this receipt\n")

    def test_portable_study_root_rebases_setup_and_case_paths(self):
        previous = (RUNNER.STUDY_ROOT, RUNNER.SETUP_ROOT, RUNNER.CASES_ROOT)
        try:
            with tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                (root / "reference-setup").mkdir()
                RUNNER._configure_study_root(root)
                self.assertEqual(RUNNER.STUDY_ROOT, root.resolve())
                self.assertEqual(RUNNER.SETUP_ROOT, root.resolve() / "reference-setup")
                self.assertEqual(RUNNER.CASES_ROOT, root.resolve() / "reference-cases")
        finally:
            RUNNER.STUDY_ROOT, RUNNER.SETUP_ROOT, RUNNER.CASES_ROOT = previous

    def test_invalid_horizon_and_timestep_fail_before_creating_a_case(self):
        with tempfile.TemporaryDirectory() as temp:
            cases = Path(temp) / "reference-cases"
            bad_invocations = (
                ["--mode", "mountain-timing", "--output", "bad-horizon", "--horizon", "nan"],
                ["--mode", "mountain-timing", "--output", "bad-zero", "--horizon", "0"],
                ["--mode", "mountain-timing", "--output", "too-long", "--horizon", "86401"],
                ["--mode", "mountain-timing", "--output", "future-sample", "--times", "60"],
                ["--mode", "flat-rain", "--output", "bad-dt", "--dt-cap", "inf"],
                ["--mode", "flat-rain", "--output", "bad-small-dt", "--dt-cap", "0.0001"],
            )
            with mock.patch.object(RUNNER, "CASES_ROOT", cases):
                for invocation in bad_invocations:
                    with self.subTest(invocation=invocation):
                        with contextlib.redirect_stderr(io.StringIO()):
                            with self.assertRaises(SystemExit):
                                RUNNER.main(invocation)
                        self.assertFalse(cases.exists())


if __name__ == "__main__":
    unittest.main()
