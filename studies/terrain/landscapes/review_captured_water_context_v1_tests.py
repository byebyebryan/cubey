#!/usr/bin/env python3
"""Portable report checks, not renderer acceptance."""
from pathlib import Path
import tempfile
import unittest
import review_captured_water_context_v1 as review

class ReportTests(unittest.TestCase):
    def test_context_is_explicit(self):
        self.assertNotIn("--terrain-water-context",review.command("overview",False))
        self.assertIn("--terrain-water-context",review.command("overview",True))
        self.assertIn("--headless",review.command("landscape",True))

    def test_provisional_report_does_not_claim_acceptance(self):
        with tempfile.TemporaryDirectory(prefix="cubey-context-report-") as d:
            out=Path(d);review.report(out,{})
            text=(out/"index.html").read_text()
            self.assertIn("Prototype review; not acceptance",text)
            self.assertIn("not a seamless",text)
            self.assertIn("boundary-off.png",text)
            self.assertNotIn("Validation receipt",text)

    def test_final_report_contains_diagnostics_and_receipt(self):
        with tempfile.TemporaryDirectory(prefix="cubey-context-report-") as d:
            out=Path(d);review.report(out,{"images_folder":"final","foundation_tests":8,
                "fluid_tests":2,"gpu_edge_height_error_m":.000488281,
                "verdict":"Useful opt-in context; manual review deferred."})
            text=(out/"index.html").read_text()
            self.assertIn("Validation receipt",text)
            self.assertIn("0.488 mm",text)
            self.assertIn("lake-frozen.mp4",text)
            self.assertIn("overview-ownership.png",text)

if __name__=="__main__":unittest.main()
