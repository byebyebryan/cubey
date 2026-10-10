import unittest
import hashlib
import json
from pathlib import Path
import tempfile
import numpy as np

from terrain_grid_sampling import grid_geometry, sample_grid, load_source


class SamplingTests(unittest.TestCase):
    def test_planar_world_sampling_and_orientation(self):
        raw = np.arange(5, dtype=np.float32)[None, :] + 10 * np.arange(4, dtype=np.float32)[:, None]
        source = {"grid": {"sample_spacing_m": 8, "sample_origin_x_m": -16,
                            "sample_origin_z_m": -8}, "height": {"offset_m": 2, "scale": .5}}
        g = grid_geometry(5, 3, 4, (0, 0), (-16, -8, 16, 16))
        result = sample_grid(source, raw, g)
        x = np.arange(5) * 4 - 8
        z = np.arange(3) * 4 - 4
        expected = ((x[None, :] + 16) / 8 + 10 * (z[:, None] + 8) / 8 + 2) * .5
        np.testing.assert_allclose(result, expected)

    def test_nested_footprints_shift_larger_at_edge(self):
        small = grid_geometry(1024, 1024, 16, (11520, 19200), (-30600, -30600, 30600, 30600))
        big = grid_geometry(2048, 2048, 16, (11520, 19200), (-30600, -30600, 30600, 30600))
        for axis in range(2):
            self.assertLessEqual(big["sample_origin_xz_m"][axis], small["sample_origin_xz_m"][axis])
            self.assertGreaterEqual(big["sample_origin_xz_m"][axis] + 2047 * 16,
                                    small["sample_origin_xz_m"][axis] + 1023 * 16)

    def test_rejects_out_of_coverage_and_invalid_geometry(self):
        for size, spacing in ((2048, 32), (4096, 8), (512, float("nan")), (512, 0)):
            with self.assertRaises(ValueError):
                grid_geometry(size, size, spacing, (0, 0), (-30000, -30000, 30000, 30000))
        with self.assertRaises(ValueError):
            grid_geometry(512.0, 512, 32, (0, 0), (-30000, -30000, 30000, 30000))

    def test_source_custody_and_immutable_reads(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            raw = np.arange(12, dtype="<f4").tobytes()
            (root / "elevation.f32").write_bytes(raw)
            manifest = {"schema": "cubey.terrain.heightfield.v1", "grid": {"width": 4, "height": 3},
                "files": {"elevation": {"path": "elevation.f32", "dtype": "float32-le",
                    "layout": "row-major-zx", "shape": [3, 4], "sha256": hashlib.sha256(raw).hexdigest()}}}
            path = root / "heightfield.json"
            path.write_text(json.dumps(manifest))
            before = path.read_bytes()
            _, sampled = load_source(path)
            self.assertEqual(sampled[2, 3], 11)
            self.assertEqual(before, path.read_bytes())
            self.assertEqual(raw, (root / "elevation.f32").read_bytes())
            manifest["files"]["elevation"]["sha256"] = "0" * 64
            path.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                load_source(path)
            manifest["files"]["elevation"]["path"] = "../escape.f32"
            path.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                load_source(path)


if __name__ == "__main__":
    unittest.main()
