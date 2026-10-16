import json
from pathlib import Path
import tempfile
import unittest
import review_terrain_world_v1 as review


class ReviewTests(unittest.TestCase):
    def test_site_command_is_headless_and_retains_world_focus(self):
        with tempfile.TemporaryDirectory(prefix="cubey-world-review-test-") as temporary:
            path=Path(temporary)/"site.json"
            path.write_text(json.dumps({"center_xz_m":[11520,19200]}))
            cmd=review.site_command(path,shift=(1000,-1000))
            self.assertIn("--headless",cmd)
            self.assertIn("--terrain-site",cmd)
            self.assertEqual(cmd[cmd.index("--terrain-water-focus-x")+1],"12520")
            self.assertEqual(cmd[cmd.index("--terrain-water-focus-z")+1],"18200")

    def test_extension_does_not_replace_capture_anchor(self):
        out=review.study.ROOT/"outputs/terrain/world-sites-unit"
        before=review.water_command(out,False);after=review.water_command(out,True)
        self.assertEqual(after[:len(before)],before)
        self.assertEqual(after[-2],"--terrain-water-context-source")
        self.assertEqual(after[after.index("--terrain-heightfield")+1],str(review.study.SOURCE))
        self.assertEqual(after[after.index("--terrain-water-capture")+1],str(review.study.CAPTURE))


if __name__=="__main__":unittest.main()
