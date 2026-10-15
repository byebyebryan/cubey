"""Current-entry links and preserved evidence; no network, GPU or optional runtime."""
import re
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent


def slug(text):
    return re.sub(r'\s','-',re.sub(r'[^\w\s-]','',text.lower()))


class Documentation(unittest.TestCase):
    def test_current_front_pages_link_to_existing_local_targets(self):
        for path in (HERE/'README.md',HERE/'demo_setup_v1.md',ROOT/'projects/fluid/README.md',
                     ROOT/'README.md', ROOT/'docs/showcase.md', ROOT/'docs/media/showcase/README.md',
                     HERE/'presentation_v1.md'):
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

    def test_archive_keeps_old_section_entry_points(self):
        current=(HERE/'README.md').read_text()
        history=(HERE/'README_HISTORY.md').read_text()
        for heading in re.findall(r'^#{2,4} (.+)$',history,re.M):
            self.assertIn(f'id="{slug(heading)}"',current)
        self.assertIn('not the current quickstart',history)
        self.assertGreater(len(history.splitlines()),2400)

    def test_inventory_promotes_the_demo_without_claiming_equivalent_solvers(self):
        root=(ROOT/'README.md').read_text()
        docs=(ROOT/'docs/README.md').read_text()
        self.assertNotIn('Fluid 2.5D design and CPU contract',docs)
        self.assertNotIn('is a design-first direction',root)
        self.assertIn('Fluid 2.5D terrain-rain demo',docs)

    def test_selected_cut_and_unpublished_attachment_are_distinct(self):
        for path in (ROOT/'README.md',HERE/'README.md',ROOT/'docs/showcase.md'):
            source=path.read_text()
            self.assertIn('Joining valleys',source)
            self.assertNotIn('Owner creative review and GitHub attachment publication',source)
        root=(ROOT/'README.md').read_text()
        self.assertIn('fluid-25d-master-1080p.mp4',root)
        self.assertNotIn('36-second project film',root)


if __name__=='__main__':
    unittest.main()
