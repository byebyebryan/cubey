"""Current docs and immutable history; no network, GPU or optional runtime."""
import ast
import hashlib
import json
import re
from pathlib import Path
import unittest

import run_terrain_rain_demo as terrain

ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent


def slug(text):
    return re.sub(r'\s','-',re.sub(r'[^\w\s-]','',text.lower()))


class Documentation(unittest.TestCase):
    def test_current_front_pages_link_to_existing_local_targets(self):
        for path in (HERE/'README.md',HERE/'demo_setup_v1.md',ROOT/'projects/fluid/README.md',
                     ROOT/'README.md', ROOT/'docs/showcase.md', ROOT/'docs/media/showcase/README.md',
                     HERE/'presentation_v1.md', HERE/'current_reference.md',
                     HERE/'development_map.md', ROOT/'docs/README.md',
                     ROOT/'docs/architecture/fluid-simulation.md', HERE/'external_synxflow/README.md'):
            source=path.read_text()
            for target in re.findall(r'\]\(([^)]+)\)',source):
                if '://' in target:
                    continue
                relative,_,anchor=target.partition('#')
                destination=(path.parent/relative).resolve() if relative else path
                self.assertTrue(destination.exists(),f'{path.name}: missing {target}')
                if anchor and destination.suffix=='.md':
                    text=destination.read_text()
                    anchors={slug(h) for h in re.findall(r'^#{1,6} (.+)$',text,re.M)}
                    anchors.update(re.findall(r'id="([^"]+)"',text))
                    self.assertIn(anchor,anchors,f'{path.name}: missing anchor {target}')

    def test_front_page_is_current_not_a_study_transcript(self):
        current=(HERE/'README.md').read_text()
        self.assertLess(len(current.splitlines()),300)
        self.assertIn('separate CUDA worker',current)
        self.assertIn('not silent fallbacks',current)
        self.assertIn('not a repository dependency',current)
        self.assertIn('README_HISTORY.md',current)
        self.assertIn('current_reference.md',current)
        self.assertIn('development_map.md',current)
        self.assertLess(len((HERE/'presentation_v1.md').read_text().splitlines()),180)

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
        current=(HERE/'presentation_v1.md').read_text()
        for heading in re.findall(r'^#{2,4} (.+)$',body.decode(),re.M):
            self.assertIn(f'id="{slug(heading)}"',current)

    def test_reference_grid_table_matches_preparation_contract(self):
        reference=(HERE/'current_reference.md').read_text()
        rows=re.findall(r'^\| `([^`]+)` \| (\d+)² \| (\d+) m \| ([\d.]+) km \|$',
                        reference,re.M)
        actual={name:(int(n),int(spacing)) for name,n,spacing,_ in rows}
        preparation=ast.parse((HERE/'prepare_live_perf_terrain_v1.py').read_text())
        configs=next(ast.literal_eval(node.value) for node in preparation.body
                     if isinstance(node,ast.Assign)
                     and any(isinstance(target,ast.Name) and target.id=='CONFIGS'
                             for target in node.targets))
        self.assertEqual(actual,configs)
        self.assertEqual(set(actual),set(terrain.CONFIGS))
        for _,n,spacing,extent in rows:
            self.assertAlmostEqual(int(n)*int(spacing)/1000,float(extent))

    def test_reference_entry_defaults_match_launcher_without_requiring_assets(self):
        reference=(HERE/'current_reference.md').read_text()
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
        manifest=json.loads((ROOT/'docs/media/showcase/manifest.json').read_text())
        selected=manifest['entries']
        count_words={1:'one',2:'two',3:'three',4:'four',5:'five',6:'six',7:'seven',
                     8:'eight',9:'nine',10:'ten',11:'eleven',12:'twelve'}
        word=count_words[len(selected)]
        self.assertIn(f'the {word} showcased project highlights',
                      (ROOT/'docs/README.md').read_text())
        roadmap=(ROOT/'docs/roadmap.md').read_text()
        self.assertIn(f'{word.title()} highlights are now',roadmap)
        self.assertNotIn('packaged candidate and a 36-second explanatory film',roadmap)
        self.assertNotIn('attachment publication remain pending',roadmap)
        terrain_docs=(ROOT/'projects/terrain/README.md').read_text()
        self.assertNotIn('source sampling and exterior-ground resources',terrain_docs)

    def test_study_entries_distinguish_historical_limits_from_current_status(self):
        perf=(HERE/'live_perf_terrain_v1.md').read_text()
        self.assertIn('Historical V1 study',perf)
        self.assertIn('offscreen_perf_v2.md',perf)
        boundary=(HERE/'shoreline_boundary_study_v1.md').read_text()
        self.assertIn('Historical offline checkpoint',boundary)
        self.assertIn('current_reference.md',boundary)
        decision=(HERE/'development_map.md').read_text()
        self.assertIn('not a new backlog',decision)
        self.assertIn('not establish a fundamental impossibility',decision)
        self.assertIn('noise remains optional',decision)

    def test_inventory_promotes_the_demo_without_claiming_equivalent_solvers(self):
        root=(ROOT/'README.md').read_text()
        docs=(ROOT/'docs/README.md').read_text()
        self.assertNotIn('Fluid 2.5D design and CPU contract',docs)
        self.assertNotIn('is a design-first direction',root)
        self.assertIn('Fluid 2.5D terrain-rain demo',docs)

    def test_selected_cut_is_published_without_promoting_interactive_defaults(self):
        for path in (ROOT/'README.md',HERE/'README.md',ROOT/'docs/showcase.md'):
            source=path.read_text()
            self.assertIn('Joining valleys',source)
            self.assertNotIn('Owner creative review and GitHub attachment publication',source)
        root=(ROOT/'README.md').read_text()
        self.assertIn('fluid-25d-master-1080p.mp4',root)
        self.assertNotIn('36-second project film',root)
        self.assertIn('Five highlights',root)
        self.assertNotIn('publication remains pending',root)
        self.assertIn('Default prepared site: Mountain',(HERE/'README.md').read_text())


if __name__=='__main__':
    unittest.main()
