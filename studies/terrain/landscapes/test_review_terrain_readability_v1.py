import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import review_terrain_readability_v1 as review


class ReviewTests(unittest.TestCase):
    def test_matched_camera_and_explicit_opt_in(self):
        with tempfile.TemporaryDirectory(prefix="cubey-readability-test-") as temporary:
            root=Path(temporary); folder=root/"sites/valley-interior";folder.mkdir(parents=True)
            (folder/"site.json").write_text(json.dumps({"center_xz_m":[-7665,61455]}))
            with patch.object(review.atlas,"WORLD",root):
                before=review.command("valley-interior")
                after=review.command("valley-interior",mode="refined")
            self.assertIn("--headless",before)
            self.assertEqual(before[before.index("--terrain-water-span")+1],"32768")
            i=before.index("--terrain-readability")+1
            self.assertEqual(before[i],"current");self.assertEqual(after[i],"refined")
            after[i]="current";self.assertEqual(before,after)

    def test_water_anchor_and_mesh_focus_do_not_move(self):
        before=review.command("water");after=review.command("water",mode="refined",cap=512)
        for option in ("--terrain-heightfield","--terrain-water-capture","--terrain-water-detail-focus-x",
                       "--terrain-water-detail-focus-z","--terrain-water-focus-x","--terrain-water-focus-z"):
            self.assertEqual(before[before.index(option)+1],after[after.index(option)+1])
        self.assertEqual(after[after.index("--terrain-context-max-spacing")+1],"512")

    def test_owned_output_cannot_overwrite_previous_report(self):
        with self.assertRaises(ValueError):review.atlas.owned(review.OLD)


if __name__=="__main__":unittest.main()
