#!/usr/bin/env python3
"""Reject incomplete or failed native compatibility receipts before publication."""
import copy
import unittest

from build_library_report import validate_compatibility


class CompatibilityEvidenceTests(unittest.TestCase):
    def setUp(self):
        names = ("sphere-clay-reference", "sphere-glass-reference", "guardian-clay-reference",
                 "guardian-glass-reference", "furnace", "furnace-ior", "gltf")
        self.evidence = {name: dict(returncode=0, byte_identical=True, validation_errors=False,
                                   sha256="a" * 64, baseline_sha256="a" * 64) for name in names}

    def test_complete_pass(self):
        validate_compatibility(self.evidence)

    def test_missing_controls(self):
        for evidence in ({}, {k: v for k, v in self.evidence.items() if k != "gltf"}):
            with self.subTest(controls=list(evidence)), self.assertRaises(ValueError):
                validate_compatibility(evidence)

    def test_failed_or_inconsistent_control(self):
        for field, value in (("returncode", 1), ("byte_identical", False),
                             ("validation_errors", True), ("sha256", "b" * 64)):
            evidence = copy.deepcopy(self.evidence)
            evidence["gltf"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_compatibility(evidence)

    def test_missing_validation_receipt(self):
        del self.evidence["gltf"]["validation_errors"]
        with self.assertRaises(ValueError):
            validate_compatibility(self.evidence)


if __name__ == "__main__":
    unittest.main()
