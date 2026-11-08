"""Regression controls that need neither a GPU nor the downloaded dragon."""
import unittest

from dragon_glass_acceptance import (
    REQUESTED_MEMORY_FIELDS, STATUS_NAMES, require_requested_memory, require_transport_status,
)


class GlassAcceptanceTests(unittest.TestCase):
    def counts(self, *bits):
        return {str(bit): int(bit in bits) for bit in STATUS_NAMES}

    def test_accepted_views_reject_event_and_shadow_exhaustion(self):
        for bit in [2, 32]:
            with self.subTest(bit=bit), self.assertRaisesRegex(AssertionError, 'forbidden transport'):
                require_transport_status('preset-default', self.counts(bit))

    def test_shallow_diagnostic_can_record_truncation(self):
        require_transport_status('depth-24', self.counts(2, 32), allow_truncation=True)

    def test_diagnostics_still_reject_other_transport_failures(self):
        for bit in [1, 4, 16, 64]:
            with self.subTest(bit=bit), self.assertRaises(AssertionError):
                require_transport_status('depth-24', self.counts(bit), allow_truncation=True)

    def test_clean_and_zero_weight_terminations_are_accepted(self):
        require_transport_status('preset-default', self.counts())
        require_transport_status('bronze', self.counts(8))

    def memory(self):
        metrics = dict.fromkeys(REQUESTED_MEMORY_FIELDS, 0)
        metrics.update(geometry_bytes=100, edge_adjacency_bytes=20, as_bytes=50,
                       target_bytes=10, resident_project_requested_bytes=160,
                       peak_simultaneous_project_requested_bytes=200)
        return metrics

    def test_adjacency_subset_is_not_counted_twice(self):
        self.assertEqual(require_requested_memory(self.memory()), 160)

    def test_missing_adjacency_in_resident_total_is_rejected(self):
        metrics = self.memory()
        metrics['resident_project_requested_bytes'] -= metrics['edge_adjacency_bytes']
        with self.assertRaisesRegex(AssertionError, 'component sum'):
            require_requested_memory(metrics)

    def test_peak_smaller_than_resident_is_rejected(self):
        metrics = self.memory()
        metrics['peak_simultaneous_project_requested_bytes'] = 159
        with self.assertRaisesRegex(AssertionError, 'peak requested'):
            require_requested_memory(metrics)


if __name__ == '__main__':
    unittest.main()
