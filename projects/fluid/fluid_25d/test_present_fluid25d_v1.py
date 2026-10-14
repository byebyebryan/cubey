import unittest
from pathlib import Path

import present_fluid25d_v1 as present


class Presentation(unittest.TestCase):
    def test_auditions_change_only_camera_and_dots(self):
        shots = present.auditions()
        self.assertEqual(len(shots), 4)
        self.assertEqual({(s['start_s'],s['end_s'],s['camera']) for s in shots}, {(2400,7200,'runoff')})
        self.assertEqual({(s['dots'],s['sweep']>0) for s in shots}, {(a,b) for a in (False,True) for b in (False,True)})

    def test_capture_is_headless_replay_not_a_worker(self):
        shot = present.auditions()[0]
        command = present.command(Path('/case'), Path('/recording.json'), shot, Path('/video.mp4'))
        self.assertIn('--headless',command)
        self.assertNotIn('--fluid25d-external-session',command)
        self.assertNotIn('--fluid25d-motion-markers',command)
        self.assertEqual(command[command.index('--frames')+1],'480')
        self.assertEqual(command[command.index('--fps')+1],'60')

    def test_held_and_incomplete_capture_do_not_qualify(self):
        shot = present.recipe('test','runoff',0,1200,1)
        with self.assertRaises(ValueError):
            present.check_timeline('',shot)
        raw = '\n'.join(f'fluid_25d_recording_capture: output_frame={i} requested_s={i*1200/59} saved_s=0' for i in range(60))
        with self.assertRaises(ValueError):
            present.check_timeline(raw,shot)

    def test_recipe_has_bounded_physical_timeline(self):
        for arguments in ((100,0,8), (0,72000,8), (0,7200,60)):
            with self.assertRaises(ValueError):
                present.recipe('bad','runoff',*arguments)

    def test_selected_story_never_reverses_physical_time(self):
        shots=present.story()
        self.assertEqual(sum(s['frames'] for s in shots),2160)
        self.assertTrue(all(a['end_s']<=b['start_s'] for a,b in zip(shots,shots[1:])))
        self.assertTrue(all(not shot['dots'] for shot in shots))
        self.assertEqual(present.hero()[0]['easing'],'eased')
        self.assertLess(present.hero()[0]['end_s'],7200)


if __name__ == '__main__':
    unittest.main()
