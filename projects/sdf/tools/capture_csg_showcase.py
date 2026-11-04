#!/usr/bin/env python3
"""Capture the approved CSG collection from tracked JSONs with exact provenance."""
import argparse
import copy
import datetime as dt
from fractions import Fraction
import json
from pathlib import Path
import re
import shutil
import subprocess

from refine_csg_showcase import ROOT, sha, save, validate

ROSTER = Path(__file__).with_name('csg_showcase.json')


def probe(path):
    command=['ffprobe','-v','error','-count_frames','-show_streams','-show_format','-of','json',str(path)]
    info=json.loads(subprocess.check_output(command,text=True))
    videos=[s for s in info['streams']if s['codec_type']=='video']
    assert len(videos)==1 and not any(s['codec_type']=='audio'for s in info['streams'])
    v=videos[0]
    return dict(width=int(v['width']),height=int(v['height']),codec=v['codec_name'],pixelFormat=v['pix_fmt'],
        frameRate=v['avg_frame_rate'],frames=int(v['nb_read_frames']),durationSeconds=float(v.get('duration',info['format']['duration'])))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True,help='Fresh output directory; contains frozen inputs, native masters and logs')
    parser.add_argument('--binary',type=Path,default=Path('build/release/projects/sdf/sdf'))
    parser.add_argument('--posters-only',action='store_true',help='Review framing before the full film capture')
    args=parser.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    for name in ('inputs','captures','logs','source'): (out/name).mkdir()
    binary=(ROOT/args.binary).resolve()
    roster=json.loads(ROSTER.read_text())
    scenes={}
    for e in roster['entries']:
        scene=ROOT/e['scene'];assert sha(scene)==e['approvedSceneSha256'],e['id']+' geometry drift'
        scenes[e['scene']]=sha(scene);validate(json.loads(scene.read_text()))
    section=roster['cutaway'];scene=ROOT/section['sectionScene']
    assert sha(scene)==section['approvedSectionSha256'];scenes[section['sectionScene']]=sha(scene)
    for path in scenes:shutil.copy2(ROOT/path,out/'inputs'/Path(path).name)
    head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    dirty=subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).splitlines()
    receipt=dict(format='cubey.sdf.showcase-capture',version=1,createdUtc=dt.datetime.now(dt.timezone.utc).isoformat(),
        sourceHead=head,worktreeDirty=bool(dirty),worktreeStatus=dirty,approvedModelCommit=roster['approvedModelCommit'],
        binary=str(binary),binarySha256=sha(binary),shaderSha256={p.name:sha(p)for p in sorted((binary.parent/'shaders').glob('*.spv'))},
        rosterSha256=sha(ROSTER),sceneSha256=scenes,generatorSha256=sha(__file__),runs={},complete=False,
        scope='Native recorded stills/replay and camera motion. Encoded FPS does not establish interactive rendering or editing latency.')
    for path in (__file__,ROSTER):shutil.copy2(path,out/'source'/Path(path).name)
    save(out/'manifest.json',receipt)
    def run(identifier,preset,subject,film=False,duration=None):
        config=json.loads((ROOT/preset).read_text())
        source_scene=config['authored-scene'];assert source_scene in scenes
        config['authored-scene']=str(out/'inputs'/Path(source_scene).name)
        cfg=out/'inputs'/(identifier+'-config.json');save(cfg,config)
        target=out/'captures'/(identifier+('.mp4'if film else'.png'))
        command=[str(binary),'--config',str(cfg),'--headless','--require-validation',
            '--width','1920'if film else'1600','--height','1080'if film else'900','--output',str(target)]
        if film:command+=['--capture','video','--fps','30','--frames',str(int(duration*30))]
        else:command+=['--frames','1']
        log=out/'logs'/(identifier+'.log')
        with log.open('w')as f:
            result=subprocess.run(command,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,timeout=900)
        text=log.read_text()
        if result.returncode or 'VUID-'in text or 'Validation Error'in text:raise RuntimeError(text[-4000:])
        row=dict(id=identifier,subject=subject,kind='video'if film else'image',sourcePreset=preset,
            sourcePresetSha256=sha(ROOT/preset),sourceScene=source_scene,sourceSceneSha256=scenes[source_scene],
            frozenConfig=str(cfg.relative_to(out)),frozenConfigSha256=sha(cfg),command=command,
            file=str(target.relative_to(out)),sha256=sha(target),bytes=target.stat().st_size,exitCode=result.returncode,
            log=str(log.relative_to(out)),logSha256=sha(log),stats=[s for s in text.splitlines()if s.startswith('sdf:')])
        if film:
            row['video']=probe(target);v=row['video']
            assert v['frames']==int(duration*30) and Fraction(v['frameRate'])==30 and v['width']==1920 and v['height']==1080,v
            assert abs(v['durationSeconds']-duration)<.04,v
            if subject=='robot':
                count=len(json.loads((ROOT/source_scene).read_text())['history'])
                visited=[int(s)for s in re.findall(r'sdf: history step=(\d+)',text)]
                assert visited==list(range(1,count+1)),visited
                row['savedStatesVisited']=visited
        receipt['runs'][identifier]=row;save(out/'manifest.json',receipt)
        print(identifier,'PASS',flush=True)
    for entry in roster['entries']:run('csg-'+entry['id'],entry['preset'],entry['id'])
    for state in ('intact','section'):
        run('csg-observatory-'+state,section[state+'Preset'],'observatory')
    if not args.posters_only:
        for entry in roster['entries']:
            run('csg-'+entry['id']+'-film',entry['filmPreset'],entry['id'],True,entry['durationSeconds'])
    assert sha(binary)==receipt['binarySha256']
    assert all(sha(binary.parent/'shaders'/name)==h for name,h in receipt['shaderSha256'].items())
    receipt.update(complete=True,runtimeUnchanged=True,postersOnly=args.posters_only)
    save(out/'manifest.json',receipt)


if __name__=='__main__':main()
