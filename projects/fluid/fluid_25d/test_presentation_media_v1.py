"""Committed media/provenance audits without the optional solver or terrain cache."""
import hashlib
import json
from pathlib import Path
import struct
import unittest

ROOT=Path(__file__).resolve().parents[3]
MEDIA=ROOT/'docs/media/showcase'


class Media(unittest.TestCase):
    def test_media_hashes_and_dimensions_match_the_manifest(self):
        manifest=json.loads((MEDIA/'manifest.json').read_bytes())
        entry=next(e for e in manifest['entries'] if e['id']=='fluid-25d')
        recipe=json.loads((MEDIA/'fluid-25d-recipe.json').read_bytes())
        self.assertEqual(recipe['schema'],'cubey.fluid25d.presentation.v2')
        self.assertEqual(entry['publication']['github_attachment_url'],None)
        for descriptor in recipe['media'].values():
            path=ROOT/descriptor['path']
            self.assertEqual(path.stat().st_size,descriptor['bytes'])
            with path.open('rb') as stream:
                self.assertEqual(hashlib.file_digest(stream,'sha256').hexdigest(),descriptor['sha256'])
            if path.suffix=='.png':
                header=path.read_bytes()[:24]
                self.assertEqual(header[:8],b'\x89PNG\r\n\x1a\n')
                self.assertEqual(struct.unpack('>II',header[16:24]),(1280,720))
            elif path.suffix=='.mp4':
                payload=path.read_bytes()
                offset,atoms=0,[]
                while offset+8<=len(payload):
                    size,kind=struct.unpack_from('>I4s',payload,offset)
                    self.assertTrue(size==0 or size>=8)
                    atoms.append(kind)
                    offset+=size or len(payload)-offset
                self.assertLess(atoms.index(b'moov'),atoms.index(b'mdat'))
        self.assertEqual(entry['publication']['sha256'],recipe['media']['fluid-25d.mp4']['sha256'])
        self.assertEqual(entry['master']['sha256'],recipe['media']['fluid-25d-master-1080p.mp4']['sha256'])
        self.assertEqual(entry['poster']['timestamp_seconds'],6)
        self.assertEqual(recipe['gallery']['derived_from'],'fluid-25d-master-1080p.mp4')
        self.assertIn('scale=1280:720:flags=lanczos,setpts=N/(60*TB)',recipe['gallery']['command'])
        self.assertEqual(recipe['gallery']['probe']['width'],1280)
        self.assertEqual(recipe['gallery']['probe']['nb_read_frames'],'480')

    def test_native_frames_and_timelines_are_not_live_fps_claims(self):
        recipe=json.loads((MEDIA/'fluid-25d-recipe.json').read_bytes())
        self.assertEqual(recipe['hero']['probe']['nb_read_frames'],'480')
        self.assertNotIn('story',recipe)
        self.assertEqual(recipe['source_protocol']['source_dirty_diff_sha256'],hashlib.sha256(b'').hexdigest())
        self.assertEqual(recipe['source_protocol']['source_status_sha256'],hashlib.sha256(b'').hexdigest())
        self.assertEqual(recipe['hero']['probe']['width'],1920)
        self.assertEqual(recipe['hero']['probe']['height'],1080)
        self.assertEqual(recipe['hero']['sha256'],'f35e77792d96e640bcefafad9af2edc2e47b14e62f19fcd67c26555256e76576')
        self.assertEqual(recipe['solver']['extension_sha256'],recipe['source_protocol']['input']['extension_sha256'])
        self.assertIn('SynxFlow',recipe['solver']['name'])
        self.assertEqual(recipe['hero']['shot']['dots'],False)
        self.assertEqual(recipe['hero']['unique_hydraulic_states'],72)
        self.assertEqual(recipe['source_protocol']['input']['identity']['spec']['site'],'joining-valleys')
        self.assertEqual(recipe['hero']['editorial']['cloned_frames'],0)
        self.assertEqual(recipe['hero']['editorial']['hydraulic_dispatches'],0)
        self.assertEqual(recipe['hero']['editorial']['closing_held_native_frames'],120)
        self.assertIn('--headless',recipe['hero']['command'])
        self.assertNotIn('--fluid25d-external-session',recipe['hero']['command'])
        self.assertEqual(recipe['hero']['cleanup']['active_group_remaining'],[])

    def test_legacy_mountain_film_is_separate_and_unchanged(self):
        recipe=json.loads((MEDIA/'fluid-25d-legacy-v1-recipe.json').read_bytes())
        self.assertEqual(recipe['schema'],'cubey.fluid25d.presentation.v1')
        self.assertEqual(recipe['story']['probe']['nb_read_frames'],'2160')
        for name in ('fluid-25d-story.mp4','fluid-25d-story.png'):
            with (MEDIA/name).open('rb') as stream:
                self.assertEqual(hashlib.file_digest(stream,'sha256').hexdigest(),recipe['media'][name]['sha256'])
        segments=recipe['story']['segments']
        self.assertTrue(all(a['last_saved_s']<=b['first_saved_s'] for a,b in zip(segments,segments[1:])))

    def test_existing_four_media_are_still_pinned(self):
        manifest=json.loads((MEDIA/'manifest.json').read_bytes())
        expected={'ocean':'dd0c33c65ba355c75da484875822fef2abf7dd2a61fc611b3b21671302216d6b',
                  'gltf-terrain':'936661a15f5b49b915d5a09be8a0ae061e4a14b6a8a0b254f28268c285bb7bc2',
                  'water-3d':'90d242578225647d82ae8041e32b63f66f9ac4797622d0413360c62d7f438ece',
                  'fire-3d':'3fc22accb5084a27acd7435d0ffbabbd597229cc3dcb555e83f831b1fa215348'}
        # All original entries are audited against their retained metadata; extra
        # pins protect the fluid siblings while no native-player URL is fabricated.
        self.assertEqual([e['id'] for e in manifest['entries'][:4]],['ocean','gltf-terrain','water-3d','fire-3d'])
        for entry in manifest['entries'][:4]:
            path=ROOT/entry['publication']['path']
            with path.open('rb') as stream:
                self.assertEqual(hashlib.file_digest(stream,'sha256').hexdigest(),entry['publication']['sha256'])
            self.assertEqual(entry['publication']['sha256'],expected[entry['id']])
            poster=entry['poster']
            with (ROOT/poster['path']).open('rb') as stream:
                self.assertEqual(hashlib.file_digest(stream,'sha256').hexdigest(),poster['sha256'])
        self.assertEqual([e['publication']['github_attachment_url'].rsplit('/',1)[-1]
                          for e in manifest['entries'][:4]],
                         ['dc923c55-1061-402f-92ad-09b7da2e7208','a6119257-6810-440d-b910-e247274fbd02',
                          'd1267204-7857-40d8-a259-4205605a5a75','50052e2e-21f0-46fe-be94-9d8a93a657d0'])


if __name__=='__main__':
    unittest.main()
