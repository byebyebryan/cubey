# SPDX-License-Identifier: GPL-3.0-only
"""CPU-only tests for the explicit, pinned SynxFlow service build helper."""

from __future__ import annotations

from argparse import Namespace
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import build_synxflow_service_v1 as build_helper


class PinnedBuildHelperTests(unittest.TestCase):
    def test_run_extractor_keeps_stock_entry_point_text(self) -> None:
        source = (
            "preamble\n"
            "int run(const char* work_dir){\n"
            "    //calculate the surface elevation\n"
            "    original_operator();\n"
            "    //update maximum depth\n"
            "    return 0;\n"
            "}\n\n"
            "Scalar dt_out = 0.5;\n"
        )
        run, _ = build_helper.extract_stock_run(source)
        self.assertEqual(run, source[source.index("int run("):source.index("\n\nScalar dt_out")])

    def test_operator_verifier_allows_only_the_deliberate_rain_overlay(self) -> None:
        stock = (
            "int run(const char* work_dir){\n"
            "    //calculate the surface elevation\n"
            "    update_depth();\n"
            "    precipitation.update_data_values();\n"
            "    source_sink();\n"
            "    //update maximum depth\n"
            "}\n"
        )
        overlay = (
            "    if (rain_override_active) {\n"
            "      fv::cuUnaryOn(precipitation, [rain_override_m_per_s] __device__(Scalar& a) -> Scalar{ return rain_override_m_per_s; });\n"
            "    }\n"
        )
        service = stock.replace(
            "int run(const char* work_dir){", "int run_service(const char* work_dir){", 1
        ).replace("    //update maximum depth", overlay + "    //update maximum depth", 1)
        build_helper.verify_operator_body(stock, service)

        altered = service.replace("    source_sink();", "    changed_source_sink();", 1)
        with self.assertRaises(build_helper.BuildError):
            build_helper.verify_operator_body(stock, altered)

    def test_source_audit_accepts_exact_app_changes_only(self) -> None:
        before = {
            str(build_helper.CU_REL): "stock-cu",
            str(build_helper.DECL_REL): "stock-decl",
            str(build_helper.BINDING_REL): "stock-binding",
        }
        after = {
            str(build_helper.CU_REL): "service-cu",
            str(build_helper.DECL_REL): "service-decl",
            str(build_helper.BINDING_REL): "service-binding",
            str(build_helper.HOOK_REL): "new-hook",
        }
        self.assertEqual(
            build_helper.audit_changed_files(before, after),
            sorted(after),
        )

        after["synxflow/lib/src/untouched_operator.cu"] = "changed-library"
        with self.assertRaises(build_helper.BuildError):
            build_helper.audit_changed_files(before, after)

    def test_wrong_archive_pin_fails_before_toolchain_or_build_commands(self) -> None:
        with tempfile.TemporaryDirectory(prefix="synxflow-pin-test-") as temporary:
            archive = Path(temporary) / "wrong.tar.gz"
            archive.write_bytes(b"not the pinned upstream source")
            args = Namespace(
                source_archive=archive,
                out=build_helper.REPO_DIR / "private-builds" / "must-not-be-created",
                cuda_root=Path(temporary) / "missing-cuda",
                python=Path(temporary) / "missing-python",
                cc=Path(temporary) / "missing-cc",
                cxx=Path(temporary) / "missing-cxx",
                jobs=8,
            )
            with mock.patch.object(build_helper.subprocess, "run") as run_mock, \
                    mock.patch.object(build_helper.subprocess, "check_output") as output_mock:
                with self.assertRaisesRegex(build_helper.BuildError, "SHA-256"):
                    build_helper.build(args)
                run_mock.assert_not_called()
                output_mock.assert_not_called()
            self.assertFalse(args.out.exists())

    def test_pinned_archive_patch_changes_only_declared_app_files(self) -> None:
        repository = build_helper.REPO_DIR.parents[3]
        archive = (
            repository
            / "outputs/fluid/backend-integration-v2-20261004-wlELKz/downloads/"
            "synxflow-github-1.0.1-8a978150.tar.gz"
        )
        if not archive.is_file():
            self.skipTest("retained pinned source archive is unavailable in this checkout")
        self.assertEqual(build_helper.sha256(archive), build_helper.ARCHIVE_SHA256)
        with tempfile.TemporaryDirectory(prefix="synxflow-patch-test-", dir=build_helper.REPO_DIR) as temporary:
            root = Path(temporary)
            source = build_helper._safe_extract(archive, root)
            before = build_helper.source_file_hashes(source)
            build_helper.patch_flood_sources(source)
            after = build_helper.source_file_hashes(source)
            changed = build_helper.audit_changed_files(before, after)
            self.assertEqual(changed, sorted(after_path for after_path in after if after_path not in before or before[after_path] != after.get(after_path)))
            cu = (source / build_helper.CU_REL).read_text(encoding="utf-8")
            stock_run, _ = build_helper.extract_stock_run(cu)
            self.assertIn("int run_service(", cu)
            self.assertEqual(stock_run.count("int run(const char* work_dir){"), 1)
            self.assertIn("++consecutive_zero_steps", cu)
            self.assertIn("consecutive_zero_steps > 1", cu)
            self.assertIn("previous_step_dt != 0.0f", cu)
            self.assertIn("time_controller.current() != t_out", cu)
            self.assertIn("cudaDeviceSynchronize(snapshot)", (source / build_helper.HOOK_REL).read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
