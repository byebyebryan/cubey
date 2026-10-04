"""Receipt contract and no-overwrite tests for the V4 geometry study runner."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import run_geometry_study_v4 as runner


def report_for(mode: str, *, diagnostic: bool = True, acceptance: bool = False) -> dict:
    expected_mode, acceptance_key = runner.REPORT_CONTRACT[mode]
    report = {
        "schema": runner.REPORT_SCHEMA,
        "mode": expected_mode,
        "diagnostic_success": diagnostic,
        acceptance_key: acceptance,
    }
    if mode in ("full", "probe"):
        report["conservation_health_pass"] = False
    if mode == "full":
        report["conservation_health_pass"] = False
    if mode == "self-test":
        report["historical_accuracy_gates_evaluated"] = False
    return report


class GeometryStudyReceiptTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base / "repo"
        self.root.mkdir()
        for relative in runner.SOURCE_PATHS:
            source = self.root / relative
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text(f"provenance:{relative}\n", encoding="utf-8")
        self.baseline = self.base / "baseline.json"
        self.baseline.write_text('{"schema":"retained baseline"}\n', encoding="utf-8")
        self.output = self.base / "receipt"

    def make_binary(
        self,
        report: dict | None,
        *,
        exit_code: int,
        stderr: str = "",
        stdout: str | None = None,
        mutate_source: Path | None = None,
        mutate_binary: bool = False,
    ) -> Path:
        binary = self.base / "fake_geometry_benchmark"
        if stdout is None:
            stdout = json.dumps(report) if report is not None else "not-json"
        source_mutation = ""
        if mutate_source is not None:
            source_mutation = (
                f"p = Path({str(mutate_source)!r})\n"
                "p.write_text(p.read_text(encoding='utf-8') + '# changed\\n', encoding='utf-8')\n"
            )
        binary_mutation = ""
        if mutate_binary:
            binary_mutation = (
                "p = Path(__file__)\n"
                "p.write_bytes(p.read_bytes() + b'\\n# changed\\n')\n"
            )
        script = (
            "#!/usr/bin/env python3\n"
            "from pathlib import Path\n"
            f"{source_mutation}{binary_mutation}"
            f"print({stdout!r})\n"
            f"print({stderr!r}, file=__import__('sys').stderr)\n"
            f"raise SystemExit({exit_code})\n"
        )
        binary.write_text(script, encoding="utf-8")
        binary.chmod(binary.stat().st_mode | 0o111)
        return binary

    def run_receipt(
        self,
        report: dict | None,
        *,
        exit_code: int,
        **binary_options,
    ) -> tuple[int, dict]:
        binary = self.make_binary(report, exit_code=exit_code, **binary_options)
        return runner.execute_receipt(
            binary=binary,
            output=self.output,
            mode="full"
            if report is None or report.get("mode") == "full_cpu_report"
            else "self-test",
            baseline=self.baseline,
            root=self.root,
        )

    def test_command_modes_exclude_fallback_and_contracts_are_distinct(self) -> None:
        self.assertEqual(runner.command_for(Path("/bench"), "full"), ["/bench"])
        self.assertEqual(
            runner.command_for(Path("/bench"), "probe"), ["/bench", "--probe"]
        )
        self.assertEqual(
            runner.command_for(Path("/bench"), "self-test"), ["/bench", "--self-test"]
        )
        self.assertNotIn("fallback-probe", runner.MODE_ARGUMENTS)
        self.assertEqual(
            runner.REPORT_CONTRACT["self-test"][1], "implementation_self_test_pass"
        )

    def test_report_contract_retains_numerical_negative_exit_two(self) -> None:
        report = report_for("full", diagnostic=True, acceptance=False)
        self.assertEqual(runner.validate_report(report, "full", 2), [])
        self.assertEqual(runner.validate_report(report_for("probe"), "probe", 2), [])

    def test_self_test_contract_does_not_use_historical_acceptance(self) -> None:
        report = report_for("self-test", diagnostic=True, acceptance=True)
        report["historical_accuracy_gates_evaluated"] = False
        self.assertEqual(runner.validate_report(report, "self-test", 0), [])
        report["historical_accuracy_gates_evaluated"] = True
        self.assertIn(
            "self-test must declare historical_accuracy_gates_evaluated=false",
            runner.validate_report(report, "self-test", 0),
        )

    def test_malformed_schema_mode_booleans_and_exit_mismatch_are_rejected(
        self,
    ) -> None:
        report = report_for("full")
        report["schema"] = "wrong"
        report["mode"] = "probe"
        report["candidate_acceptance_pass"] = "false"
        errors = runner.validate_report(report, "full", 0)
        self.assertTrue(any("schema" in error for error in errors))
        self.assertTrue(any("mode" in error for error in errors))
        self.assertTrue(any("must be a boolean" in error for error in errors))
        mismatch = runner.validate_report(report_for("full"), "full", 0)
        self.assertTrue(any("exit code" in error for error in mismatch))

    def test_diagnostic_failure_cannot_pass_acceptance(self) -> None:
        report = report_for("full", diagnostic=False, acceptance=True)
        errors = runner.validate_report(report, "full", 1)
        self.assertIn("acceptance cannot pass when diagnostics fail", errors)

    def test_receipt_preserves_negative_report_logs_hashes_and_refuses_reuse(
        self,
    ) -> None:
        report = report_for("full", diagnostic=True, acceptance=False)
        code, receipt = self.run_receipt(
            report,
            exit_code=2,
            stderr="study diagnostic stream",
        )
        self.assertEqual(code, 2)
        self.assertTrue(receipt["report_contract_valid"])
        self.assertTrue(receipt["source_inputs_unchanged"])
        self.assertTrue(receipt["binary_unchanged"])
        self.assertTrue(receipt["legacy_cpu_oracle_comparison_included"])
        self.assertEqual(
            (self.output / "stderr.raw").read_text(), "study diagnostic stream\n"
        )
        self.assertEqual(json.loads((self.output / "report.json").read_text()), report)
        with self.assertRaises(FileExistsError):
            runner.execute_receipt(
                binary=self.base / "fake_geometry_benchmark",
                output=self.output,
                mode="full",
                baseline=self.baseline,
                root=self.root,
            )

    def test_receipt_rejects_exit_mismatch_but_keeps_report_and_streams(self) -> None:
        report = report_for("full", diagnostic=True, acceptance=False)
        binary = self.make_binary(report, exit_code=0, stderr="must be retained")
        code, receipt = runner.execute_receipt(
            binary=binary,
            output=self.output,
            mode="full",
            baseline=self.baseline,
            root=self.root,
        )
        self.assertEqual(code, 1)
        self.assertFalse(receipt["report_contract_valid"])
        self.assertTrue((self.output / "report.json").is_file())
        self.assertEqual((self.output / "stderr.raw").read_text(), "must be retained\n")

    def test_receipt_detects_source_changes(self) -> None:
        report = report_for("self-test", diagnostic=True, acceptance=True)
        changed = self.root / runner.SOURCE_PATHS[0]
        binary = self.make_binary(report, exit_code=0, mutate_source=changed)
        code, receipt = runner.execute_receipt(
            binary=binary,
            output=self.output,
            mode="self-test",
            baseline=self.baseline,
            root=self.root,
        )
        self.assertEqual(code, 1)
        self.assertFalse(receipt["source_inputs_unchanged"])

    def test_receipt_detects_binary_changes(self) -> None:
        report = report_for("self-test", diagnostic=True, acceptance=True)
        binary = self.make_binary(report, exit_code=0, mutate_binary=True)
        code, receipt = runner.execute_receipt(
            binary=binary,
            output=self.output,
            mode="self-test",
            baseline=self.baseline,
            root=self.root,
        )
        self.assertEqual(code, 1)
        self.assertFalse(receipt["binary_unchanged"])

    def test_malformed_stdout_is_retained_and_fails_closed(self) -> None:
        binary = self.make_binary(None, exit_code=0, stdout="[]", stderr="bad report")
        code, receipt = runner.execute_receipt(
            binary=binary,
            output=self.output,
            mode="self-test",
            baseline=self.baseline,
            root=self.root,
        )
        self.assertEqual(code, 1)
        self.assertFalse(receipt["report_json_valid"])
        self.assertTrue((self.output / "report-parse-error.json").is_file())
        self.assertEqual((self.output / "stdout.raw").read_text().strip(), "[]")


if __name__ == "__main__":
    unittest.main()
