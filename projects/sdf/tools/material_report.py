#!/usr/bin/env python3
"""Package the qualified guardian surface audition without changing native data."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('comparison','limestone-history','marble-history','controls','history-controls',
                 'profile','foundation','output'):
        p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();root=Path(__file__).resolve().parents[3]
    read=lambda f:json.loads(f.read_text());sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    c=read(a.comparison/'manifest.json');f=read(a.foundation/'manifest.json')
    profile=read(a.profile/'manifest.json');controls=read(a.controls/'windowed-interaction.json')
    hc=read(a.history_controls/'windowed-interaction.json')
    histories={name:read(path/'manifest.json') for name,path in
               (('limestone',a.limestone_history),('marble',a.marble_history))}
    assert f['passed'] and controls['passed'] and hc['passed']
    assert controls['procedural_controls'] and min(controls['procedural_render_changed_pixels'].values())>100
    assert hc['paired_undo_redo_steps']==[12,21]
    for receipt in (f,profile,controls,hc,*histories.values()):
        for key in ('binary_sha256','shader_sha256'):assert receipt[key]==c[key],key
    assert c['device']==profile['device']==histories['marble']['device']==histories['limestone']['device']
    versions=dict(plain='before',limestone='limestone',marble='after')
    scenes={name:read(a.comparison/version/'source/scene.json') for name,version in versions.items()}
    for name,version in versions.items():
        row=c['versions'][version]
        assert row['scene_sha256']==sha(a.comparison/version/'source/scene.json')==f['source_scene_sha256'][name]
        assert row['config_sha256']==sha(a.comparison/version/'source/preset.json')
        assert scenes[name]['root']==scenes['plain']['root'] and scenes[name]['history']==scenes['plain']['history']
        for run in row['runs'].values():assert sha(a.comparison/run['capture'])==run['sha256']
        orbit=row['orbit'];assert orbit['field_builds']==1 and orbit['frames']==240
        assert sha(a.comparison/orbit['capture'])==orbit['sha256']
        assert read(a.comparison/version/'video-check.json')['passed']
    assert read(a.comparison/'intent-audit.json')['passed']
    count=len(scenes['marble']['history']);totals=dict(states=0,checkpoints=0,rays=0,points=0)
    maxima={key:0 for key in ('material','gradient','normal','shortcut')}
    identities={}
    for name,path in (('limestone',a.limestone_history),('marble',a.marble_history)):
        h=histories[name]
        assert h['history']==scenes[name]['history']
        assert h['source_scene_sha256']==c['versions'][versions[name]]['scene_sha256']
        assert h['validated_states']==[*range(count+1),count//2,0,count]
        assert h['final_matches_static_fingerprints'] and h['final_image_comparison']['changed']==0
        for run in h['runs'].values():assert sha(path/run['capture'])==run['capture_sha256']
        log=(path/'logs/history-validation.log').read_text()
        assert [line for line in log.splitlines() if line.startswith('sdf:')]==h['runs']['history-validation']['stats']
        for key in ('mismatches','exhausted','sampling_ambiguous','part_mismatches'):
            values=list(map(int,re.findall(key+r'=(\d+)',log)));assert values and not any(values)
        identities[name]={int(n):(g,m) for n,g,m in re.findall(r'history validated step=(\d+)/\d+ geometry=(\w+) appearance=(\w+)',log)}
        totals['states']+=len(set(h['validated_states']));totals['checkpoints']+=len(h['validated_states'])
        totals['rays']+=sum(map(int,re.findall(r'oracle rays=(\d+)',log)))
        totals['points']+=sum(map(int,re.findall(r'CPU/GPU points=(\d+)',log)))
        for key in maxima:
            values=list(map(float,re.findall('max_'+key+r'_error=([\deE.+-]+)',log)))
            assert values;maxima[key]=max(maxima[key],max(values))
    assert {n:g for n,(g,_) in identities['limestone'].items()}=={n:g for n,(g,_) in identities['marble'].items()}
    for n,g,m in re.findall(r'history step=(\d+)/\d+ geometry=(\w+) appearance=(\w+)',
                            (a.history_controls/'logs/windowed-interaction.log').read_text()):
        assert identities['marble'][int(n)]==(g,m)
    control_log=(a.controls/'logs/windowed-interaction.log').read_text()
    assert 'validation error' not in control_log.lower()
    updates=re.findall(r'authored update geometry=(\w+) appearance=(\w+) field_rebuilt=(\d)',control_log)
    assert updates and all(row[2]=='0' for row in updates)
    marble=histories['marble']
    assert marble['video_builds']==count+1 and marble['frames']==2400
    assert sha(a.marble_history/'presentation.mp4')==marble['presentation_sha256']
    assert read(a.marble_history/'video-check.json')['passed']
    assert read(root/'outputs/sdf/material-reproduction.json')['byte_identical']
    profile_kinds={'plain-full':'plain','plain':'plain','limestone-pattern':'limestone',
                   'limestone':'limestone','marble-pattern':'marble','marble':'marble'}
    for name,kind in profile_kinds.items():
        assert profile['runs'][name]['scene_sha256']==c['versions'][versions[kind]]['scene_sha256']
    chapters=[dict(label=item['label'],time=marble['steps'][item['index']]['time']+.1)
              for item in scenes['marble']['authoring']['chapters']]
    chapters.append(dict(label='Finished orbit',time=marble['construction_seconds']+.2))
    data=dict(count=count,leaves=1011,versions=versions,views=c['views'],totals=totals,maxima=maxima,
        performance=profile['runs'],profile_settings=profile['settings'],device=c['device'],
        chapters=chapters,marble_steps=marble['steps'],limestone_steps=histories['limestone']['steps'],
        frames=marble['frames'],source=marble['source_commit'],browser_review='unavailable; no connected browser',
        field_bytes=profile['runs']['plain']['field_bytes'],checks=f['checks'])
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    for directory,name in ((a.comparison,'comparison'),(a.limestone_history,'limestone-history'),
                           (a.marble_history,'marble-history'),(a.controls,'controls'),
                           (a.history_controls,'history-controls'),(a.profile,'profile'),(a.foundation,'foundation')):
        shutil.copytree(directory,out/name)
    shutil.copy2(root/'outputs/sdf/material-reproduction.json',out/'reproduction.json')
    # Keep only the intended captioned film in the presentation bundle. The
    # original native film remains in the working evidence, hash recorded.
    (out/'marble-history/native.mp4').unlink()
    data['omitted_working_assets']=['marble-history/native.mp4 (native source for the captioned presentation)']
    (out/'review.json').write_text(json.dumps(data,indent=2)+'\n')
    template=Path(__file__).with_name('material_report_template.html').read_text()
    (out/'index.html').write_text(template.replace('__DATA__',json.dumps(data).replace('</','<\\/')))
    files={str(f.relative_to(out)):dict(bytes=f.stat().st_size,sha256=sha(f)) for f in sorted(out.rglob('*')) if f.is_file()}
    (out/'assets.json').write_text(json.dumps(files,indent=2)+'\n')
    size=sum(f.stat().st_size for f in out.rglob('*') if f.is_file());assert size<=100*1024*1024,size
    print(json.dumps(dict(output=str(out),bytes=size,files=len(files)+1)))


if __name__=='__main__':main()
