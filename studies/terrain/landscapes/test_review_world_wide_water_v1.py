import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import review_world_wide_water_v1 as review


class Contracts(unittest.TestCase):
    def test_complete_performance_rejects_partial_duplicate_and_failed_rows(self):
        rows = [{"view":view,"repeat":repeat,"pass":True}
                for view in ("local","overview") for repeat in range(2)]
        self.assertTrue(review.complete_performance({"complete":True,"rows":rows}))
        for bad in (rows[:-1], rows[:-1]+[rows[0]], [dict(r,**{"pass":False}) for r in rows]):
            self.assertFalse(review.complete_performance({"complete":True,"rows":bad}))

    def test_commands_keep_static_and_core_sources_distinct(self):
        p = {"grids":{"wide":{"center_xz_m":[100,200]}}, "source":"pinned-source",
             "terrain_site":{"manifest_path":"pinned-site"}}
        with patch.object(review.study.bake,"check_frozen", return_value=p), \
                patch.object(review,"pose", return_value=(1500,11457.6)):
            core = review.command(Path("study"),"mountain","core-16m")
            wide = review.command(Path("study"),"mountain","wide-64m")
        self.assertIn("--terrain-site-water",wide)
        self.assertNotIn("--terrain-water-capture",wide)
        self.assertIn("--terrain-water-capture",core)
        self.assertNotIn("--terrain-site-water",core)
        self.assertTrue(all("--fluid25d-external-session" not in cmd for cmd in (core,wide)))

    def test_performance_rejects_unfinished_owned_worker(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            (out / "one/runs/wide").mkdir(parents=True)
            with patch.object(review,"verify"), patch.object(review,"input_identity", return_value={}), \
                    patch.object(review.render,"run") as run:
                with self.assertRaisesRegex(ValueError,"worker still active"):
                    review.performance(out,"one","wide-64m")
                run.assert_not_called()

    def test_performance_has_two_views_two_repeats_and_ground_controls(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            with patch.object(review,"verify"), patch.object(review,"input_identity", return_value={"field":"pin"}), \
                    patch.object(review,"command", return_value=["owned-app"]), \
                    patch.object(review.render,"run", return_value={"exit_code":0}) as run, \
                    patch.object(Path,"read_text", return_value="NVIDIA GeForce RTX 5070 Ti headless_render_only_ready:"), \
                    patch.object(review.render,"timings", return_value={"whole_gpu_p95_ms":5,"cpu_service_p95_ms":.5}):
                review.performance(out,"one","wide-64m")
            self.assertEqual(run.call_count,8)
            result = json.loads((out/"one/performance/wide-64m/results.json").read_bytes())
            self.assertTrue(result["complete"])
            self.assertEqual(len(result["rows"]),4)
            self.assertTrue(all(r["pass"] and set(r["runs"])=={"ground","water"} for r in result["rows"]))


if __name__ == "__main__":
    unittest.main()
