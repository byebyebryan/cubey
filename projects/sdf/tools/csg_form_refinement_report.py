#!/usr/bin/env python3
"""Bundle matched native comparisons for the four authored CSG refinements."""
import argparse
import json
from pathlib import Path
import shutil

from refine_csg_showcase import ROOT, sha, save

COPY = {
    'robot': dict(title='Field-kit robot', lead='A clear face and a small, readable gesture.',
        changed='Slightly narrowed the head, removed its decorative fasteners and belly latch, and reduced both vent sets to two slots.',
        kept='The lens, three-bar display, waving hand and articulated joints carry its character. Collars and seals still explain how the parts connect.',
        judgment='A light refinement. The robot already had a clear hierarchy, so its pose and main assemblies stay intact.'),
    'canal-mill': dict(title='Canal mill', lead='The roof, wheel and water make the scene.',
        changed='Removed the roof grid and chimney bands; replaced the striped canopy with a broad cream panel and two teal edges. Eight wheel members and larger bank slabs give each piece more room.',
        kept='The timber frame, wheel, curved bridge, sacks and delivery cart tell you what the place does. Bridge planks and wheel paddles remain useful repetition.',
        judgment='The broad red roof is quieter, while the wheel becomes the focal mechanism. The canal is a static authored solid; this is not a fluid simulation.'),
    'observatory': dict(title='Hill observatory', lead='A dome, a slit and the instrument inside.',
        changed='Removed dense masonry courses, dome rivets, gear teeth, brackets and rail studs. Reduced roof seams, balcony bars, approach stones and shelf books; made the foundation shallower.',
        kept='The hollow dome and slit guides, telescope fork and barrel, clock, armillary and stairs explain the building. The separate survey tripod stays on its own observing pad.',
        judgment='The largest gain in the set: the curved shell and telescope read before the ornament. The section remains part of its presentation.'),
    'courtyard': dict(title='Terraced courtyard', lead='Open arches and a clear route up the stair.',
        changed='Removed the roof grid, cornice blocks and paving checkerboard. Kept one banner, removed the pot at the stair approach, reduced rail posts and made the base shallower.',
        kept='The tower, arch rhythm, stair, fountain and bench establish scale and use. Window framing and the remaining banner give the facade enough character.',
        judgment='A calmer ground plane makes the fountain and arch openings easier to see. Functional stair and arch repetition stays.'),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--captures', type=Path, required=True)
    parser.add_argument('--qualification', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    capture = json.loads((args.captures/'captures.json').read_text())
    qualification = json.loads((args.qualification/'qualification.json').read_text())
    assert capture['complete'] and qualification['complete']
    assert capture['binary_sha256'] == qualification['binary_sha256']
    assert capture['shaders'] == qualification['shaders']
    rows = json.loads((ROOT/'docs/evidence/sdf-csg-form-refinement-2026-11/refinement.json').read_text())
    (out/'images').mkdir()
    # Copy the exact native PNGs; no retouching, relighting or generated stand-ins.
    for run in capture['runs']:
        path = Path(run['image'])
        assert sha(path) == run['image_sha256']
        shutil.copy2(path, out/'images'/path.name)
        assert sha(run['scene_input']) == run['scene_sha256']
        assert sha(run['config_input']) == run['config_sha256']
    shutil.copytree(args.captures/'inputs', out/'evidence/inputs')
    for log in args.captures.glob('*.log'):
        shutil.copy2(log, out/'evidence'/log.name)
    shutil.copytree(args.qualification, out/'evidence/qualification')
    save(out/'evidence/captures.json', capture)
    save(out/'evidence/refinement.json', rows)
    (out/'reproduce').mkdir()
    for name in ('refine_csg_showcase.py', 'qualify_csg_refinement.py', 'sdf_author.py',
                 'check_robot_joints.py', 'csg_form_refinement_report.py', 'csg_form_refinement_report.html'):
        shutil.copy2(Path(__file__).parent/name, out/'reproduce'/name)
    data = []
    for row in rows:
        subject = row['subject']
        check = next(q for q in qualification['subjects'] if q['subject'] == subject)
        assert check['scene_sha256'] == row['scene_sha256'] == sha(ROOT/row['scene'])
        for key in ('scene', 'preset', 'source', 'old_preset'):
            dest = out/'downloads'/row[key]
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT/row[key], dest)
        if subject == 'observatory-section':
            continue
        data.append(dict(id=subject, **COPY[subject], before=row['checks']['before_leaves'],
                         after=row['checks']['leaves'], history=row['checks']['history_actions'],
                         scene=row['scene'], preset=row['preset']))
    payload = dict(subjects=data, source_head=capture['source_head'], binary_sha256=capture['binary_sha256'],
                   native_frames=len(capture['runs']), native_history_states=sum(len(q['validated_states']) for q in qualification['subjects']),
                   shader_count=len(capture['shaders']))
    template = Path(__file__).with_suffix('.html').read_text()
    assert template.count('/*REPORT_DATA*/') == 1
    (out/'index.html').write_text(template.replace('/*REPORT_DATA*/', json.dumps(payload).replace('</', '<\\/')))
    save(out/'manifest.json', dict(format='cubey.sdf.form-refinement-report', version=1,
         complete=True, data=payload, files=[dict(path=str(p.relative_to(out)), bytes=p.stat().st_size, sha256=sha(p))
            for p in sorted(out.rglob('*')) if p.is_file()]))
    print(out)


if __name__ == '__main__':
    main()
