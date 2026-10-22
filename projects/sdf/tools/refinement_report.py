#!/usr/bin/env python3
"""Package the two refinements, matched baselines and native construction proof."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tarfile


COPY={
    'guardian':dict(title='Stone guardian',description='The same broad-faced sentinel, with resolved facial forms, inner ears, horn fluting and restrained stone damage.',
        verdict='The expression and silhouette survive the detail pass. Ears, horn flutes and the facial transitions are more resolved, but much of the rear skull remains deliberately quiet. Grazing light still reveals the sampled horns.',
        chapters=[('Resolve the cheek','left-cheek-ridge'),('Open the sockets','left-eye-socket'),
                  ('Carve the expression','mouth-0'),('Flute the horn','left-horn-flute-0'),('Chip the stone','base-chip')]),
    'driftwood':dict(title='Twisted driftwood',description='Blunt unequal limbs and a rearward turn replace the antler-like tips. Hollowing, a real split, an irregular knot and directional channels establish the wood.',
        verdict='The larger improvement: broken ends and the split now explain the object as wood. Grain bends around the knot and continues around the sides. It remains a stylized carving; the grooves are sparse and curve transitions remain visible under rear light.',
        chapters=[('Break the ends','upper-break'),('Hollow the stub','upper-hollow'),('Split the rim','end-split-0'),
                  ('Carve flowing grain','knot-left-0'),('Detail the broken end','rim-notch-a')])}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input-root',type=Path,default=Path('outputs/sdf'))
    p.add_argument('--comparison',type=Path,required=True)
    p.add_argument('--controls',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve()
    if out.exists():raise RuntimeError('Use a new report export')
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    comparison=json.loads((a.comparison/'manifest.json').read_text())
    foundation=json.loads((a.input_root/'refinement-foundation/manifest.json').read_text())
    assert comparison['robot_pixels_identical']
    data=[]
    for subject,copy in COPY.items():
        src=a.input_root/f'refinement-{subject}-qualified'
        m=json.loads((src/'manifest.json').read_text());h=json.loads((src/'history/manifest.json').read_text())
        intent=json.loads((src/'intent-audit.json').read_text())
        screen=json.loads((src/'history/video-check.json').read_text())
        controls=json.loads((a.controls/subject/'windowed-interaction.json').read_text())
        assert intent['passed'] and screen['passed'] and controls['passed']
        assert m['binary_sha256']==h['binary_sha256']==controls['binary_sha256']==comparison['binary_sha256']==foundation['binary_sha256']
        assert m['shader_sha256']==h['shader_sha256']==controls['shader_sha256']==comparison['shader_sha256']==foundation['shader_sha256']
        assert m['source_scene_sha256']==intent['scene_sha256']==h['source_scene_sha256']==sha(src/'scenes/source.json')
        assert m['reloaded_scene_and_image_identical'] and m['atlas_80_image_identical']
        assert h['final_matches_static_fingerprints'] and h['video_builds']==m['edits']+1
        assert screen['decoded_frames']==h['frames']
        assert sha(src/'history/presentation.mp4')==h['presentation_sha256']
        for group,base in ((m['runs'],src),(h['runs'],src/'history')):
            for row in group.values():
                if 'capture' in row:assert sha(base/row['capture'])==row['capture_sha256']
        for version,row in comparison['subjects'][subject].items():
            for name,r in row['runs'].items():
                assert sha(a.comparison/subject/version/f'{name}.png')==r['image_sha256']
        assert comparison['subjects'][subject]['refined']['scene_sha256']==m['source_scene_sha256']
        shutil.copytree(src,out/'data'/subject)
        shutil.copytree(a.controls/subject,out/'controls'/subject)
        labels={s['node']:i+1 for i,s in enumerate(h['history'])};previews={s['node']:s for s in h['previews']}
        chapters=[]
        for label,node in copy['chapters']:
            index=labels[node];time=(previews[node]['time'] if node in previews else h['steps'][index]['time'])
            chapters.append(dict(label=label,step=index,time=time*(h['frames']-1)/h['frames']))
        chapters.append(dict(label='Finished orbit',step=m['edits'],time=h['construction_seconds']+.2))
        stats='\n'.join(m['runs']['hero']['stats'])
        match=re.search(r'bricks=(\d+)/(\d+) allocated=(\d+) build_ms=([\d.]+)',stats);assert match
        peak=max(int(v) for r in h['runs'].values() for s in r['stats']
                 for v in re.findall(r'bricks=(\d+)/',s))
        data.append(dict(subject=subject,**{k:v for k,v in copy.items() if k!='chapters'},chapters=chapters,
            count=m['edits'],steps=h['steps'],events=h['video_events'],views=m['metadata']['views'],
            gpu=m['runs']['profile-subject']['gpu_frame'],baseline_gpu=comparison['subjects'][subject]['baseline']['gpu_frame'],
            source=m['source_commit'],dirty=m['source_dirty'],bricks=int(match[1]),peak=peak,capacity=int(match[2]),
            build_ms=float(match[4]),frames=h['frames'],duration=h['duration'],config=m['metadata']['config'],
            build_config=m['metadata']['build_config'],hidden=h['hidden_preview_allowlist']))
    shutil.copytree(a.comparison,out/'comparison')
    shutil.copy2(a.input_root/'refinement-foundation/manifest.json',out/'foundation.json')
    shutil.copy2(a.input_root/'refinement-reproduction.json',out/'reproduction.json')
    template=Path(__file__).with_name('refinement_report_template.html').read_text()
    (out/'index.html').write_text(template.replace('__DATA__',json.dumps(data).replace('</','<\\/')))
    (out/'gallery-manifest.json').write_text(json.dumps(dict(subjects=data),indent=2)+'\n')
    auxiliary=sorted([*out.glob('data/*/history/captures/preview-*.png'),*out.glob('controls/*/captures/*.png')])
    inventory={str(f.relative_to(out)):dict(sha256=sha(f),bytes=f.stat().st_size) for f in auxiliary}
    with tarfile.open(out/'auxiliary-captures.tar.xz','w:xz',preset=6) as archive:
        for f in auxiliary:archive.add(f,arcname=str(f.relative_to(out)))
    with tarfile.open(out/'auxiliary-captures.tar.xz','r:xz') as archive:
        for member in archive:
            assert hashlib.sha256(archive.extractfile(member).read()).hexdigest()==inventory[member.name]['sha256']
    for f in auxiliary:f.unlink()
    (out/'auxiliary-captures.json').write_text(json.dumps(inventory,indent=2)+'\n')
    size=sum(f.stat().st_size for f in out.rglob('*') if f.is_file())
    assert size<=100*1024*1024,size
    print(json.dumps(dict(output=str(out),bytes=size)))


if __name__=='__main__':main()
