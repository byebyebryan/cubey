import unittest
from pathlib import Path
import review_wider_water_v1 as review


class ReviewTests(unittest.TestCase):
    def test_offscreen_and_independent_fixed_focus(self):
        a=review.command(Path("capture.json"),pose=(12520,19200,1342,8192,1.2,-.4))
        self.assertIn("--headless",a)
        self.assertEqual(a[a.index("--terrain-water-focus-x")+1],"12520")
        self.assertEqual(a[a.index("--terrain-water-detail-focus-x")+1],"11520")
        self.assertNotIn("--terrain-water-context",a)

    def test_matched_uniform_control(self):
        a=review.command(Path("capture.json"),detail="uniform-control")
        self.assertEqual(a[a.index("--terrain-water-detail")+1],"uniform-control")
        self.assertIn("--terrain-water-detail-focus-z",a)

    def test_ground_control_and_world_views(self):
        a=review.command(Path("capture.json"),"stream",water=False)
        self.assertIn("--no-terrain-water-visible",a)
        self.assertEqual(a[a.index("--terrain-water-span")+1],"900")
        self.assertEqual(a[a.index("--terrain-water-focus-x")+1],"15464.687988")


if __name__=="__main__":unittest.main()
