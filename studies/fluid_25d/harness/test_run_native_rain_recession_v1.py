"""CPU-only focused tests for the frozen native recession wrapper."""
import unittest
import run_native_rain_recession_v1 as study

class RecessionTests(unittest.TestCase):
    def setUp(self):
        self.history=[[0,.001],[120,.001],[180,0],[240,0]]
    def test_rate_integral_phase_and_zero_tail(self):
        self.assertEqual(study.history_value(self.history,0),(0.001,0.0,"On"))
        self.assertEqual(study.history_value(self.history,120),(0.001,.12,"Tapering"))
        rate,depth,phase=study.history_value(self.history,150)
        self.assertAlmostEqual(rate,.0005); self.assertAlmostEqual(depth,.1425); self.assertEqual(phase,"Tapering")
        for time in (180,240):
            rate,depth,phase=study.history_value(self.history,time)
            self.assertEqual(rate,0); self.assertAlmostEqual(depth,.15); self.assertEqual(phase,"Off")
    def test_frozen_main_integral_does_not_continue_after_off(self):
        p=study.load(); off,on=p["cases_in_order"]
        self.assertAlmostEqual(study.history_value(off["rainfall_history"],14400)[1],.241)
        self.assertAlmostEqual(study.history_value(on["rainfall_history"],14400)[1],.48)
        self.assertEqual(study.history_value(off["rainfall_history"],7200)[2],"Tapering")
    def test_bad_histories_and_times_reject(self):
        for history in ([],[[0,.001],[0,0]],[[0,-1],[240,0]],[[0,.001],[240,float("nan")]],[[0,.001],[239,0]]):
            with self.assertRaises(ValueError): study.validate_history(history,240,.001)
        for time in (-1,241,float("nan")):
            with self.assertRaises(ValueError): study.history_value(self.history,time)
    def test_persistent_patch_requires_four_same_locations(self):
        first=study.quiet_tracks([], [{1,2,3,4,5}],60,0)
        same=study.quiet_tracks(first,[{2,3,4,5,6}],120,60)
        self.assertEqual(same[0]["support_s"],60)
        unrelated=study.quiet_tracks(same,[{10,11,12,13}],180,120)
        self.assertEqual(unrelated[0]["support_s"],0)
        dwindled=study.quiet_tracks(same,[{3,4,5,7}],180,120)
        self.assertEqual(dwindled[0]["support_s"],0)

if __name__=="__main__": unittest.main()
