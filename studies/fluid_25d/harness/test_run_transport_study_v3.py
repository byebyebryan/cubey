from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import run_transport_study_v3 as runner


def executable(path: Path, source: str) -> Path:
    path.write_text(source, encoding="utf-8")
    path.chmod(0o755)
    return path


def report_json(mode: str, diagnostic: bool = True, acceptance: bool = True) -> dict[str, object]:
    acceptance_key = (
        "fallback_probe_acceptance_pass" if mode == "bounded_fallback_probe"
        else "candidate_acceptance_pass"
    )
    fields = {
        "schema": runner.REPORT_SCHEMA,
        "mode": mode,
        "diagnostic_success": diagnostic,
        acceptance_key: acceptance,
    }
    if mode == "full_cpu_report":
        fields["conservation_health_pass"] = diagnostic
    return fields


class TransportStudyReceiptTests(unittest.TestCase):
    def test_negative_acceptance_keeps_report_logs_baseline_and_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            valid_report = {
                "schema": runner.REPORT_SCHEMA,
                "mode": "probe",
                "diagnostic_success": True,
                "candidate_acceptance_pass": False,
            }
            binary = executable(
                root / "fake-study",
                "#!/usr/bin/env python3\n"
                "import json, sys\n"
                f"print(json.dumps({valid_report!r}))\n"
                "print('retained stderr', file=sys.stderr)\n"
                "raise SystemExit(2)\n",
            )
            baseline = root / "baseline-transport.json"
            baseline_bytes = b'{"retained":"v2 baseline"}\n'
            baseline.write_bytes(baseline_bytes)
            output = root / "run-01"

            code, receipt = runner.execute_receipt(
                binary=binary,
                output=output,
                mode="probe",
                baseline=baseline,
                root=runner.ROOT,
            )

            self.assertEqual(code, 2)
            self.assertFalse(receipt["candidate_acceptance_pass"])
            self.assertEqual((output / "baseline-transport.json").read_bytes(), baseline_bytes)
            stdout = (output / "stdout.raw").read_bytes()
            stderr = (output / "stderr.raw").read_bytes()
            self.assertEqual(json.loads((output / "report.json").read_text()), valid_report)
            self.assertIn(b"retained stderr", stderr)
            self.assertEqual(receipt["stdout_raw_sha256"], hashlib.sha256(stdout).hexdigest())
            self.assertEqual(receipt["stderr_raw_sha256"], hashlib.sha256(stderr).hexdigest())
            self.assertEqual(receipt["report_sha256"],
                             hashlib.sha256((output / "report.json").read_bytes()).hexdigest())
            self.assertEqual(receipt["baseline_snapshot"]["sha256"],
                             hashlib.sha256(baseline_bytes).hexdigest())
            self.assertTrue(receipt["source_inputs_unchanged"])
            self.assertTrue(receipt["binary_unchanged"])
            self.assertTrue(receipt["report_contract_valid"])
            self.assertEqual(receipt["benchmark_exit_code"], 2)
            self.assertEqual((output / "execution-receipt.json").is_file(), True)

    def test_existing_output_is_refused_without_overwriting_anything(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = executable(root / "fake-study", "#!/bin/sh\nexit 0\n")
            baseline = root / "baseline.json"
            baseline.write_text("{}\n", encoding="utf-8")
            output = root / "existing-run"
            output.mkdir()
            sentinel = output / "keep.txt"
            sentinel.write_text("preserve", encoding="utf-8")

            with self.assertRaises(FileExistsError):
                runner.execute_receipt(binary=binary, output=output, mode="full",
                                       baseline=baseline, root=runner.ROOT)

            self.assertEqual(sentinel.read_text(encoding="utf-8"), "preserve")
            self.assertEqual(sorted(path.name for path in output.iterdir()), ["keep.txt"])

    def test_invalid_json_is_still_retained_and_receipted_as_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = executable(root / "fake-study", "#!/bin/sh\nprintf 'not-json\\n'\nexit 0\n")
            baseline = root / "baseline.json"
            baseline.write_text("{}\n", encoding="utf-8")
            output = root / "run-invalid"

            code, receipt = runner.execute_receipt(
                binary=binary, output=output, mode="self-test", baseline=baseline,
                root=runner.ROOT,
            )

            self.assertEqual(code, 1)
            self.assertFalse(receipt["report_json_valid"])
            self.assertEqual((output / "stdout.raw").read_bytes(), b"not-json\n")
            self.assertTrue((output / "report-parse-error.json").is_file())
            self.assertTrue((output / "execution-receipt.json").is_file())
            self.assertFalse((output / "report.json").exists())

    def test_json_array_root_is_not_accepted_as_a_report(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = executable(root / "fake-study", "#!/bin/sh\nprintf '[]\\n'\nexit 0\n")
            baseline = root / "baseline.json"
            baseline.write_text("{}\n", encoding="utf-8")
            output = root / "run-array"

            code, receipt = runner.execute_receipt(
                binary=binary, output=output, mode="probe", baseline=baseline,
                root=runner.ROOT,
            )

            self.assertEqual(code, 1)
            self.assertFalse(receipt["report_json_valid"])
            self.assertIn("root is not an object", receipt["report_parse_error"])
            self.assertEqual((output / "stdout.raw").read_bytes(), b"[]\n")
            self.assertTrue((output / "execution-receipt.json").is_file())

    def test_false_health_cannot_be_reported_as_success(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            report = report_json("probe", diagnostic=False, acceptance=False)
            binary = executable(
                root / "fake-study",
                "#!/usr/bin/env python3\n"
                "import json\n"
                f"print(json.dumps({report!r}))\n"
                "raise SystemExit(0)\n",
            )
            baseline = root / "baseline.json"
            baseline.write_text("{}\n", encoding="utf-8")
            output = root / "run-false-health"

            code, receipt = runner.execute_receipt(
                binary=binary, output=output, mode="probe", baseline=baseline,
                root=runner.ROOT,
            )

            self.assertEqual(code, 1)
            self.assertFalse(receipt["diagnostic_success"])
            self.assertFalse(receipt["report_contract_valid"])
            self.assertTrue(any("conflicts with report verdict" in error
                                for error in receipt["report_validation_errors"]))
            self.assertTrue((output / "report.json").is_file())

    def test_exit_code_must_match_negative_acceptance_verdict(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            report = report_json("probe", diagnostic=True, acceptance=False)
            binary = executable(
                root / "fake-study",
                "#!/usr/bin/env python3\n"
                "import json\n"
                f"print(json.dumps({report!r}))\n"
                "raise SystemExit(0)\n",
            )
            baseline = root / "baseline.json"
            baseline.write_text("{}\n", encoding="utf-8")
            output = root / "run-exit-mismatch"

            code, receipt = runner.execute_receipt(
                binary=binary, output=output, mode="probe", baseline=baseline,
                root=runner.ROOT,
            )

            self.assertEqual(code, 1)
            self.assertFalse(receipt["report_contract_valid"])
            self.assertTrue(any("expected 2" in error
                                for error in receipt["report_validation_errors"]))
            self.assertEqual(receipt["benchmark_exit_code"], 0)
            self.assertTrue((output / "stderr.raw").is_file())

    def test_binary_change_during_run_fails_the_receipt(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = executable(
                root / "fake-study",
                "#!/usr/bin/env python3\n"
                "from pathlib import Path\n"
                "import json, sys\n"
                f"print(json.dumps({report_json('self_test')!r}))\n"
                "with Path(sys.argv[0]).open('ab') as executable_file:\n"
                "    executable_file.write(b'\\n# changed while running\\n')\n"
                "raise SystemExit(0)\n",
            )
            baseline = root / "baseline.json"
            baseline.write_text("{}\n", encoding="utf-8")
            output = root / "run-binary-changed"

            code, receipt = runner.execute_receipt(
                binary=binary, output=output, mode="self-test", baseline=baseline,
                root=runner.ROOT,
            )

            self.assertEqual(code, 1)
            self.assertFalse(receipt["binary_unchanged"])
            self.assertNotEqual(receipt["binary_sha256_before"],
                                receipt["binary_sha256_after"])
            self.assertTrue(receipt["report_contract_valid"])
            self.assertTrue((output / "report.json").is_file())
            self.assertTrue((output / "execution-receipt.json").is_file())

    def test_modes_select_only_the_focused_cpu_benchmark_flags(self) -> None:
        binary = Path("/tmp/study-benchmark")
        self.assertEqual(runner.command_for(binary, "full"), [str(binary)])
        self.assertEqual(runner.command_for(binary, "probe"), [str(binary), "--probe"])
        self.assertEqual(runner.command_for(binary, "fallback-probe"),
                         [str(binary), "--fallback-probe"])
        self.assertEqual(runner.command_for(binary, "self-test"), [str(binary), "--self-test"])
        with self.assertRaises(ValueError):
            runner.command_for(binary, "mountain")


if __name__ == "__main__":
    unittest.main()
