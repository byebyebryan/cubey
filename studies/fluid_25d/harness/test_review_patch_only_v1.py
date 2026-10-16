import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import review_patch_only_v1 as review
import report_patch_only_v1 as report
import xml.etree.ElementTree as ET


class ReviewTests(unittest.TestCase):
    def test_matched_full_patch_recipe(self):
        with patch.object(Path, "read_bytes", return_value=b'{"frames":[{"time_s":123}]}'):
            for case, camera in review.VIEWS:
                args=review.command(case,camera)
                self.assertIn("--headless",args)
                self.assertEqual(args[args.index("--fluid25d-native-terrain-context")+1],"off")
                self.assertEqual(args[args.index("--fluid25d-native-bank-view")+1],"bspline-2x")
                self.assertEqual(args[args.index("--fluid25d-recording-time-seconds")+1],"123")
                self.assertEqual(args[args.index("--fluid25d-recording-camera")+1],camera)
                self.assertNotIn("--fluid25d-external-session",args)
                self.assertNotIn("--terrain-water-detail",args)

    def test_freeze_rejects_drift(self):
        with tempfile.TemporaryDirectory(prefix="cubey-patch-review-test-") as folder:
            out=Path(folder)
            (out/"final-protocol.json").write_text(json.dumps({"runtime":{"app":"a"},
                "inputs":{"field":"b"},"harness":"c"}))
            with patch.object(review,"runtime",return_value={"app":"a"}), \
                 patch.object(review,"inputs",return_value={"field":"b"}), \
                 patch.object(review.render,"digest",return_value="c"):
                review.verify(out,"final")
                with patch.object(review,"inputs",return_value={"field":"changed"}):
                    with self.assertRaises(ValueError): review.verify(out,"final")
                with patch.object(review,"runtime",return_value={"app":"changed"}):
                    with self.assertRaises(ValueError): review.verify(out,"final")

    def test_regressions_require_named_groups_and_no_skips(self):
        with tempfile.TemporaryDirectory(prefix="cubey-patch-report-test-") as folder:
            path=Path(folder)/"tests.xml"
            root=ET.Element("testsuite")
            for name in sorted(report.REQUIRED_TESTS):
                ET.SubElement(root,"testcase",name=name)
            ET.ElementTree(root).write(path)
            self.assertEqual(len(report.checked_regressions(path)),45)
            ET.SubElement(root[0],"skipped")
            ET.ElementTree(root).write(path)
            with self.assertRaises(ValueError): report.checked_regressions(path)
            root[0].remove(root[0][0])
            root.remove(root[0])
            ET.ElementTree(root).write(path)
            with self.assertRaises(ValueError): report.checked_regressions(path)

    def test_initial_scene_is_counted_once_with_replacements(self):
        life={"events":[{"event":"initial" if i==0 else "installed",
            "session_id":str(i),"physical_s":0,"hydraulic_dispatches":0} for i in range(9)],
            "cleanup":[{"active_group_remaining":[],"escalated":False}]}
        self.assertEqual(len(report.checked_installs(life)),9)
        life["events"][1]["session_id"]="0"
        with self.assertRaises(ValueError): report.checked_installs(life)


if __name__=="__main__": unittest.main()
