#!/usr/bin/env python3
"""Package four qualified native sculptures as a static, inspectable gallery."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tarfile

from author_sculptures import SUBJECTS

COPY = {
    'guardian': dict(kicker='Expression / blended masses',
        description='A broad stone face grows swept horns, then gains its expression through sockets, nostrils and surface-following mouth and cheek cuts.',
        verdict='The most expressive subject. Broad face and horn roots blend well; the cuts are stylized, and rear lighting still reveals some segment transitions in the horns.',
        chapters=[('Block the face','cranium'),('Carve the sockets','left-eye-socket'),('Shape the mouth','mouth-cut-0')]),
    'vessel': dict(kicker='Hollowing / pierced walls',
        description='A full asymmetric body becomes a hollow ceramic vessel. The handle fuses into the body and three piercings open through its front wall.',
        verdict='The clearest hollowing experiment. The simpler analytic handle and round openings read better than the more elaborate sampled alternatives.',
        chapters=[('Hollow the body','interior'),('Fuse the handle','handle-loop'),('Pierce the wall','piercing-0')]),
    'arch': dict(kicker='Carving / connected passages',
        description='Two cavities approach from opposite sides of one solid mass. Their meeting opens a passage; an undercut separates the feet, followed by broad cuts and shallow erosion lines.',
        verdict='The breakthrough sells the operation better than the finished object. The result is a stylized carved arch; convincing natural erosion needs a stronger study of large forms.',
        chapters=[('Solid mass','solid-arch-core'),('Front alcove','front-cavity'),('Break through','rear-cavity'),('Separate the feet','passage-floor-cut')]),
    'driftwood': dict(kicker='Curved strokes / fused branches',
        description='Tapered paths form a twisting trunk and branches, including one reaching behind the fork. Hollow ends, a knot recess and long grooves shape the retained wood.',
        verdict='Useful evidence for curved authoring, with a readable fork and hollows. Grooves and branch forms still feel stylized, and some tangent changes remain visible; an exact curve sweep is a possible later improvement.',
        chapters=[('Grow the fork','left-branch-0'),('Hollow the trunk','trunk-hollow'),('Follow the grain','grain-groove-0-0')]),
}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-root',type=Path,default=Path('outputs/sdf'))
    parser.add_argument('--controls-root',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    out=args.output.resolve()
    if out.exists():raise RuntimeError('Use a fresh report directory')
    repo=Path(__file__).resolve().parents[3]
    foundation=json.loads((args.input_root/'sculpture-foundation/manifest.json').read_text())
    data=[]
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    for subject in SUBJECTS:
        source=args.input_root/f'sculpture-{subject}-qualified'
        m=json.loads((source/'manifest.json').read_text())
        h=json.loads((source/'history/manifest.json').read_text())
        audit=json.loads((source/'intent-audit.json').read_text())
        screen=json.loads((source/'history/video-check.json').read_text())
        control_path=args.controls_root/subject/'windowed-interaction.json'
        controls=json.loads(control_path.read_text())
        assert audit['passed'] and screen['passed'] and controls['passed']
        assert m['binary_sha256']==h['binary_sha256']==controls['binary_sha256']
        assert m['shader_sha256']==h['shader_sha256']==controls['shader_sha256']
        assert m['binary_sha256']==foundation['binary_sha256']
        assert m['shader_sha256']==foundation['shader_sha256']
        assert m['reloaded_scene_and_image_identical'] and m['atlas_80_image_identical']
        assert h['final_matches_static_fingerprints'] and h['video_builds']==m['edits']+1
        assert screen['decoded_frames']==h['frames']
        assert m['source_scene_sha256']==h['source_scene_sha256']==audit['scene_sha256']
        assert sha(source/'scenes/source.json')==m['source_scene_sha256']
        assert sha(source/'history/presentation.mp4')==h['presentation_sha256']
        # Check every packaged native capture against its receipt.
        for group,base in ((m['runs'],source),(h['runs'],source/'history')):
            for row in group.values():
                if 'capture' in row:assert sha(base/row['capture'])==row['capture_sha256']
        shutil.copytree(source,out/'data'/subject)
        shutil.copytree(args.controls_root/subject,out/'controls'/subject)
        labels={s['node']:i+1 for i,s in enumerate(h['history'])}
        previews={p['node']:p for p in h['previews']}
        chapters=[]
        for label,node in COPY[subject]['chapters']:
            index=labels[node]
            t=previews[node]['time'] if node in previews else h['steps'][index]['time']
            chapters.append(dict(label=label,time=t*(h['frames']-1)/h['frames'],step=index))
        chapters.append(dict(label='Finished orbit',time=h['construction_seconds']+.2,step=m['edits']))
        stats='\n'.join(m['runs']['hero']['stats'])
        match=re.search(r'bricks=(\d+)/(\d+) allocated=(\d+) build_ms=([\d.]+)',stats)
        assert match
        text={key:value for key,value in COPY[subject].items() if key!='chapters'}
        data.append(dict(subject=subject,title=m['title'],**text,chapters=chapters,
            count=m['edits'],steps=h['steps'],previews=h['previews'],events=h['video_events'],
            views=m['metadata']['views'],gpu=m['runs']['profile-subject']['gpu_frame'],
            robot_gpu=m['runs']['profile-robot']['gpu_frame'],source=m['source_commit'],
            dirty=m['source_dirty'],bricks=int(match[1]),capacity=int(match[2]),allocated=int(match[3]),
            build_ms=float(match[4]),frames=h['frames'],duration=h['duration'],
            state_checks=len(h['validated_states']),intent_checks=len(audit['checks']),
            config=m['metadata']['config'],build_config=m['metadata']['build_config']))
    shutil.copy2(args.input_root/'sculpture-foundation/manifest.json',out/'foundation.json')
    shutil.copy2(args.input_root/'sculpture-foundation/robot-regression.json',out/'robot-regression.json')
    shutil.copy2(args.input_root/'sculpture-arch-bad-support-audit.json',out/'arch-negative-control.json')
    shutil.copy2(args.input_root/'sculpture-arch-bad-support.json',out/'arch-negative-control-scene.json')
    shutil.copy2(repo/'docs/notes/sdf-rounded-stroke-contract.md',out/'rounded-stroke-contract.md')
    template=Path(__file__).with_name('sculpture_report_template.html').read_text()
    payload=json.dumps(data).replace('</','<\\/')
    (out/'index.html').write_text(template.replace('__SCULPTURES__',payload))
    (out/'gallery-manifest.json').write_text(json.dumps(dict(subjects=data),indent=2)+'\n')
    # Keep the full native proof without exceeding the static publisher's
    # bundle ceiling. These images are auxiliary to the interactive viewers.
    auxiliary=sorted([*out.glob('data/*/history/captures/preview-*.png'),
                      *out.glob('controls/*/captures/*.png')])
    inventory={str(p.relative_to(out)):dict(sha256=sha(p),bytes=p.stat().st_size) for p in auxiliary}
    with tarfile.open(out/'auxiliary-captures.tar.xz','w:xz',preset=6) as archive:
        for path in auxiliary:archive.add(path,arcname=str(path.relative_to(out)))
    # Verify byte restoration before removing only the export's duplicate PNGs.
    with tarfile.open(out/'auxiliary-captures.tar.xz','r:xz') as archive:
        for member in archive:
            assert hashlib.sha256(archive.extractfile(member).read()).hexdigest()==inventory[member.name]['sha256']
    for path in auxiliary:path.unlink()
    (out/'auxiliary-captures.json').write_text(json.dumps(dict(
        scope='Lossless archive of all cutter on/off PNGs and desktop screenshots; paths restore relative to this report root. All state/inspection images and films remain directly accessible.',
        files=inventory),indent=2)+'\n')
    total=sum(p.stat().st_size for p in out.rglob('*') if p.is_file())
    if total>100*1024*1024:raise RuntimeError(f'Report still exceeds the 100 MiB publishing envelope: {total}')
    print(out)


if __name__=='__main__':main()
