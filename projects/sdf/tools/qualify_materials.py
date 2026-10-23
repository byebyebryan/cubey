#!/usr/bin/env python3
"""Native procedural appearance, cache, geometry and persistence regressions."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess

from sdf_author import capsule, material, rotation, smooth_union, sphere, subtract, union, write_scene
from author_guardian_materials import MARBLE_DETAILS


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,default=Path('build/release/projects/sdf/sdf'))
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();root=Path(__file__).resolve().parents[3];out=a.output.resolve()
    for folder in ('captures','logs','scenes'):(out/folder).mkdir(parents=True,exist_ok=False)
    binary=(root/a.binary).resolve();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    configs={name:root/f'projects/sdf/presets/sculpture-guardian-{suffix}.json'
             for name,suffix in (('plain','mirrored'),('limestone','limestone'),('marble','marble'))}
    sources={name:root/json.loads(config.read_text())['authored-scene'] for name,config in configs.items()}
    documents={name:json.loads(scene.read_text()) for name,scene in sources.items()}
    assert all(d['root']==documents['plain']['root'] and d['history']==documents['plain']['history']
               for d in documents.values())
    receipt=dict(binary_sha256=sha(binary),shader_sha256={f.name:sha(f) for f in sorted((binary.parent/'shaders').glob('*.spv'))},
        source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        source_scene_sha256={name:sha(scene) for name,scene in sources.items()},runs={},checks={})
    def save(): (out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')
    def run(name,kind='plain',options=(),validate=False):
        image=out/'captures'/(name+'.png')
        cmd=[str(binary),'--headless','--require-validation','--config',str(configs[kind]),
             '--width','960','--height','960',*map(str,options),'--output',str(image)]
        if validate:cmd.append('--validate-field')
        r=subprocess.run(cmd,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (out/'logs'/(name+'.log')).write_text(r.stdout)
        assert r.returncode==0 and 'validation error' not in r.stdout.lower(),r.stdout
        receipt['runs'][name]=dict(command=cmd,capture=str(image.relative_to(out)),sha256=sha(image),
            stats=[line for line in r.stdout.splitlines() if line.startswith('sdf:')])
        save();print(name,'passed',flush=True)
        return image
    def equal(left,right):
        assert left.read_bytes()==right.read_bytes(),(left,right)
    plain=run('plain',validate=True)
    equal(plain,run('plain-full',options=['--no-material-fast-path']))
    equal(plain,root/'outputs/sdf/guardian-mirrored-comparison/after/captures/hero.png')
    receipt['checks']['plain_shortcut_and_previous_png_exact']=True
    for kind in ('limestone','marble'):
        image=run(kind,kind,validate=True)
        equal(image,run(kind+'-repeat',kind))
        equal(image,run(kind+'-full',kind,['--no-material-fast-path']))
        no_bump=run(kind+'-no-bump',kind,['--no-bump'])
        assert image.read_bytes()!=no_bump.read_bytes()
        no_pattern=run(kind+'-no-pattern',kind,['--no-procedural'])
        plain_doc=copy.deepcopy(documents[kind]);plain_doc['materials'][0].pop('procedural')
        plain_source=out/'scenes'/(kind+'-plain.json');plain_source.write_text(json.dumps(plain_doc))
        equal(no_pattern,run(kind+'-plain-record',kind,['--authored-scene',plain_source]))
        export=out/'scenes'/(kind+'-export.json')
        equal(image,run(kind+'-export',kind,['--export-authored',export]))
        reexport=out/'scenes'/(kind+'-reexport.json')
        equal(image,run(kind+'-reload',kind,['--authored-scene',export,'--export-authored',reexport]))
        equal(export,reexport)
        changed=copy.deepcopy(documents[kind]);changed['materials'][0]['procedural']['seed']+=1
        changed_source=out/'scenes'/(kind+'-seed.json');changed_source.write_text(json.dumps(changed))
        assert image.read_bytes()!=run(kind+'-seed',kind,['--authored-scene',changed_source]).read_bytes()
        for debug in (1,2):
            normal=run(f'{kind}-geometry-{debug}',kind,['--debug',debug])
            equal(normal,run(f'{kind}-geometry-{debug}-plain',options=['--debug',debug]))
        stats='\n'.join(receipt['runs'][kind]['stats'])
        assert 'procedural-only cache check passed builds=1' in stats
        receipt['checks'][kind]=dict(deterministic=True,shortcut_exact=True,bump_visible=True,
            disabled_pattern_exact=True,geometry_views_exact=True,seed_visible=True,
            export_reload_exact=True,procedural_edit_keeps_one_build=True)
    # Mixed patterned/plain branches, smooth blending, inherited transforms and
    # an explicit patterned cut material exercise the general evaluator.
    stone=copy.deepcopy(documents['limestone']['materials'][0]);stone['name']='stone'
    marble=copy.deepcopy(documents['marble']['materials'][0]);marble['name']='marble'
    blended=smooth_union('blend',sphere('left',(-.08,.03,0),.16,'stone'),
                         sphere('right',(.08,.03,0),.16,'marble'),.055,True)
    cut=subtract('cut',blended,sphere('tool',(0,.03,.16),.09),'marble')
    placed=union('placed',cut,capsule('insert',(0,-.18,0),(0,-.07,0),.055,'insert'),
                 position=[.01,.01,-.02],rotation_xyzw=rotation((1,2,3),17),scale=.9)
    fixture=out/'scenes/mixed.json'
    write_scene(fixture,'Mixed procedural fixture',[stone,marble,material('insert','A85B41')],placed)
    run('mixed','plain',['--authored-scene',fixture],True)
    uniform=out/'scenes/uniform-history.json'
    body=sphere('body',(0,0,0),.2,'stone')
    tool=sphere('tool',(0,0,.18),.06,'stone')
    write_scene(uniform,'Uniform procedural history',[stone],subtract('carved',body,tool),
                [dict(node='body',label='Body'),dict(node='tool',label='Cut')])
    run('uniform-history','plain',['--authored-scene',uniform,'--validate-history'],True)
    receipt['checks']['mixed_material_cpu_gpu_passed']=True
    receipt['checks']['empty_uniform_history_passed']=True
    neutral=copy.deepcopy(documents['marble'])
    neutral['materials'][0]['procedural']['color_variation']=0
    neutral_source=out/'scenes/marble-neutral.json';neutral_source.write_text(json.dumps(neutral))
    neutral_image=run('marble-neutral','marble',['--authored-scene',neutral_source],True)
    equal(neutral_image,root/'outputs/sdf/marble-balance-native/after/captures/hero.png')
    assert neutral_image.read_bytes()!= (out/'captures/marble.png').read_bytes()
    for debug in (7,8):
        colored=run(f'marble-color-invariant-{debug}','marble',['--debug',debug])
        equal(colored,run(f'marble-neutral-invariant-{debug}','marble',
                          ['--authored-scene',neutral_source,'--debug',debug]))
    tinted=out/'scenes/marble-tinted-history.json'
    body=sphere('body',(0,0,0),.2,'marble');tool=sphere('tool',(0,0,.18),.06,'marble')
    write_scene(tinted,'Tinted marble history',[marble],subtract('carved',body,tool),
                [dict(node='body',label='Body'),dict(node='tool',label='Cut')])
    run('tinted-history','plain',['--authored-scene',tinted,'--validate-history'],True)
    assert 'marble color cache check passed builds=1' in '\n'.join(receipt['runs']['marble']['stats'])
    receipt['checks']['marble_color_variation']=dict(zero_matches_prior_png=True,
        roughness_and_shading_normals_exact=True,color_visible=True,
        empty_cut_history_passed=True,variation_edit_keeps_one_build=True)
    # Thin colored cores and irregular shoulders must agree across every named
    # detail choice, including the maximum supported color amount and another seed.
    for detail,parameters in MARBLE_DETAILS.items():
        extreme=copy.deepcopy(documents['marble'])
        extreme['materials'][0]['procedural'].update(parameters,color_variation=.25,seed=118)
        source=out/'scenes'/f'marble-{detail}-max-color.json'
        source.write_text(json.dumps(extreme))
        run(f'marble-{detail}-max-color','marble',['--authored-scene',source],True)
    receipt['checks']['marble_colored_details_max_amount_passed']=list(MARBLE_DETAILS)
    receipt['geometry_identities']={name:re.search(r'authored geometry=(\w+)',
        '\n'.join(receipt['runs'][name]['stats']))[1] for name in configs}
    assert len(set(receipt['geometry_identities'].values()))==1
    receipt['passed']=True;save();print(out/'manifest.json')


if __name__=='__main__':main()
