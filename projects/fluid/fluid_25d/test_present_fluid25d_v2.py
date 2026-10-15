"""Selected capture guards without optional archives, a GPU, or ffmpeg."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import present_fluid25d_v2 as v2


def receipts():
    shot = dict(v2.p.patch_solar_refinement(), end_s=21635.626953125)
    lines = []
    for index in range(480):
        requested = shot['end_s']*min(max(index-24,0),335)/335
        state = shot['end_s']*min(int(requested/shot['end_s']*71+1e-9),71)/71
        fraction = index/479
        yaw = .3+.6*fraction*fraction*(3-2*fraction)
        lines += [f'fluid_25d_recording_capture: output_frame={index} requested_s={requested} saved_s={state} camera=overview hydraulic_dispatches=0',
                  f'fluid_25d_rain_capture: output_frame={index} applied_mm_h=512 known=1 clock_s={index/60}',
                  f'fluid_25d_terrain_camera: output_frame={index} yaw={yaw} pitch={shot["pitch"]} distance=13500']
    for index in range(32):
        wall = index*.25
        hour = 15.5+2.5/(479/60)*wall
        lines.append(f'terrain_surface_environment: wall_s={wall} time_hours={hour} sun_elevation=12 sky_generation={index+1} clouds=0 primary=sun')
    return shot, '\n'.join(lines)


class CaptureGuards(unittest.TestCase):
    def test_clean_source_covers_index_and_untracked_files(self):
        for dirty in (b' M tracked\n', b'M  staged\n', b'?? new.py\n'):
            with self.subTest(dirty=dirty), patch.object(v2, 'git', return_value=dirty):
                with self.assertRaisesRegex(ValueError, 'clean'):
                    v2.clean_source()

    def test_clean_source_receipt(self):
        with patch.object(v2, 'git', side_effect=[b'', b'abc\n', b'']):
            self.assertEqual(v2.clean_source(), dict(source_revision='abc',
                source_status_sha256=v2.EMPTY, source_dirty_diff_sha256=v2.EMPTY))

    def test_selected_archive_cannot_fall_back_to_another_site(self):
        with patch.object(v2.p, 'digest', return_value='other'):
            with self.assertRaisesRegex(ValueError, 'owner-selected'):
                v2.verify_archive(Path('/case'), Path('/recording'), Path('/audit'))

    def test_direct_child_and_hash_guards(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            path = root/'value'
            path.write_bytes(b'test fixture')
            (root/'link').symlink_to(path)
            for bad in ('../value', '/value', 'link', 'absent'):
                with self.subTest(bad=bad), self.assertRaises(ValueError):
                    v2.direct_file(root, bad)
            with self.assertRaisesRegex(ValueError, 'hash'):
                v2.checked_file(root, dict(path='value', sha256='wrong'))
            self.assertEqual(v2.checked_file(root, dict(path='value', sha256=v2.p.digest(path))), path)

    def test_locked_recipe_is_native_1080p_and_offscreen(self):
        shot = v2.p.patch_solar_refinement()
        command = v2.p.command(Path('/case'), Path('/recording'), shot, Path('/out'))
        self.assertEqual((shot['width'], shot['height'], shot['frames']), (1920,1080,480))
        self.assertEqual((shot['opening_frames'], shot['closing_frames']), (24,120))
        self.assertIn('--headless', command)
        self.assertNotIn('--fluid25d-external-session', command)
        self.assertNotIn('--fluid25d-motion-markers', command)
        self.assertEqual(command[command.index('--fluid25d-recording-frame-interval-seconds')+1],
                         '64.47761535644531')  # render snaps 21600 to the exact final native save first.

    def test_missing_receipts_fail_closed(self):
        with self.assertRaises(ValueError):
            v2.editorial_receipts('', v2.p.patch_solar_refinement())

    def test_full_editorial_clocks_and_animated_holds(self):
        shot, raw = receipts()
        result = v2.editorial_receipts(raw, shot)
        self.assertEqual(result['unique_hydraulic_states'],72)
        self.assertTrue(result['all_frames_rendered'])
        self.assertTrue(result['matched_camera'])
        self.assertEqual(result['cloned_frames'],0)

    def test_receipt_corruption_is_rejected(self):
        shot, raw = receipts()
        for before, after in (('hydraulic_dispatches=0','hydraulic_dispatches=1'),
                              ('applied_mm_h=512','applied_mm_h=1024'),
                              ('distance=13500','distance=15000'),
                              ('time_hours=15.5 ','time_hours=18.5 '),
                              ('clock_s=0.0','clock_s=1.0')):
            with self.subTest(before=before), self.assertRaises(ValueError):
                v2.editorial_receipts(raw.replace(before,after,1),shot)

    def test_dirty_or_wrong_archive_capture_cannot_be_packaged(self):
        protocol = dict(schema='cubey.fluid25d.presentation-capture.v2',
                        source_status_sha256=v2.EMPTY, source_dirty_diff_sha256=v2.EMPTY,
                        input=dict(recording_sha256=v2.RECORDING))
        row = dict(sha256=v2.APPROVED,cleanup=dict(active_group_remaining=[],exit_code=0))
        for key in ('source_status_sha256','source_dirty_diff_sha256'):
            with self.subTest(key=key), patch.object(v2.p, 'read', side_effect=[dict(protocol,**{key:'dirty'}),row]):
                with self.assertRaisesRegex(ValueError,'qualified clean-source'):
                    v2.qualify_capture(Path('/unused'))


if __name__ == '__main__':
    unittest.main()
