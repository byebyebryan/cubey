import copy
import unittest
from terrain_world_coverage import Box, native_point, site_plan, world_recipe


def anchor():
    return {"schema": "cubey.terrain.heightfield.v1", "seed": 0,
            "source": {"generator": "terrain-diffusion", "id": "test", "model_snapshot": "/one",
                       "settings": {"device": "cuda"}},
            "height": {"offset_m": 39.367, "scale": .848, "relief_scale_m": 3500},
            "grid": {"width": 2048, "height": 2048, "sample_spacing_m": 30,
                     "sample_origin_x_m": -30705, "sample_origin_z_m": -30705,
                     "axis_mapping": {"world_x": "model_j", "world_z": "model_i"}},
            "provenance": {"model_native_origin": {"i": 30720, "j": -28672}}}


class CoverageTests(unittest.TestCase):
    def test_mapping_does_not_recenter_each_site(self):
        world = world_recipe(anchor())
        self.assertEqual(native_point(world, (-30705, -30705)), (30720, -28672))
        self.assertEqual(native_point(world, (-30675, -30645)), (30722, -28671))

    def test_largest_box_and_margins_define_admission(self):
        world = world_recipe(anchor())
        self.assertTrue(site_plan(world, "mountain", (11520, 19200))["admitted"])
        # Detail fits, but outer backdrop does not. Never silently clamp a site.
        plan = site_plan(world, "edge", (100000, 0))
        self.assertFalse(plan["admitted"])
        self.assertEqual(plan["center_xz_m"], [100000, 0])
        edge = 131072 - 65536 - 1480
        self.assertTrue(site_plan(world, "edge", (edge, edge))["admitted"])
        self.assertFalse(site_plan(world, "edge", (edge + 1, edge))["admitted"])

    def test_nested_boxes_and_water_independence(self):
        world = world_recipe(anchor())
        p = site_plan(world, "one", (0, 0))
        boxes = [Box(**{k: tuple(v) for k, v in level["bounds"].items()}) for level in p["levels"]]
        self.assertTrue(all(b.contains(a) for a, b in zip(boxes, boxes[1:])))
        self.assertIsNone(p["water_capture"])

    def test_identity_portable_but_settings_sensitive(self):
        a = anchor(); b = copy.deepcopy(a)
        b["source"]["model_snapshot"] = "/two"; b["source"]["settings"]["device"] = "cpu"
        self.assertEqual(world_recipe(a)["world_id"], world_recipe(b)["world_id"])
        b["seed"] = 1
        self.assertNotEqual(world_recipe(a)["world_id"], world_recipe(b)["world_id"])

    def test_invalid_requests(self):
        world = world_recipe(anchor())
        for center in ((float("nan"), 0), (0, float("inf"))):
            with self.assertRaises(ValueError): site_plan(world, "one", center)
        for spans in ((32, 16), (16, 16), (), (16, -1)):
            with self.assertRaises(ValueError): site_plan(world, "one", (0, 0), spans=spans)
        with self.assertRaises(ValueError): site_plan(world, "../bad", (0, 0))
        with self.assertRaises(ValueError): site_plan(world, "one", (0, 0), camera_margin=-1)


if __name__ == "__main__":
    unittest.main()
