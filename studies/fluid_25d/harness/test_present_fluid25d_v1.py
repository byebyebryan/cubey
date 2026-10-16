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

    def test_nonfinite_and_future_capture_receipts_are_rejected(self):
        shot=present.recipe('test','runoff',0,1200,1)
        rows=[f'fluid_25d_recording_capture: output_frame={i} requested_s={i*1200/59} saved_s={i*1200/59}' for i in range(60)]
        present.check_timeline('\n'.join(rows),shot)
        for requested,saved in (('nan','nan'),('inf','inf'),('20','21'),('20','-1')):
            bad=list(rows)
            bad[1]=f'fluid_25d_recording_capture: output_frame=1 requested_s={requested} saved_s={saved}'
            with self.assertRaises(ValueError):
                present.check_timeline('\n'.join(bad),shot)

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

    def test_lake_audition_preserves_the_packaged_hero(self):
        shots = present.lake_auditions()
        self.assertEqual(len(shots), 3)
        self.assertEqual({(s['start_s'],s['end_s'],s['frames']) for s in shots}, {(1800,7200,480)})
        self.assertTrue(all(not s['dots'] and s['camera']=='collection' for s in shots))
        self.assertEqual(shots[0]['target'], shots[1]['target'])
        self.assertEqual(shots[0]['sweep'], 0)
        self.assertEqual(shots[1]['easing'], 'eased')
        self.assertEqual(present.hero()[0]['target'], [.76,.60,1800])

    def test_lake_command_carries_explicit_oblique_view(self):
        shot = present.lake_auditions()[1]
        argv = present.command(Path('/case'),Path('/recording.json'),shot,Path('/out.mp4'))
        self.assertEqual(argv[argv.index('--fluid25d-native-camera-yaw')+1], '2.1')
        self.assertEqual(argv[argv.index('--fluid25d-native-camera-motion')+1], 'eased')

    def test_formation_starts_dry_with_wider_matched_framing(self):
        shots = present.lake_formation()
        self.assertEqual({s['start_s'] for s in shots}, {0})
        self.assertEqual({s['end_s'] for s in shots}, {7200,21600})
        self.assertEqual({s['frames'] for s in shots}, {360})
        self.assertEqual(shots[0]['target'],shots[1]['target'])
        self.assertGreater(shots[0]['target'][2],present.lake_auditions()[0]['target'][2])
        self.assertTrue(all(s['easing']=='eased' and not s['dots'] for s in shots))

    def test_whole_patch_is_fixed_and_centered_with_explicit_distance(self):
        shot=present.patch_formation()[0]
        self.assertEqual(shot['camera'],'overview')
        self.assertNotIn('target',shot)
        self.assertEqual((shot['start_s'],shot['end_s'],shot['frames']),(0,21600,360))
        self.assertEqual(shot['sweep'],0)
        argv=present.command(Path('/case'),Path('/recording.json'),shot,Path('/out.mp4'))
        self.assertEqual(argv[argv.index('--fluid25d-home-camera-distance-m')+1],'23000')
        self.assertEqual(argv[argv.index('--fluid25d-native-camera-pitch')+1],'-1.2')
        self.assertNotIn('--fluid25d-native-camera-target-x',argv)

    def test_low_orbit_is_closer_lower_and_stronger(self):
        fixed=present.patch_formation()[0]
        shot=present.patch_orbit()[0]
        self.assertEqual((shot['camera'],shot['start_s'],shot['end_s'],shot['frames']),
                         (fixed['camera'],fixed['start_s'],fixed['end_s'],fixed['frames']))
        self.assertNotIn('target',shot)
        self.assertLess(shot['distance_m'],fixed['distance_m'])
        self.assertLess(abs(shot['pitch']),abs(fixed['pitch']))
        self.assertEqual((shot['sweep'],shot['easing']),(.6,'eased'))
        for pose,yaw in zip(present.patch_orbit_framing_shots(),(.3,.6,.9)):
            self.assertAlmostEqual(pose['yaw'],yaw)

    def test_coarser_archive_cadence_is_explicit_not_a_new_default(self):
        shot=present.recipe('survey','overview',0,1200,1)
        raw='\n'.join(f'fluid_25d_recording_capture: output_frame={i} requested_s={i*1200/59} saved_s={int((i*1200/59)//100)*100}' for i in range(60))
        with self.assertRaises(ValueError):
            present.check_timeline(raw,shot)
        result=present.check_timeline(raw,dict(shot,maximum_hold_s=101))
        self.assertGreater(result['unique_hydraulic_states'],10)
        self.assertGreater(result['maximum_hold_physical_s'],75)

    def test_refinement_renders_all_frames_with_independent_endpoint_holds(self):
        shot=present.patch_presentation_refinement()
        self.assertEqual((shot['frames'],shot['opening_frames'],shot['closing_frames']),(480,24,120))
        self.assertEqual((shot['width'],shot['height']),(1920,1080))
        tuning=present.read(shot['tuning'])
        self.assertEqual(tuning,{'schema':'cubey.fluid25d.scenic-material.v2','rain_macro_softening':1})
        argv=present.command(Path('/case'),Path('/recording.json'),shot,Path('/out.mp4'))
        for flag,value in (('--width','1920'),('--height','1080'),
                           ('--fluid25d-recording-opening-frames','24'),
                           ('--fluid25d-recording-closing-frames','120'),
                           ('--fluid25d-native-camera-vertical-shift','0.06')):
            self.assertEqual(argv[argv.index(flag)+1],value)
        interval=float(argv[argv.index('--fluid25d-recording-frame-interval-seconds')+1])
        self.assertGreaterEqual(interval,21600/335)
        self.assertLess(interval-21600/335,1e-5)
        raw='\n'.join(f'fluid_25d_recording_capture: output_frame={i} requested_s={min(max(i-24,0),335)*21600/335} saved_s={int((min(max(i-24,0),335)*21600/335)//300)*300}' for i in range(480))
        checked=present.check_timeline(raw,shot)
        self.assertEqual((checked['first_saved_s'],checked['last_saved_s']),(0,21600))
        self.assertGreater(checked['unique_hydraulic_states'],60)
        with self.assertRaises(ValueError):
            present.check_timeline(raw,dict(shot,opening_frames=0,closing_frames=0))

    def test_resolution_is_native_not_upscaled_and_defaults_remain_720p(self):
        stream=dict(codec_name='h264',pix_fmt='yuv420p',width=1920,height=1080,
                    r_frame_rate='60/1',avg_frame_rate='60/1',nb_read_frames='480',duration='8')
        present.check_video(stream,480,8,1920,1080)
        with self.assertRaises(ValueError):
            present.check_video(stream,480,8)
        self.assertNotIn('width',present.hero()[0])
        with self.assertRaises(ValueError):
            present.check_video(dict(stream,duration='nan'),480,8,1920,1080)
        with self.assertRaises(ValueError):
            present.command(Path('/case'),Path('/recording.json'),
                dict(present.patch_presentation_refinement(),closing_frames=455),Path('/out.mp4'))

    def test_solar_refinement_uses_existing_clock_without_retiming_water(self):
        fixed=present.patch_presentation_refinement()
        solar=present.patch_solar_refinement()
        a=present.command(Path('/case'),Path('/recording.json'),fixed,Path('/fixed.mp4'))
        b=present.command(Path('/case'),Path('/recording.json'),solar,Path('/solar.mp4'))
        for flag in ('--fluid25d-recording-frame-interval-seconds',
                     '--fluid25d-recording-opening-frames','--fluid25d-recording-closing-frames',
                     '--fluid25d-native-camera-sweep','--fluid25d-native-camera-pitch',
                     '--fluid25d-native-camera-vertical-shift'):
            self.assertEqual(a[a.index(flag)+1],b[b.index(flag)+1])
        self.assertEqual(b[b.index('--time-of-day-mode')+1],'solar')
        self.assertIn('atmosphere.time_paused=false',b)
        self.assertEqual(b[b.index('--twilight-strength')+1],'0.18')
        self.assertEqual(b[b.index('--twilight-horizon-warmth')+1],'0.25')
        speed=float(b[b.index('--time-speed-hours-per-second')+1])
        self.assertAlmostEqual(solar['solar_start_hour']+speed*479/60,solar['solar_end_hour'])
        self.assertNotIn('--fluid25d-environment',a)
        with self.assertRaises(ValueError):
            present.command(Path('/case'),Path('/recording.json'),
                dict(solar,sun_elevation=22,sun_azimuth=-75),Path('/mixed.mp4'))
        with self.assertRaises(ValueError):
            present.command(Path('/case'),Path('/recording.json'),
                dict(solar,solar_end_hour=14),Path('/reversed.mp4'))


if __name__ == '__main__':
    unittest.main()
