#!/usr/bin/env python3
"""Package four qualified native sculptures as a static, inspectable gallery."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

from author_sculptures import SUBJECTS

COPY = {
    'guardian': dict(kicker='Expression / blended masses',
        description='A broad stone face grows swept horns, then gains its expression through sockets, nostrils and surface-following mouth and cheek cuts.',
        verdict='The most expressive subject. Blending works well for the broad face and horn roots; the mouth and cheek treatment remain deliberately stylized.',
        chapters=[('Block the face','cranium'),('Carve the sockets','left-eye-socket'),('Shape the mouth','mouth-cut-0')]),
    'vessel': dict(kicker='Hollowing / pierced walls',
        description='A full asymmetric body becomes a hollow ceramic vessel. The handle fuses into the body and three piercings open through its front wall.',
        verdict='The clearest hollowing experiment. The simpler analytic handle and round openings read better than the more elaborate sampled alternatives.',
        chapters=[('Hollow the body','interior'),('Fuse the handle','handle-loop'),('Pierce the wall','piercing-0')]),
    'arch': dict(kicker='Carving / connected passages',
        description='Two cavities approach from opposite sides of one solid mass. Their meeting opens a passage, followed by broad cuts and shallow erosion lines.',
        verdict='The breakthrough sells the operation better than the finished object. The result is a stylized carved arch; convincing natural erosion needs a stronger study of large forms.',
        chapters=[('Solid mass','solid-arch-core'),('Front alcove','front-cavity'),('Break through','rear-cavity'),('Shape exposed planes','crown-plane')]),
    'driftwood': dict(kicker='Curved strokes / fused branches',
        description='Tapered paths form a twisting trunk and branches, including one reaching behind the fork. Hollow ends, a knot recess and long grooves shape the retained wood.',
        verdict='Useful evidence for curved authoring, with a readable fork and hollows. The grooves and simplified branch forms still feel stylized; an exact curve sweep remains a possible later improvement.',
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
    print(out)


if __name__=='__main__':main()
