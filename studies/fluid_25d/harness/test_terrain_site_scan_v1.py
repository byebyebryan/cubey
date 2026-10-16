from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import terrain_site_scan_v1 as scan


class TerrainSiteScanV1Tests(unittest.TestCase):
    @staticmethod
    def _candidate(digest: str, x: int, z: int, score: float, junctions: int = 0):
        return scan.Candidate(
            elevation_sha256=digest,
            manifest_path=f"cache/{digest[:8]}/heightfield.json",
            alias_manifest_paths=(),
            variant=digest[:8],
            seed=0,
            x=x,
            z=z,
            score=score,
            deep_valley_fraction=0.1,
            largest_component_fraction=0.9,
            deep_component_count=1,
            branch_junction_count=junctions,
            p95_valley_depth_m=200.0,
            maximum_valley_depth_m=300.0,
            valley_edge_fraction=0.0,
        )

    @staticmethod
    def _write_manifest(root: Path, variant: str, payload: bytes) -> Path:
        root.mkdir(parents=True)
        elevation_path = root / "elevation.f32"
        elevation_path.write_bytes(payload)
        digest = hashlib.sha256(payload).hexdigest()
        document = {
            "schema": "cubey.terrain.heightfield.v1",
            "seed": 0,
            "source": {
                "generator": "terrain-diffusion",
                "id": "terrain-diffusion-30m",
                "native_resolution_m": 30.0,
                "code_revision": scan.PINNED_CODE_REVISION,
                "model_id": scan.PINNED_MODEL_ID,
                "model_revision": scan.PINNED_MODEL_REVISION,
            },
            "grid": {
                "width": 4,
                "height": 4,
                "sample_spacing_m": 30.0,
                "axis_mapping": {"world_x": "model_j", "world_z": "model_i"},
            },
            "files": {
                "elevation": {
                    "path": "elevation.f32",
                    "dtype": "float32-le",
                    "layout": "row-major-zx",
                    "shape": [4, 4],
                    "unit": "m",
                    "byte_count": 64,
                    "sha256": digest,
                }
            },
            "provenance": {"landscape_variant": variant},
        }
        manifest = root / "heightfield.json"
        manifest.write_text(json.dumps(document))
        return manifest

    def test_manifest_hash_relation_and_payload_deduplication(self) -> None:
        payload = np.arange(16, dtype="<f4").tobytes()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = self._write_manifest(root / "a", "first", payload)
            second = self._write_manifest(root / "b", "alias", payload)

            records = [
                scan.load_manifest_record(first, expected_shape=(4, 4)),
                scan.load_manifest_record(second, expected_shape=(4, 4)),
            ]
            assets = scan.deduplicate_assets(records)

            self.assertEqual(len(assets), 1)
            self.assertEqual(len(assets[0].aliases), 2)
            self.assertEqual(assets[0].elevation_sha256, hashlib.sha256(payload).hexdigest())
            self.assertEqual(records[0].manifest_sha256, scan.sha256_file(first))

    def test_manifest_rejects_payload_hash_mismatch_and_non_30m_spacing(self) -> None:
        payload = np.zeros(16, dtype="<f4").tobytes()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self._write_manifest(root / "asset", "fixture", payload)
            elevation = root / "asset" / "elevation.f32"
            elevation.write_bytes(np.ones(16, dtype="<f4").tobytes())
            with self.assertRaisesRegex(ValueError, "SHA-256 disagrees"):
                scan.load_manifest_record(manifest, expected_shape=(4, 4))

            elevation.write_bytes(payload)
            document = json.loads(manifest.read_text())
            document["grid"]["sample_spacing_m"] = 60.0
            manifest.write_text(json.dumps(document))
            with self.assertRaisesRegex(ValueError, "native 30 m"):
                scan.load_manifest_record(manifest, expected_shape=(4, 4))

            document["grid"]["sample_spacing_m"] = 30.0
            document["source"]["model_revision"] = "unrecognized-model-revision"
            manifest.write_text(json.dumps(document))
            with self.assertRaisesRegex(ValueError, "pinned source"):
                scan.load_manifest_record(manifest, expected_shape=(4, 4))

    def test_candidate_origins_stay_in_bounds_and_use_overlapping_stride(self) -> None:
        origins = scan.candidate_origins(1024, 512)
        self.assertEqual(len(origins), 25)
        self.assertEqual(origins[-1], (512, 256))
        for x, z in origins:
            self.assertGreaterEqual(x, 0)
            self.assertGreaterEqual(z, 0)
            self.assertLessEqual(x + scan.CROP_WIDTH, 1024)
            self.assertLessEqual(z + scan.CROP_HEIGHT, 512)

    def test_previous_v2_crop_exclusion_uses_payload_identity_and_overlap(self) -> None:
        lowland_hash = next(
            item["elevation_sha256"]
            for item in scan.PREVIOUS_V2_CROPS
            if item["id"] == "rolling-lowland-branching"
        )
        self.assertTrue(scan.overlaps_previous_v2(lowland_hash, 1024, 1536))
        self.assertFalse(scan.overlaps_previous_v2("a" * 64, 1024, 1536))
        self.assertFalse(scan.overlaps_previous_v2(lowland_hash, 0, 0))

    @staticmethod
    def _paint_segment(mask: np.ndarray, start: tuple[float, float], end: tuple[float, float], radius: float) -> None:
        z_grid, x_grid = np.ogrid[: mask.shape[0], : mask.shape[1]]
        x0, z0 = start
        x1, z1 = end
        dx = x1 - x0
        dz = z1 - z0
        denominator = dx * dx + dz * dz
        t = np.clip(((x_grid - x0) * dx + (z_grid - z0) * dz) / denominator, 0.0, 1.0)
        distance_squared = (x_grid - (x0 + t * dx)) ** 2 + (z_grid - (z0 + t * dz)) ** 2
        mask |= distance_squared <= radius * radius

    @classmethod
    def _measure_mask(cls, mask: np.ndarray) -> dict[str, float | int]:
        depth = np.where(mask, 180.0, 0.0).astype(np.float32)
        junction_labels = scan.skeleton_junction_labels(mask)
        return scan.measure_crop(depth, mask, junction_labels, 0, 0)

    def test_connected_branch_proxy_separates_y_from_trough_and_plain(self) -> None:
        plain = np.zeros((64, 128), dtype=bool)
        trough = np.zeros_like(plain)
        self._paint_segment(trough, (6, 32), (122, 32), 2.5)
        branching = np.zeros_like(plain)
        self._paint_segment(branching, (8, 8), (64, 32), 2.5)
        self._paint_segment(branching, (8, 56), (64, 32), 2.5)
        self._paint_segment(branching, (64, 32), (120, 32), 2.5)

        plain_metrics = self._measure_mask(plain)
        trough_metrics = self._measure_mask(trough)
        branching_metrics = self._measure_mask(branching)

        self.assertEqual(plain_metrics["deep_component_count"], 0)
        self.assertEqual(trough_metrics["branch_junction_count"], 0)
        self.assertGreaterEqual(branching_metrics["branch_junction_count"], 1)
        self.assertGreater(branching_metrics["connected_branch_score"], trough_metrics["connected_branch_score"])
        self.assertEqual(branching_metrics["deep_component_count"], 1)

    def test_disconnected_branch_does_not_count_for_largest_valley_component(self) -> None:
        field = np.zeros((64, 128), dtype=bool)
        self._paint_segment(field, (4, 54), (124, 54), 2.5)
        self._paint_segment(field, (8, 5), (40, 20), 2.5)
        self._paint_segment(field, (8, 35), (40, 20), 2.5)
        self._paint_segment(field, (40, 20), (75, 20), 2.5)

        metrics = self._measure_mask(field)

        self.assertEqual(metrics["deep_component_count"], 2)
        self.assertEqual(metrics["branch_junction_count"], 0)

    def test_shortlist_is_deterministic_diverse_and_non_overlapping(self) -> None:
        candidates = [
            self._candidate("a" * 64, 0, 0, 9.0, 2),
            self._candidate("a" * 64, 512, 0, 8.0, 1),
            self._candidate("b" * 64, 0, 0, 7.0, 1),
            self._candidate("c" * 64, 0, 0, 6.0, 0),
        ]
        first = scan.select_shortlist(candidates, limit=4)
        second = scan.select_shortlist(list(reversed(candidates)), limit=4)
        self.assertEqual(
            [(item.elevation_sha256, item.x, item.z) for item in first],
            [(item.elevation_sha256, item.x, item.z) for item in second],
        )
        self.assertEqual(len(first), 4)
        self.assertEqual(first[0].elevation_sha256, "a" * 64)
        for index, candidate in enumerate(first):
            for other in first[index + 1 :]:
                self.assertFalse(scan._overlaps_candidate(candidate, other))


if __name__ == "__main__":
    unittest.main()
