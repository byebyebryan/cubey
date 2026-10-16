from array import array
import copy
import hashlib
from pathlib import Path
import tempfile
import unittest

import report_captured_water_alignment_v1 as report


class AlignmentReportTests(unittest.TestCase):
    def row(self):
        return dict(inside_disk_coverage_fraction=1, lod0_vs_saved_source_bed_max_error_m=.0005,
                    depth_at_least_0_05m=dict(samples=100, buried_samples=0, buried_fraction=0,
                        large_bed_mismatch_samples=0, large_bed_mismatch_fraction=0,
                        absolute_bed_error_p95_m=.01))

    def test_good_geometry_is_not_rejected(self):
        self.assertFalse(report.rejected(self.row()))

    def test_each_failed_gate_is_retained(self):
        row = self.row()
        row["depth_at_least_0_05m"].update(buried_samples=2, buried_fraction=.02)
        self.assertTrue(report.rejected(row))
        row = self.row()
        row["depth_at_least_0_05m"].update(large_bed_mismatch_samples=6, large_bed_mismatch_fraction=.06)
        self.assertTrue(report.rejected(row))
        for key, value in (("inside_disk_coverage_fraction", .99),
                           ("lod0_vs_saved_source_bed_max_error_m", .002)):
            row = self.row()
            row[key] = value
            self.assertTrue(report.rejected(row))

    def test_empty_nonfinite_or_inconsistent_metrics_never_pass(self):
        for key, value in (("samples", 0), ("buried_fraction", float("nan")),
                           ("buried_samples", 200), ("large_bed_mismatch_fraction", .5)):
            row = self.row()
            row["depth_at_least_0_05m"][key] = value
            with self.assertRaises(ValueError):
                report.rejected(row)

    def test_field_digest_and_size_are_strict(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "field.f32"
            raw = array("f", [1, 2, 3, 4]).tobytes()
            path.write_bytes(raw)
            checksum = hashlib.sha256(raw).hexdigest()
            self.assertEqual(len(report.load_field(path, checksum, 4)), 4)
            with self.assertRaises(ValueError):
                report.load_field(path, checksum, 5)
            path.write_bytes(raw[:-4] + b"xxxx")
            with self.assertRaises(ValueError):
                report.load_field(path, checksum, 4)

    def test_positive_candidate_cannot_get_negative_verdict_page(self):
        with self.assertRaises(ValueError):
            report.page(dict(all_trials_rejected=False, trials=[]))

    def test_incomplete_or_changed_protocol_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            import json
            path = Path(directory)
            protocol = dict(early_rejection_gates=copy.deepcopy(report.GATES), trials=[])
            (path / "protocol.json").write_text(json.dumps(protocol))
            with self.assertRaises(ValueError):
                report.validate(path)
            protocol["trials"] = [dict(name=name) for name in report.TRIALS]
            protocol["early_rejection_gates"]["maximum_buried_fraction"] = .5
            (path / "protocol.json").write_text(json.dumps(protocol))
            with self.assertRaises(ValueError):
                report.validate(path)


if __name__ == "__main__":
    unittest.main()
