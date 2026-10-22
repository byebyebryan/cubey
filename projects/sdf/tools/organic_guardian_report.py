#!/usr/bin/env python3
"""Package the organic guardian's matched views and qualified large history."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--comparison',type=Path,required=True)
    p.add_argument('--history',type=Path,required=True)
    p.add_argument('--controls',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    c=json.loads((a.comparison/'manifest.json').read_text());h=json.loads((a.history/'manifest.json').read_text())
    scene=json.loads((a.comparison/'after/source/scene.json').read_text());count=len(h['history'])
    assert c['binary_sha256']==h['binary_sha256'] and c['shader_sha256']==h['shader_sha256']
    assert c['device']==h['device']
    assert c['versions']['after']['scene_sha256']==h['source_scene_sha256']==sha(a.comparison/'after/source/scene.json')
    assert h['validated_states']==[*range(count+1),count//2,0,count]
    assert h['video_builds']==count+1 and h['final_matches_static_fingerprints']
    assert h['final_image_comparison']['changed']==0
    assert sha(a.history/'presentation.mp4')==h['presentation_sha256']
    screening=json.loads((a.history/'video-check.json').read_text())
    assert screening['passed'] and screening['decoded_frames']==h['frames']
    assert json.loads((a.comparison/'intent-audit.json').read_text())['passed']
    controls=json.loads((a.controls/'windowed-interaction.json').read_text())
    assert controls['passed'] and controls['binary_sha256']==h['binary_sha256']
    assert controls['shader_sha256']==h['shader_sha256']
    controls_log=(a.controls/'logs/windowed-interaction.log').read_text()
    final_identity=re.findall(rf'history step={count}/{count} geometry=(\w+) appearance=(\w+)',controls_log)
    expected_identity=re.findall(r'geometry=(\w+) appearance=(\w+)','\n'.join(h['runs']['static-final']['stats']))
    assert final_identity and final_identity[-1]==expected_identity[-1]
    for version in c['versions'].values():
        for run in version['runs'].values():assert sha(a.comparison/run['capture'])==run['sha256']
    for run in h['runs'].values():assert sha(a.history/run['capture'])==run['capture_sha256']
    chapters=[]
    for chapter in scene['authoring']['chapters']:
        chapters.append(dict(label=chapter['label'],time=h['steps'][chapter['index']]['time']*(h['frames']-1)/h['frames']))
    chapters.append(dict(label='Finished orbit',time=h['construction_seconds']+.2))
    log=(a.history/'logs/history-validation.log').read_text()
    assert [line for line in log.splitlines() if line.startswith('sdf:')]==h['runs']['history-validation']['stats']
    counters={k:list(map(int,re.findall(k+r'=(\d+)',log))) for k in ('mismatches','exhausted','sampling_ambiguous','part_mismatches')}
    assert all(counters.values())
    zero={k:sum(v) for k,v in counters.items()}
    assert not any(zero.values())
    data=dict(count=count,strokes=scene['authoring']['coherent_strokes'],views=c['views'],steps=h['steps'],
        events=h['video_events'],frames=h['frames'],chapters=chapters,source=h['source_commit'],
        states=len(set(h['validated_states'])),still_count=sum(s['capture'] is not None for s in h['steps']),
        rays=sum(map(int,re.findall(r'oracle rays=(\d+)',log))),points=sum(map(int,re.findall(r'CPU/GPU points=(\d+)',log))))
    out.mkdir(parents=True,exist_ok=False)
    shutil.copytree(a.comparison,out/'comparison');shutil.copytree(a.history,out/'history')
    shutil.copytree(a.controls,out/'controls')
    root=Path(__file__).resolve().parents[3]
    shutil.copy2(root/'outputs/sdf/organic-foundation.json',out/'foundation.json')
    shutil.copy2(root/'outputs/sdf/guardian-organic-performance.json',out/'performance.json')
    performance=json.loads((out/'performance.json').read_text())
    assert performance['binary_sha256']==h['binary_sha256'] and performance['shader_sha256']==h['shader_sha256']
    assert performance['device']==h['device']
    for name,row in performance['runs'].items():assert row['scene_sha256']==c['versions'][name]['scene_sha256']
    data['performance']=performance['runs']
    shutil.copy2(root/'outputs/sdf/guardian-organic-reproduction.json',out/'reproduction.json')
    orbit=root/'outputs/sdf/guardian-organic-orbit'
    o=json.loads((orbit/'manifest.json').read_text())
    assert o['binary_sha256']==h['binary_sha256'] and o['scene_sha256']==h['source_scene_sha256']
    assert sha(orbit/'native.mp4')==o['video_sha256']
    shutil.copytree(orbit,out/'orbit')
    roundtrip=root/'outputs/sdf/guardian-organic-roundtrip'
    r=json.loads((roundtrip/'manifest.json').read_text())
    assert r['binary_sha256']==h['binary_sha256'] and r['scene_sha256']==h['source_scene_sha256']
    assert r['export_reload_bytes_identical'] and r['all_png_bytes_identical']
    shutil.copytree(roundtrip,out/'roundtrip')
    template=Path(__file__).with_name('guardian_organic_report_template.html').read_text()
    (out/'index.html').write_text(template.replace('__DATA__',json.dumps(data).replace('</','<\\/')))
    (out/'review.json').write_text(json.dumps(data,indent=2)+'\n')
    files={str(f.relative_to(out)):dict(bytes=f.stat().st_size,sha256=sha(f)) for f in sorted(out.rglob('*')) if f.is_file()}
    (out/'assets.json').write_text(json.dumps(files,indent=2)+'\n')
    size=sum(f.stat().st_size for f in out.rglob('*') if f.is_file());assert size<=100*1024*1024,size
    print(json.dumps(dict(output=str(out),bytes=size,files=len(files)+1)))


if __name__=='__main__':main()
