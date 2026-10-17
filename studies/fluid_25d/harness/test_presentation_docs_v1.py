"""Current docs and immutable history; no network, GPU or optional runtime."""
import ast
import hashlib
import json
import re
import sys
from pathlib import Path
import unittest

import run_terrain_rain_demo as terrain

ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
NOTES=HERE.parent/'notes'
BUILTIN=ROOT/'projects/fluid/fluid_25d'
TERRAIN_WATER=ROOT/'projects/terrain_water'


sys.path.insert(0, str(ROOT / "tools"))
from repo_docs import link_errors, slug
from check_documentation import check_media


class Documentation(unittest.TestCase):
    def test_current_front_pages_link_to_existing_local_targets(self):
        for path in (BUILTIN/'README.md',NOTES/'demo_setup_v1.md',ROOT/'projects/fluid/README.md',
                     TERRAIN_WATER/'README.md',TERRAIN_WATER/'demo_setup_v1.md',
                     TERRAIN_WATER/'current_reference.md',TERRAIN_WATER/'integration.md',
                     ROOT/'README.md', ROOT/'docs/showcase.md', ROOT/'docs/media/showcase/README.md',
                     NOTES/'presentation_v1.md', NOTES/'current_reference.md',
                     NOTES/'development_map.md', ROOT/'docs/README.md',
                     ROOT/'docs/architecture/fluid-simulation.md', TERRAIN_WATER/'external_synxflow/README.md'):
            self.assertEqual(link_errors(ROOT, path), [], str(path))

    def test_front_page_is_current_not_a_study_transcript(self):
        current=(TERRAIN_WATER/'README.md').read_text()
        self.assertLess(len(current.splitlines()),300)
        self.assertIn('separate CUDA worker',current)
        self.assertIn('no silent fallback',current.lower())
        self.assertIn('not a repository dependency',current)
        self.assertIn('README_HISTORY.md',(HERE/'README.md').read_text())
        self.assertIn('current_reference.md',current)
        self.assertIn('development_map.md',current)
        self.assertLess(len((NOTES/'presentation_v1.md').read_text().splitlines()),180)

    def test_archive_keeps_old_section_entry_points(self):
        current=(HERE/'README.md').read_text()
        history=(HERE/'README_HISTORY.md').read_text()
        for heading in re.findall(r'^#{2,4} (.+)$',history,re.M):
            self.assertIn(f'id="{slug(heading)}"',current)
        self.assertIn('not the current quickstart',history)
        self.assertGreater(len(history.splitlines()),2400)

    def test_reconciliation_preserves_both_historical_snapshots(self):
        # README history is untouched; presentation history keeps the old body exactly.
        self.assertEqual(hashlib.sha256((HERE/'README_HISTORY.md').read_bytes()).hexdigest(),
                         '126578a5596541e608f1d6588b4994ef3eade742147d62ddfa7c703f0fca290c')
        archive=(HERE/'presentation_history.md').read_bytes()
        marker=b'<!-- preserved-presentation-body -->\n\n'
        self.assertEqual(archive.count(marker),1)
        body=archive.split(marker,1)[1]
        self.assertEqual(hashlib.sha256(body).hexdigest(),
                         '408a21304edcfd773dc853741ded79658049680368fb5da47a9e591de02d181b')
        current=(NOTES/'presentation_v1.md').read_text()
        for heading in re.findall(r'^#{2,4} (.+)$',body.decode(),re.M):
            self.assertIn(f'id="{slug(heading)}"',current)

    def test_reference_grid_table_matches_preparation_contract(self):
        reference=(TERRAIN_WATER/'current_reference.md').read_text()
        rows=re.findall(r'^\| `([^`]+)` \| (\d+)² \| (\d+) m \| ([\d.]+) km \|$',
                        reference,re.M)
        actual={name:(int(n),int(spacing)) for name,n,spacing,_ in rows}
        preparation=ast.parse((TERRAIN_WATER/'tools/prepare_case.py').read_text())
        configs=next(ast.literal_eval(node.value) for node in preparation.body
                     if isinstance(node,ast.Assign)
                     and any(isinstance(target,ast.Name) and target.id=='CONFIGS'
                             for target in node.targets))
        self.assertEqual(actual,configs)
        self.assertEqual(set(actual),set(terrain.CONFIGS))
        for _,n,spacing,extent in rows:
            self.assertAlmostEqual(int(n)*int(spacing)/1000,float(extent))

    def test_reference_entry_defaults_match_launcher_without_requiring_assets(self):
        reference=(TERRAIN_WATER/'current_reference.md').read_text()
        args=terrain.parser().parse_args(['--study','/not-a-prepared-study'])
        self.assertIn(f'`{args.configuration}`',reference)
        self.assertIn(args.site.title(),reference)
        self.assertIn(f'{args.render_fps} Hz render cap',reference)
        self.assertIn('Bare application, no input flags',reference)
        self.assertIn('Builtin virtual-pipes',reference)
        self.assertIn('Published highlight',reference)
        self.assertIn('Recorded Joining valleys',reference)
        self.assertEqual(args.mode,'live')

    def test_shared_indexes_match_selected_gallery(self):
        self.assertEqual(check_media(ROOT)["errors"], [])
        terrain_docs=(ROOT/'projects/terrain/README.md').read_text()
        self.assertNotIn('source sampling and exterior-ground resources',terrain_docs)

    def test_study_entries_distinguish_historical_limits_from_current_status(self):
        perf=(NOTES/'live_perf_terrain_v1.md').read_text()
        self.assertIn('Historical V1 study',perf)
        self.assertIn('offscreen_perf_v2.md',perf)
        boundary=(NOTES/'shoreline_boundary_study_v1.md').read_text()
        self.assertIn('Historical offline checkpoint',boundary)
        self.assertIn('current_reference.md',boundary)
        decision=(NOTES/'development_map.md').read_text()
        self.assertIn('not a new backlog',decision)
        self.assertIn('not establish a fundamental impossibility',decision)
        self.assertIn('noise remains optional',decision)

    def test_inventory_promotes_the_demo_without_claiming_equivalent_solvers(self):
        root=(ROOT/'README.md').read_text()
        docs=(ROOT/'docs/README.md').read_text()
        self.assertNotIn('Fluid 2.5D design and CPU contract',docs)
        self.assertNotIn('is a design-first direction',root)
        self.assertIn('Terrain Water',docs)
        self.assertIn('Fluid 2.5D builtin shallow water',docs)

    def test_selected_cut_is_published_without_promoting_interactive_defaults(self):
        for path in (ROOT/'README.md',TERRAIN_WATER/'README.md',ROOT/'docs/showcase.md'):
            source=path.read_text()
            self.assertIn('Joining valleys',source)
            self.assertNotIn('Owner creative review and GitHub attachment publication',source)
        root=(ROOT/'README.md').read_text()
        self.assertIn('fluid-25d-master-1080p.mp4',root)
        self.assertNotIn('36-second project film',root)
        self.assertNotIn('publication remains pending',root)
        self.assertIn('Default prepared site: Mountain',(TERRAIN_WATER/'README.md').read_text())

    def test_project_split_preserves_identity_and_backup_boundaries(self):
        builtin=(BUILTIN/'README.md').read_text()
        terrain=(TERRAIN_WATER/'README.md').read_text()
        self.assertIn('builtin shallow water',builtin)
        self.assertIn('No SynxFlow, CUDA worker',builtin)
        self.assertIn('not an\nequivalent mountain-flow backup',terrain)
        self.assertIn('No silent fallback or water-state transfer',terrain)
        entry=next(e for e in json.loads((ROOT/'docs/media/showcase/manifest.json').read_text())['entries']
                   if e['id']=='fluid-25d')
        self.assertEqual((entry['project'],entry['title']),('terrain_water','Terrain Water'))
        # Capture identity is historical and must not be rewritten by a project move.
        self.assertIn('/projects/fluid/fluid_25d/fluid_25d',entry['recipe']['capture_command'][2])
        self.assertIn('projects/terrain_water/README.md',(ROOT/'README.md').read_text())


if __name__=='__main__':
    unittest.main()
