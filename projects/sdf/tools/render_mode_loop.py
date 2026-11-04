#!/usr/bin/env python3
"""Serial native render-mode promotion evidence. Fresh output directories."""
import argparse
import json
from pathlib import Path
from sdf_paths import resolve_sdf_path

from acceleration_loop import gpu_state, save
from query_study import digest, invoke, metadata
from quality_speed_loop import command, profile

MODES=('cached','cached-refined','balanced','analytic','hybrid')
RAW={
    'cached':['--surface-trace','sampled','--csg-evaluation','full'],
    'cached-refined':['--surface-trace','sampled','--csg-evaluation','full','--region-axis','64','--cached-quality','refine-0.03125'],
    'balanced':['--surface-trace','sampled','--csg-evaluation','full','--region-axis','64','--cached-quality','refine-normal'],
    'analytic':['--surface-trace','analytic','--csg-evaluation','pruned','--region-axis','64','--acceleration-variant','root-far'],
    'hybrid':['--surface-trace','hybrid','--csg-evaluation','pruned','--region-axis','16'],
}


def settings(args, scene, view, route, samples):
    flags=command(args,scene,view,route)
    flags[flags.index('--supersample')+1]=str(samples)
    return flags


def sampling(args):
    receipt=dict(metadata(args.binary),tool_sha256=digest(Path(__file__)),runs=[],captures=[],complete=False)
    cases=[(scene,view,route,ss) for scene in ('guardian','robot') for view in ('preset','close')
           for route in ('sampled','refine03125','combined','source') for ss in (1,2)]
    for repeat in (1,2,3):
        for scene,view,route,ss in cases if repeat%2 else reversed(cases):
            prefix=args.output/f'{scene}-{view}-{route}-ss{ss}-r{repeat}'
            flags=settings(args,scene,view,route,ss)+['--headless-render-only','--frames','180',
                '--profile-warmup-frames','30','--profile-output',str(prefix)]
            before=gpu_state();wall=invoke(flags,prefix,timeout=240);after=gpu_state()
            passes,metrics=profile(prefix,180,30)
            receipt['runs'].append(dict(scene=scene,view=view,route=route,supersample=ss,repeat=repeat,
                command=flags,wall_seconds=wall,gpu_before=before,gpu_after=after,passes=passes,metrics=metrics))
            save(args.output/'sampling.json',receipt)
            print(prefix.name,round(passes['sdf.whole_scene']['p95_ms'],3),flush=True)
    # Native captures follow the timed batch, never overlap it.
    for scene,view,route,ss in cases:
        prefix=args.output/f'{scene}-{view}-{route}-ss{ss}'
        flags=settings(args,scene,view,route,ss)+['--frames','1','--output',str(prefix.with_suffix('.png'))]
        invoke(flags,prefix,timeout=240)
        receipt['captures'].append(dict(scene=scene,view=view,route=route,supersample=ss,command=flags,
            image=str(prefix.with_suffix('.png')),image_sha256=digest(prefix.with_suffix('.png'))))
        save(args.output/'sampling.json',receipt)
    receipt['complete']=True;save(args.output/'sampling.json',receipt)


def public_command(args,scene,view,mode,ss=1,width=320,height=180):
    flags=command(args,scene,view,'sampled',width,height)[:-4]
    flags[flags.index('--supersample')+1]=str(ss)
    return flags+['--render-mode',mode]


def aliases(args):
    receipt=dict(metadata(args.binary),runs=[],complete=False)
    for scene in ('guardian','robot'):
        for ss in (1,2):
            for mode in MODES:
                images=[];commands=[]
                for kind in ('named','raw'):
                    prefix=args.output/f'{scene}-{mode}-ss{ss}-{kind}'
                    flags=public_command(args,scene,'preset',mode,ss)
                    if kind=='raw':flags=flags[:-2]+RAW[mode]
                    flags+=['--frames','1','--output',str(prefix.with_suffix('.png'))]
                    invoke(flags,prefix,timeout=240)
                    images.append(digest(prefix.with_suffix('.png')));commands.append(flags)
                if images[0]!=images[1]:raise RuntimeError('public alias changed image: '+prefix.name)
                receipt['runs'].append(dict(scene=scene,mode=mode,supersample=ss,
                    commands=commands,image_sha256=images[0],native_pixels_identical=True))
                save(args.output/'aliases.json',receipt)
                print(scene,mode,ss,'alias pixels identical',flush=True)
    receipt['complete']=True;save(args.output/'aliases.json',receipt)


def action_run(args,name,document,actions,scene='robot',initial='cached',width=320,height=180):
    path=args.output/(name+'-actions.json')
    save(path,dict(format='cubey.sdf.action-study',version=1,capture_frames=True,actions=actions))
    model=args.output/(name+'-scene.json');save(model,document)
    prefix=args.output/name
    flags=public_command(args,scene,'preset',initial,width=width,height=height)+[
        '--headless-render-only','--frames',str(len(actions)),'--authored-scene',str(model),
        '--comparison-actions',str(path),'--comparison-output',str(prefix.with_suffix('.json'))]
    invoke(flags,prefix,timeout=300)
    rows=json.loads(prefix.with_suffix('.json').read_text())['actions']
    for row in rows:row['image_sha256']=digest(Path(row['frame_png']))
    return dict(command=flags,actions=rows)


def transitions(args):
    document=dict(format='cubey.sdf.scene',version=2,name='Mode transition contract',
        materials=[dict(name='body',base_color=[.5,.4,.3],roughness=.6)],
        root=dict(type='union',name='root',children=[
            dict(type='sphere',name='a',radius=.13,position=[-.07,0,0],material='body'),
            dict(type='round-cone',name='b',bottom_radius=.08,top_radius=.03,
                half_length=.15,position=[.09,0,0],material='body')]))
    def action(label,mode,fault=None):
        result=dict(label=label,scene=document,visible_scene=document,geometry_unchanged=True,render_mode=mode)
        if fault:result.update(fault=fault,expect_failure=True)
        return result
    modes=['cached']
    for a in MODES:
        for b in MODES:
            if a!=b:modes.extend((a,b))
    all_actions=[action(f'move-{i}-{m}',m) for i,m in enumerate(modes)]
    result=action_run(args,'all-transitions',document,all_actions)
    settled={};edges=set();previous=None
    for row,mode in zip(result['actions'],modes):
        if row['render_mode']!=mode or row['failed']:raise RuntimeError('wrong published mode')
        if mode in settled and settled[mode]!=row['image_sha256']:raise RuntimeError('round trip changed pixels: '+mode)
        settled[mode]=row['image_sha256']
        if previous and previous!=mode:edges.add((previous,mode))
        if mode=='analytic' and row['field_bytes']:raise RuntimeError('analytical mode retained field')
        if row['field_builds'] and previous not in (None,'analytic'):
            raise RuntimeError('cached mode transition rebuilt field')
        previous=mode
    if len(edges)!=20:raise RuntimeError('directed transitions incomplete')
    failed=[action('initial','cached'),action('region-fail','balanced','mode-region'),
        action('region-retry','balanced'),action('to-refined','cached-refined'),
        action('descriptor-fail','balanced','mode-descriptors'),action('descriptor-retry','balanced'),
        action('pipeline-fail','cached','mode-pipeline'),action('pipeline-retry','cached'),
        action('to-source','analytic'),action('field-fail','cached','mode-field'),
        action('field-retry','cached'),action('activation-fail','balanced','mode-activate'),
        action('activation-retry','balanced')]
    recovery=action_run(args,'failed-transitions',document,failed)
    for i,row in enumerate(recovery['actions']):
        if row['failed']:
            before=recovery['actions'][i-1]
            for key in ('render_mode','mode_generation','field_generation','image_sha256','geometry_hash','appearance_hash','camera'):
                if row[key]!=before[key]:raise RuntimeError('failed mode changed '+key)
            if row['field_builds'] or row['region_builds']:raise RuntimeError('failed candidate published cache')
    save(args.output/'transitions.json',dict(metadata(args.binary),all_transitions=result,
        failed_transitions=recovery,directed_edges=sorted(edges),complete=True))
    print('20 directed transitions, settled image identity and five failure/retry stages pass',flush=True)


def history(args):
    import quality_speed_loop as previous
    mapping={'sampled':'cached','refine03125':'cached-refined','combined':'balanced','source':'analytic','hybrid':'hybrid'}
    for route,mode in mapping.items():previous.ROUTES[route]=['--render-mode',mode]
    args.routes=list(mapping)
    previous.history(args)


def features(args):
    import numpy as np
    from PIL import Image
    cases=[('ao',['--config','projects/sdf/presets/survey-robot-fieldkit.json','--ao']),
        ('soft-shadow',['--config','studies/sdf/guardian/presets/sculpture-guardian-limestone.json','--shadows','2']),
        ('ground-occluder',['--authored-scene','projects/sdf/comparison/initial.json','--ground','--occluder']),
        ('wood',['--workshop','cut-block','--material','wood']),
        ('metal',['--workshop','sphere','--material','rusted-steel','--lighting','material-atmosphere']),
        ('furnace',['--workshop','csg','--material','marble','--lighting','material-furnace']),
        ('glass',['--workshop','sphere','--material','glass']),
        ('frosted',['--workshop','sphere','--material','frosted-glass']),
        ('clouded-glass',['--workshop','sphere','--material','glass','--lighting','material-atmosphere','--clouds']),
        ('lighting-cycle',['--workshop','block','--material','marble','--lighting','material-atmosphere','--lighting-cycle','dawn','--time','5']),
        ('construction',['--config','projects/sdf/presets/survey-robot-fieldkit-build.json','--time','6']),
        ('presentation',['--config','studies/sdf/guardian/presets/sculpture-guardian-mirrored-build.json','--time','30'])]
    cases += [('debug-'+str(i),['--authored-scene','projects/sdf/comparison/initial.json','--debug',str(i)]) for i in range(1,9)]
    receipt=dict(metadata(args.binary),runs=[],complete=False)
    for label,extra in cases:
        for mode in ('cached','balanced'):
            prefix=args.output/(label+'-'+mode)
            flags=[str(args.binary),*extra,'--headless','--require-validation','--frames','1',
                '--width','320','--height','180','--supersample','2','--render-mode',mode,
                '--output',str(prefix.with_suffix('.png'))]
            invoke(flags,prefix,timeout=240)
            image=np.array(Image.open(prefix.with_suffix('.png')).convert('RGB')).astype(np.int16)
            magenta=(image[:,:,0]>100)&(image[:,:,2]>100)&(image[:,:,1]<8)&(abs(image[:,:,0]-image[:,:,2])<4)
            if magenta.any():raise RuntimeError('diagnostic magenta in '+prefix.name)
            receipt['runs'].append(dict(case=label,mode=mode,command=flags,image_sha256=digest(prefix.with_suffix('.png')),
                magenta_screen_pixels=int(magenta.sum()),screen_scope='Native RGB diagnostic-color screen; not complete internal floating-point instrumentation'))
            save(args.output/'features.json',receipt)
            print(label,mode,'native capture passes',flush=True)
    receipt['complete']=True;save(args.output/'features.json',receipt)


def runtime_timing(args):
    receipt=dict(metadata(args.binary),runs=[],complete=False)
    cases=[(scene,view,mode,ss) for scene in ('guardian','robot') for view in ('preset','side','close')
           for mode in ('cached','balanced','analytic') for ss in (1,2)]
    for repeat in (1,2,3):
        for scene,view,mode,ss in cases if repeat%2 else reversed(cases):
            prefix=args.output/f'{scene}-{view}-{mode}-ss{ss}-r{repeat}'
            flags=public_command(args,scene,view,mode,ss,width=1920,height=1080)+[
                '--headless-render-only','--frames','180','--profile-warmup-frames','30','--profile-output',str(prefix)]
            before=gpu_state();wall=invoke(flags,prefix,timeout=300);after=gpu_state()
            passes,metrics=profile(prefix,180,30)
            selected=metrics['sdf.render_mode']
            expected={'cached':1,'balanced':3,'analytic':4}[mode]
            if selected['effective']!=expected or selected['named']!=1 or selected['supersample']!=ss:
                raise RuntimeError('runtime selected wrong mode/sample setting')
            receipt['runs'].append(dict(scene=scene,view=view,mode=mode,supersample=ss,repeat=repeat,
                command=flags,wall_seconds=wall,gpu_before=before,gpu_after=after,passes=passes,metrics=metrics))
            save(args.output/'timing.json',receipt)
            print(prefix.name,round(passes['sdf.whole_scene']['p95_ms'],3),flush=True)
    receipt['complete']=True;save(args.output/'timing.json',receipt)


def publication(args):
    import quality_speed_loop as previous
    for route,mode in {'sampled':'cached','combined':'balanced'}.items():previous.ROUTES[route]=['--render-mode',mode]
    args.routes=['sampled','combined'];args.scenes=['guardian','robot']
    previous.publication(args)


def captures(args):
    receipt=dict(metadata(args.binary),runs=[],complete=False)
    for scene in ('guardian','robot'):
        for view in ('preset','close'):
            for mode in MODES:
                for ss in (1,2):
                    prefix=args.output/f'{scene}-{view}-{mode}-ss{ss}'
                    flags=public_command(args,scene,view,mode,ss,width=1920,height=1080)+[
                        '--frames','1','--output',str(prefix.with_suffix('.png'))]
                    invoke(flags,prefix,timeout=180)
                    receipt['runs'].append(dict(scene=scene,view=view,mode=mode,supersample=ss,command=flags,
                        image=str(prefix.with_suffix('.png')),image_sha256=digest(prefix.with_suffix('.png'))))
                    save(args.output/'captures.json',receipt)
            print(scene,view,'all public modes/samples captured',flush=True)
    receipt['complete']=True;save(args.output/'captures.json',receipt)


def motion(args):
    import math
    from query_study import ROOT, PRESETS
    receipt=dict(metadata(args.binary),runs=[],complete=False)
    for scene in ('guardian','robot'):
        config=json.loads((ROOT/str(resolve_sdf_path(f'projects/sdf/presets/{PRESETS[scene]}.json'))).read_text())
        document=json.loads((ROOT/config['authored-scene']).read_text())
        for mode,ss in [('cached',2),('balanced',1),('balanced',2)]:
            actions=[dict(label=f'arc-{i}',scene=document,visible_scene=document,geometry_unchanged=True,
                render=dict(yaw=config['yaw']+.012*math.sin(2*math.pi*i/16))) for i in range(16)]
            name=f'{scene}-{mode}-ss{ss}';path=args.output/(name+'-actions.json')
            save(path,dict(format='cubey.sdf.action-study',version=1,capture_frames=True,actions=actions))
            prefix=args.output/name
            flags=public_command(args,scene,'preset',mode,ss,width=1920,height=1080)+[
                '--headless-render-only','--frames','16','--comparison-actions',str(path),
                '--comparison-output',str(prefix.with_suffix('.json'))]
            invoke(flags,prefix,timeout=180)
            rows=json.loads(prefix.with_suffix('.json').read_text())['actions']
            if any(r['field_builds'] or r['region_builds'] for r in rows):raise RuntimeError('Motion rebuilt geometry')
            for row in rows:row['image_sha256']=digest(Path(row['frame_png']))
            receipt['runs'].append(dict(scene=scene,mode=mode,supersample=ss,command=flags,frames=rows))
            save(args.output/'motion.json',receipt);print(name,'16 native poses; no rebuild',flush=True)
    receipt['complete']=True;save(args.output/'motion.json',receipt)


def large_transitions(args):
    from query_study import ROOT,PRESETS
    receipt=dict(metadata(args.binary),runs=[],complete=False)
    for scene in ('guardian','robot'):
        config=json.loads((ROOT/str(resolve_sdf_path(f'projects/sdf/presets/{PRESETS[scene]}.json'))).read_text())
        document=json.loads((ROOT/config['authored-scene']).read_text())
        modes=['cached','cached-refined','balanced','analytic','hybrid','cached','balanced']
        actions=[dict(label=f'{i}-{mode}',scene=document,visible_scene=document,geometry_unchanged=True,
            render_mode=mode) for i,mode in enumerate(modes)]
        result=action_run(args,scene,document,actions,scene=scene)
        rows=result['actions']
        if rows[0]['image_sha256']!=rows[5]['image_sha256'] or rows[2]['image_sha256']!=rows[6]['image_sha256']:
            raise RuntimeError('large scene round trip changed pixels')
        if any(rows[i]['field_builds'] for i in (1,2,3,5,6)) or rows[4]['field_builds']!=1:
            raise RuntimeError('large scene cache reuse/recreation incorrect')
        receipt['runs'].append(dict(scene=scene,**result));save(args.output/'large-transitions.json',receipt)
        print(scene,'cache/source/cache round trip passes',flush=True)
    receipt['complete']=True;save(args.output/'large-transitions.json',receipt)


def contracts(args):
    import quality_speed_loop as previous
    previous.ROUTES['source']=RAW['analytic'];args.routes=['source']
    previous.contracts(args)


def negative(args):
    import subprocess
    base=[str(args.binary),'--headless','--headless-render-only','--frames','1','--width','32','--height','32',
          '--motion','static','--supersample','1','--authored-scene','projects/sdf/comparison/initial.json']
    cases=[('recipe',['--headless','--frames','1','--render-mode','balanced']),
        ('trace-conflict',base[1:]+['--render-mode','balanced','--surface-trace','analytic']),
        ('axis-conflict',base[1:]+['--render-mode','balanced','--region-axis','16']),
        ('brick-source',base[1:]+['--render-mode','analytic','--debug','4']),
        ('prepared-named',base[1:]+['--render-mode','analytic','--source-optimization','combined']),
        ('probe-ss2',base[1:]+['--render-mode','balanced','--supersample','2','--quality-output',str(args.output/'bad.bin')]),
        ('probe-cap',base[1:]+['--render-mode','balanced','--width','4096','--height','4096','--quality-output',str(args.output/'bad.bin')]),
        ('named-fixture',base[1:]+['--render-mode','analytic','--comparison-input','projects/sdf/comparison/fixtures.json']),
        ('private-cache-window',['--frames','1',*RAW['balanced'],'--authored-scene','projects/sdf/comparison/initial.json']),
        ('private-rootfar-window',['--frames','1',*RAW['analytic'],'--authored-scene','projects/sdf/comparison/initial.json']),
        ('private-prepared-window',['--frames','1',*RAW['analytic'],'--source-optimization','combined','--authored-scene','projects/sdf/comparison/initial.json'])]
    receipt=dict(metadata(args.binary),runs=[],complete=False)
    for label,flags in cases:
        command=[str(args.binary),*flags];r=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20)
        if not r.returncode:raise RuntimeError('Invalid mode admitted: '+label)
        (args.output/(label+'.log')).write_text(r.stdout)
        receipt['runs'].append(dict(case=label,command=command,exit_code=r.returncode,output=r.stdout))
    receipt['complete']=True;save(args.output/'negative.json',receipt)
    print(len(cases),'actual named-mode/private-admission rejections pass',flush=True)


def working_material(args):
    import copy
    document=json.loads(Path('projects/sdf/comparison/initial.json').read_text())
    edited=copy.deepcopy(document);edited['materials'][0]['base_color']=[.2,.7,.3]
    edited['materials'][0]['roughness']=.83
    actions=[dict(label='live-material',scene=edited,visible_scene=edited,geometry_unchanged=True)]
    for mode in ('balanced','analytic','hybrid','cached-refined','cached','balanced'):
        actions.append(dict(label='retain-'+mode,scene=edited,visible_scene=edited,geometry_unchanged=True,render_mode=mode))
    result=action_run(args,'working-material',document,actions,initial='balanced')
    rows=result['actions']
    if rows[0]['image_sha256']!=rows[-1]['image_sha256']:raise RuntimeError('Working material changed across mode round trip')
    if rows[0]['field_builds'] or rows[0]['region_builds']:raise RuntimeError('Live material rebuilt geometry')
    if len({r['appearance_hash'] for r in rows})!=1:raise RuntimeError('Unsaved appearance changed')
    save(args.output/'working-material.json',dict(metadata(args.binary),result=result,passed=True,
        scope='Runtime publication of an edited working material followed by mode switches; canonical scene file untouched'))
    print('Edited working material retained across every rendering mode',flush=True)


def defaults(args):
    receipt=dict(metadata(args.binary),runs=[],complete=False)
    for scene in ('guardian','robot'):
        for ss in (1,2):
            prefix=args.output/f'{scene}-ss{ss}'
            flags=public_command(args,scene,'preset','cached',ss,width=1920,height=1080)[:-2]+[
                '--frames','1','--output',str(prefix.with_suffix('.png'))]
            invoke(flags,prefix,timeout=180)
            reference=args.input/f'{scene}-preset-cached-ss{ss}.png'
            if digest(prefix.with_suffix('.png'))!=digest(reference):raise RuntimeError('Default differs from Cached')
            receipt['runs'].append(dict(scene=scene,ss=ss,command=flags,default_matches_cached=True))
    images=[]
    for mode in (None,'cached'):
        prefix=args.output/('recipe-default' if mode is None else 'recipe-cached')
        flags=[str(args.binary),'--headless','--require-validation','--frames','1','--width','160','--height','90',
            '--output',str(prefix.with_suffix('.png'))]
        if mode:flags+=['--render-mode',mode]
        invoke(flags,prefix,timeout=180);images.append(digest(prefix.with_suffix('.png')))
    if images[0]!=images[1]:raise RuntimeError('Bare recipe default changed')
    receipt.update(complete=True,bare_recipe_matches_cached=True);save(args.output/'defaults.json',receipt)
    print('Four authored defaults and bare recipe match Cached',flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase',choices=['sampling','aliases','transitions','history','features','runtime_timing','publication','captures','motion','large_transitions','contracts','negative','working_material','defaults'])
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--binary',type=Path,default=Path('build/release/projects/sdf/sdf'))
    p.add_argument('--input',type=Path)
    args=p.parse_args();args.shadows=1;args.binary=args.binary.resolve();args.output=args.output.resolve()
    args.output.mkdir(parents=True,exist_ok=False)
    globals()[args.phase](args)


if __name__=='__main__':main()
