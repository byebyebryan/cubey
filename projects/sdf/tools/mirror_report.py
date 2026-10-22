#!/usr/bin/env python3
"""Package qualified mirrored guardian history, views, controls and costs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('comparison','history','controls','profile','roundtrip','output'):
        p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();root=Path(__file__).resolve().parents[3]
    sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    read=lambda f:json.loads(f.read_text())
    c=read(a.comparison/'manifest.json');h=read(a.history/'manifest.json')
    controls=read(a.controls/'windowed-interaction.json');performance=read(a.profile/'manifest.json')
    roundtrip=read(a.roundtrip/'manifest.json');scene=read(a.comparison/'after/source/scene.json')
    count=len(h['history'])
    for receipt in (c,controls,performance,roundtrip):
        for key in ('binary_sha256','shader_sha256'):assert receipt[key]==h[key],key
    assert c['device']==performance['device']==h['device']
    assert h['source_scene_sha256']==c['versions']['after']['scene_sha256']==roundtrip['scene_sha256']
    assert h['history']==scene['history']
    assert h['validated_states']==[*range(count+1),count//2,0,count]
    assert h['video_builds']==count+1 and h['final_matches_static_fingerprints']
    assert h['final_image_comparison']['changed']==0
    assert sha(a.history/'presentation.mp4')==h['presentation_sha256']
    screening=read(a.history/'video-check.json')
    assert screening['passed'] and screening['decoded_frames']==h['frames']
    assert controls['passed'] and controls['paired_undo_redo_steps']==[12,21]
    assert roundtrip['export_reload_bytes_identical'] and roundtrip['all_png_bytes_identical']
    assert read(a.comparison/'intent-audit.json')['passed']
    for name,row in performance['runs'].items():assert row['scene_sha256']==c['versions'][name]['scene_sha256']
    for version in c['versions'].values():
        for row in version['runs'].values():assert sha(a.comparison/row['capture'])==row['sha256']
    for row in h['runs'].values():assert sha(a.history/row['capture'])==row['capture_sha256']
    log=(a.history/'logs/history-validation.log').read_text()
    assert [line for line in log.splitlines() if line.startswith('sdf:')]==h['runs']['history-validation']['stats']
    for key in ('mismatches','exhausted','sampling_ambiguous','part_mismatches'):
        counters=list(map(int,re.findall(key+r'=(\d+)',log)))
        assert counters and not any(counters),key
    identities={int(n):(g,m) for n,g,m in re.findall(r'history validated step=(\d+)/\d+ geometry=(\w+) appearance=(\w+)',log)}
    control_log=(a.controls/'logs/windowed-interaction.log').read_text()
    for n,g,m in re.findall(r'history (?:initial )?step=(\d+)/\d+ geometry=(\w+) appearance=(\w+)',control_log):
        assert identities[int(n)]==(g,m)
    assert 'validation error' not in control_log.lower()
    foundation=root/'outputs/sdf/mirror-foundation-native'
    f=read(foundation/'manifest.json')
    assert f['binary_sha256']==h['binary_sha256'] and f['shader_sha256']==h['shader_sha256']
    assert read(foundation/'paired-preview-check.json')['passed']
    assert read(foundation/'paired-preview-check.json')['hidden_preview_byte_identical']
    reproduction=root/'outputs/sdf/guardian-mirrored-reproduction'
    assert read(reproduction/'manifest.json')['byte_identical']
    regression=read(root/'outputs/sdf/mirror-foundation-regression/manifest.json')
    assert regression['byte_identical']
    chapters=[dict(label=item['label'],time=h['steps'][item['index']]['time']+.1)
              for item in scene['authoring']['chapters']]
    chapters.append(dict(label='Finished orbit',time=h['construction_seconds']+.2))
    data=dict(count=count,primitives=scene['authoring']['leaf_reveals'],authoring=scene['authoring'],
        views=c['views'],steps=h['steps'],previews=h['previews'],chapters=chapters,
        performance=performance['runs'],device=h['device'],source=h['source_commit'],
        states=len(set(h['validated_states'])),checkpoints=len(h['validated_states']),
        rays=sum(map(int,re.findall(r'oracle rays=(\d+)',log))),
        points=sum(map(int,re.findall(r'CPU/GPU points=(\d+)',log))),frames=h['frames'],
        history_builds=h['video_builds'],browser_review='unavailable; no connected browser',
        native_controls='passed, including paired horn and socket undo/redo')
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    for directory,name in ((a.comparison,'comparison'),(a.history,'history'),(a.controls,'controls'),
                           (a.profile,'profile'),(a.roundtrip,'roundtrip'),(foundation,'fixture')):
        shutil.copytree(directory,out/name)
    shutil.copytree(reproduction,out/'reproduction')
    shutil.copytree(root/'outputs/sdf/mirror-foundation-regression',out/'regression')
    (out/'review.json').write_text(json.dumps(data,indent=2)+'\n')
    template=Path(__file__).with_name('mirror_report_template.html').read_text()
    (out/'index.html').write_text(template.replace('__DATA__',json.dumps(data).replace('</','<\\/')))
    files={str(f.relative_to(out)):dict(bytes=f.stat().st_size,sha256=sha(f))
           for f in sorted(out.rglob('*')) if f.is_file()}
    (out/'assets.json').write_text(json.dumps(files,indent=2)+'\n')
    size=sum(f.stat().st_size for f in out.rglob('*') if f.is_file())
    assert size<=100*1024*1024,size
    print(json.dumps(dict(output=str(out),bytes=size,files=len(files)+1)))


if __name__=='__main__':main()
