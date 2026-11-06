"""Capture matched native stills, clear films and scoped forward/SDF costs serially."""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import subprocess
import time
from profile_first_pass import sha, gpu_state, quantile

ROOT=Path(__file__).resolve().parents[3]
PRESETS=dict(forward='projects/pbr_materials/presets/rt-comparison',
             sdf='projects/sdf/presets/rt-comparison',rt='projects/raytracing/presets/material-workshop')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--stage',choices=['stills','films','profiles','retention'],required=True)
    p.add_argument('--resume',action='store_true')
    a=p.parse_args();os.chdir(ROOT);out=a.output.resolve();out.mkdir(parents=True,exist_ok=a.resume)
    binaries={r:ROOT/f'build/dev/projects/{b}/{b}' for r,b in [('forward','pbr_materials'),('sdf','sdf'),('rt','raytracing')]}
    identity=dict(source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        status=subprocess.check_output(['git','status','--porcelain'],text=True),
        binaries={r:sha(b) for r,b in binaries.items()},
        shaders={r:{f.name:sha(f) for f in sorted((b.parent/'shaders').glob('*.spv'))} for r,b in binaries.items()},
        inputs={str(f):sha(f) for f in [*sorted(Path('assets/materials').glob('*.json')),
            *[f for directory in PRESETS.values() for f in sorted(Path(directory).glob('*.json'))],
            *sorted(Path('projects/sdf/scenes/rt-comparison').glob('*.json'))]},gpu=gpu_state())
    if a.resume:
        prior=json.loads((out/'identity.json').read_text())
        assert all(prior[k]==identity[k] for k in ['source','binaries','shaders','inputs'])
    else:(out/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
    runs=json.loads((out/'runs.json').read_text()) if a.resume and (out/'runs.json').exists() else []
    ids=[e['id'] for e in json.loads(Path('assets/materials/catalog.json').read_text())['entries']]

    def run(name,renderer,preset='gallery-clear',extra=(),suffix='png',validated=True,profile=False):
        artifact=out/(name+'.'+suffix)
        command=[str(binaries[renderer]),'--config',f'{PRESETS[renderer]}/{preset}.json','--headless',
            '--require-validation' if validated else '--no-validation','--width','1920','--height','1080',
            '--output',str(artifact),*extra]
        if renderer=='rt':command+=['--metrics',str(out/(name+'.metrics.json'))]
        previous=next((r for r in runs if r['name']==name),None)
        if previous:
            assert previous['command']==command and all(sha(Path(f))==h for f,h in previous['artifacts'].items())
            return artifact
        start=time.monotonic();before=gpu_state()
        r=subprocess.run(command,capture_output=True,text=True,timeout=1800,
            env=dict(os.environ,VK_LAYER_VALIDATE_SYNC='1') if validated else os.environ)
        log=r.stdout+r.stderr;(out/(name+'.log')).write_text(log)
        assert r.returncode==0 and not any(t in log.lower() for t in ['vuid-','hazard detected','vulkan validation error']),log
        paths=[artifact] if not profile else list(out.glob(name+'.*'))
        if renderer=='rt':paths.append(out/(name+'.metrics.json'))
        paths += [Path(command[i+1]) for i,f in enumerate(command) if f in ['--recipe-output','--export-authored']]
        runs.append(dict(name=name,renderer=renderer,command=command,wall_seconds=time.monotonic()-start,
                         before=before,after=gpu_state(),artifacts={str(f):sha(f) for f in paths if f.is_file()}))
        (out/'runs.json').write_text(json.dumps(runs,indent=2)+'\n');print(name,'captured',flush=True)
        return artifact

    results=[]
    if a.stage=='stills':
        # Focused forward captures do not use the gallery presentation config.
        for renderer in ['forward','sdf','rt']:
            for material in ids:
                name=renderer+'-'+material
                if renderer=='forward':
                    # Empty presentation restores the isolated material view.
                    extra=['--presentation','','--material',material,'--distance','1.05','--yaw','0','--pitch','.15',
                           '--exposure','0','--recipe-output',str(out/(name+'.recipe.json'))]
                elif renderer=='sdf':
                    extra=['--authored-scene',f'projects/sdf/scenes/rt-comparison/{material}.json',
                           '--distance','1.05','--yaw','0','--pitch','-.15','--export-authored',str(out/(name+'.scene.json'))]
                else:extra=['--material',material,'--distance','1.05','--yaw','0','--pitch','.15']
                run(name,renderer,extra=['--frames','1',*extra])
            run(renderer+'-gallery',renderer,extra=['--frames','1'])
            for phase in ['dawn','dusk']:
                for seconds in [0,4,8]:
                    flag='--time' if renderer=='sdf' else '--presentation-time'
                    run(f'{renderer}-{phase}-{seconds}',renderer,'gallery-'+phase,
                        ['--frames','1',flag,str(seconds)])
        run('rt-gallery-scene','rt','gallery-scene',['--frames','1'])
        # Canonical IDs and fingerprints must survive every RT capture.
        expected=json.loads(Path('docs/evidence/raytracing-material-workshop-2026-11/p3.json').read_text())
        baseline=json.loads((ROOT/expected['raw_evidence']/'quality/glass-environment-256-seed7.json').read_text())
        canonical={r['catalog_id']:r['source_fingerprint'] for r in baseline['workshop_materials']}
        manifest=json.loads(Path(PRESETS['rt']+'/manifest.json').read_text())
        for material in ids:
            meta=json.loads((out/('rt-'+material+'.metrics.json')).read_text())
            assert len(meta['workshop_supported_material_ids'])==12
            for recipe in meta['workshop_materials']:
                assert recipe['source_fingerprint']==recipe['resolved_fingerprint']==canonical[recipe['catalog_id']]
            scene=json.loads((out/('sdf-'+material+'.scene.json')).read_text())
            assert len(scene['materials'])==1
        results.append(dict(passed=True,canonical_fingerprints=canonical,
            domain_adaptation=manifest['specimens'],scope='Canonical recipe identity and recorded SDF instance-domain transforms; no whole-image equality claim'))
    if a.stage=='films':
        for renderer in ['forward','sdf','rt']:
            for motion in ['orbit','dawn','dusk']:
                name=renderer+'-'+motion
                movie=run(name,renderer,'gallery-'+motion,
                    ['--capture','video','--frames','240','--fps','30'],suffix='mp4')
                command=['ffprobe','-v','error','-count_frames','-show_entries',
                    'stream=codec_type,codec_name,width,height,pix_fmt,r_frame_rate,nb_read_frames:format=duration','-of','json',str(movie)]
                data=json.loads(subprocess.check_output(command,text=True));streams=data['streams'];assert len(streams)==1
                s=streams[0]
                assert s['codec_type']=='video' and s['codec_name']=='h264' and s['width']==1920 and s['height']==1080
                assert s['nb_read_frames']=='240' and s['r_frame_rate']=='30/1' and float(data['format']['duration'])==8
                if renderer=='rt':
                    meta=json.loads((out/(name+'.metrics.json')).read_text());journal=meta['sampling_frames']
                    assert len(journal)==240 and meta['history_epochs']==240
                    assert all(j['new_samples']==256 and j['samples_before']==0 and j['samples_after']==256 for j in journal)
                    for i,j in enumerate(journal):
                        assert abs(j['authored_seconds']-8*i/239)<1e-6
                        yaw=math.radians(20)*i/239 if motion=='orbit' else 0
                        assert abs(j['yaw']-yaw)<1e-6
                    assert meta['as_builds']==10 and meta['target_generations']==1
                results.append(dict(name=name,passed=True,ffprobe=data,sha256=sha(movie)))
                poster=out/(name+'-poster.png')
                subprocess.run(['ffmpeg','-v','error','-y','-ss','4','-i',str(movie),'-frames:v','1',str(poster)],check=True)
    if a.stage=='profiles':
        for renderer in ['forward','sdf']:
            for repetition in range(3):
                name=f'profile-{renderer}-{repetition}'
                # These existing consumers collect timestamps on slot reuse.
                # Two extra frozen iterations retire the last measured slots.
                run(name,renderer,extra=['--headless-render-only','--frames','182','--profile-warmup-frames','30',
                    '--profile-output',str(out/name)],validated=False,profile=True)
                rows=list(csv.DictReader((out/(name+'.passes.csv')).open()))
                gpu=[r for r in rows if r['kind']=='gpu' and 30<=int(r['frame_index'])<180]
                spans={}
                for label in sorted({r['label'] for r in gpu}):
                    selected=[r for r in gpu if r['label']==label];values=[float(r['duration_ms']) for r in selected]
                    assert len(values)==150 and len({r['frame_index'] for r in selected})==150
                    spans[label]=dict(samples=150,p50_ms=quantile(values,.5),p95_ms=quantile(values,.95))
                assert spans
                results.append(dict(name=name,renderer=renderer,passed=True,gpu=spans,
                    scope='150 completed single-frame GPU spans at indices 30..179, 30 warmups and two retirement iterations; frozen sky, not RT time to convergence'))
    if a.stage=='retention':
        baseline=Path('outputs/raytracing/material-workshop/p1-74f42e3c/matched/receipt.json')
        for control in json.loads(baseline.read_text()):
            name=control['renderer']+'-retained'
            command=control['command'].copy();target=out/(name+'.png')
            command[command.index('--output')+1]=str(target)
            r=subprocess.run(command,capture_output=True,text=True,timeout=300);log=r.stdout+r.stderr
            (out/(name+'.log')).write_text(log)
            assert r.returncode==0 and sha(target)==control['image_sha256'],log
            results.append(dict(name=name,passed=True,sha256=sha(target),baseline_sha256=control['image_sha256'],command=command))
        protected=json.loads(Path('outputs/raytracing/first-pass/original-worktree.json').read_text())
        assert all(sha(Path(protected['source'])/f)==h for f,h in protected['files'].items())
        results.append(dict(name='original-SDF-horse-worktree',passed=True,protected_files=len(protected['files'])))
    (out/'receipt.json').write_text(json.dumps(dict(passed=True,stage=a.stage,identity=identity,results=results),indent=2)+'\n')


if __name__=='__main__':main()
