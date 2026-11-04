#!/usr/bin/env python3
"""Resolve authored CSG joins after the forms-first pass; preserve its generator."""
import argparse
import copy
import json
import math
import shutil
import subprocess
from pathlib import Path

from sdf_author import box, capsule, subtract, union
from refine_csg_showcase import (ROOT, SCENES, PRESETS, SOURCES, refine,
    leaves, walk, family, serial, rotate, prune, validate, save, sha, capture)
from check_robot_joints import audit as audit_robot


def endpoints(node):
    offset=rotate(node.get('rotation_xyzw',[0,0,0,1]),[0,node['half_length'],0])
    return [[a+sign*b for a,b in zip(node['position'],offset)]for sign in (-1,1)]


def polish(document,subject):
    doc=copy.deepcopy(document)
    original={n['name']:{k:v for k,v in n.items()if k!='children'}for n in walk(doc['root'])}
    groups={}
    for n in walk(doc['root']):groups.setdefault(family(n),[]).append(n)
    for items in groups.values():items.sort(key=serial)
    removed=set();new_history=[]
    def one(prefix):
        assert len(groups[prefix])==1,prefix
        return groups[prefix][0]
    def move(node,delta):
        node['position']=[a+b for a,b in zip(node.get('position',[0,0,0]),delta)]
    def beam(node,start,end,radius=None):
        replacement=capsule(node['name'],start,end,radius or node['radius'],node['material'])
        node.clear();node.update(replacement)
    def append(nodes,label,after=None):
        doc['root']['children'].extend(nodes)
        refs=[n['name']for p in nodes for n in leaves(p)]
        if after:
            step=next(s for s in doc['history']if s['label']==after)
            step['nodes'].extend(refs)
        else:new_history.append(dict(nodes=refs,label=label,seconds=1.3))
    def replace(name,replacement):
        def visit(node):
            if 'children'in node:
                node['children']=[replacement if c['name']==name else c for c in node['children']]
                for c in node['children']:
                    if c is not replacement:visit(c)
        visit(doc['root'])
    if subject.startswith('observatory'):
        # Overlap the border cores at each corner, beyond their rounded ends.
        for border in groups['terrace-border']:
            axis=0 if abs(border['position'][0])<1e-9 else 2
            border['half_extent'][axis]=.359 if axis==0 else .334
        # A single landing overlaps the circular balcony at the same elevation.
        one('top-landing').update(position=[.133,-.081,.018],half_extent=[.096,.009,.027],rounding=.001)
        one('top-landing-support').update(position=[.137,-.145,.018],half_extent=[.091,.057,.029])
        # Open the rail precisely where the landing enters the balcony.
        for n in groups['top-rail']+groups['baluster']:
            a,b=endpoints(n)
            if min(a[0],b[0])>.025 and min(a[2],b[2])<.055 and max(a[2],b[2])>-.015:
                removed.add(n['name'])
        end=[.044539876433198095,-.012,.045872734486879785]
        terminal=capsule('polish-balcony-entry-post',[end[0],-.072,end[2]],end,.0045,'dark')
        # Insert beside the rail so the saved architectural section also cuts it.
        def add_terminal(node):
            if any(c['name']=='top-rail-0463'for c in node.get('children',[])):
                assert node['type']=='union';node['children'].append(terminal);return True
            return any(add_terminal(c)for c in node.get('children',[]))
        assert add_terminal(doc['root'])
        next(s for s in doc['history']if s['label']=='Frame the balcony with an open rail')['nodes'].append(terminal['name'])
    elif subject=='canal-mill':
        # Align both banks and bridge entries to a small, consistent curb.
        for prefix in ('near-bank','far-bank'):
            n=one(prefix);n['position'][1]=-.252;n['half_extent'][1]=.013;n['rounding']=.002
        for z in (.083,.282):
            row=[n for n in groups['bank-cap']if abs(n['position'][2]-z)<1e-8]
            spans=[(-.353,-.187),(-.173,-.007),(.007,.150),(.314,.353)]
            for n,(lo,hi)in zip(row,spans):
                if z<.1 and lo>.0 and hi<.2:
                    removed.add(n['name']);continue
                n['position']=[(lo+hi)/2,-.23325,z]
                n['half_extent'][0]=(hi-lo)/2
        # Two sacks stand behind the coping, on the yard, not inside the bank.
        for i,(sack,tie)in enumerate(zip(groups['grain-sack'],groups['sack-tie'])):
            if i==2:removed.update([sack['name'],tie['name']]);continue
            x=[-.10,-.05][i]
            sack['position']=[x,-.204,.022];tie['position']=[x,-.173,.022]
        for n in groups['awning-post']:
            x=n['position'][0];beam(n,[x,-.222,.098],[x,-.072,.098])
        # Give the wheel an actual water-filled race, joined to the canal.
        cutters=[]
        for prefix in ('island','yard','far-bank'):
            body=one(prefix)
            cutter=box('polish-millrace-cut-'+prefix,[.083,-.250,.053],[.051,.060,.075],.004,'earth')
            replace(body['name'],subtract('polish-open-race-'+prefix,body,cutter))
            cutters.append(cutter['name'])
        step=next(s for s in doc['history']if s['label']=='Set the island, canal and stone banks / carve')
        step['nodes'].extend(cutters)
        append([box('polish-millrace-water',[.083,-.258,.050],[.048,.012,.080],.003,'water'),
                box('polish-race-left-cheek',[.023,-.246,.045],[.005,.013,.065],.002,'stone'),
                box('polish-race-right-cheek',[.143,-.246,.045],[.005,.013,.065],.002,'stone')],
               'Connect the wheel race to the canal',after='Indicate the canal current')
        for prefix in ('wheel-rim','wheel-spoke','wheel-axle','paddle'):
            for n in groups[prefix]:move(n,[0,-.022,0])
        planks=groups['bridge-plank']
        for i,n in enumerate(planks):
            z=.075+i*.023;top=-.226+.030*math.sin(math.pi*i/9)
            n.update(position=[.232,top-.006,z],half_extent=[.066,.005,.010],rounding=.001)
        def surface(z):return -.226+.030*math.sin(math.pi*(z-.075)/.207)
        for n in groups['bridge-post']:
            x=n['position'][0];z=.075+round((n['position'][2]-.073)/.069)*.069
            beam(n,[x,surface(z)-.002,z],[x,surface(z)+.061,z])
        rails=groups['bridge-rail']
        for side in range(2):
            x=.232+(-1 if side==0 else 1)*.061
            for i in range(3):
                z=.075+i*.069;end=z+.069
                beam(rails[side*3+i],[x,surface(z)+.061,z],[x,surface(end)+.061,end])
        stringers=[]
        for side,x in enumerate((.173,.291)):
            for i in range(3):
                z=.075+i*.069;end=z+.069
                stringers.append(capsule(f'polish-bridge-stringer-{side}-{i}',
                    [x,surface(z)-.017,z],[x,surface(end)-.017,end],.010,'wood'))
        append(stringers,'Support the bridge planks',after='Bridge the canal with planks and curved rails')
        # Seat the cart on its paving, and keep both barrels off that path.
        move(one('delivery-cart'),[0,.01225,0])
        append([capsule('polish-cart-rest',[.255,-.21975,-.035],[.255,-.174,-.035],.004,'wood')],
               'Rest the delivery cart',after='Stock the yard with sacks, barrels, a cart and pots')
        for n,x,z in zip(groups['barrel-prop'],[.126,.126],[-.185,-.255]):
            old=n['children'][0]['position'];move(n,[x-old[0],-.001,z-old[2]])
    elif subject=='courtyard':
        removed.update(n['name']for n in groups['water-thread'])
        one('paving-slab').update(position=[0,-.231,.086],half_extent=[.317,.004,.196])
        ground=-.2252
        for n in groups['bench-seat']+groups['bench-back']+groups['bench-leg']:move(n,[0,.0098,0])
        for n in groups['garden-pot']:move(n,[0,.0048,0])
        move(one('cypress'),[0,.0048,0])
        for prefix in ('fountain-foot','fountain-basin','basin-rim','basin-water','fountain-stem','upper-bowl','upper-rim','fountain-finial'):
            for n in groups[prefix]:move(n,[0,.0038-(.007 if prefix=='fountain-finial'else 0),0])
        # The upper tower opening was backed by the overlapping rear-wing wall.
        original_arch=next(n for n in groups['arch']if
            any(abs(c.get('position',[0,0,0])[0]-.224)<1e-9 and abs(c['position'][1]-.047)<1e-8
                for c in n.get('children',[])))
        passage=copy.deepcopy(original_arch)
        passage['name']='polish-tower-passage'
        for i,n in enumerate(passage['children']):n['name']=f'polish-tower-passage-cut-{i}'
        room=box('polish-wing-room-cut',[.224,.078,-.166],[.042,.074,.045],0,'plaster')
        body=one('rear-wing');replace(body['name'],subtract('polish-rear-wing-passage',body,
            union('polish-passage-and-room',passage,room)))
        tower_room=copy.deepcopy(room);tower_room['name']='polish-tower-room-cut'
        body=one('bell-tower');replace(body['name'],subtract('polish-hollow-tower',body,tower_room))
        refs=[n['name']for n in leaves(passage)]+[room['name'],tower_room['name']]
        next(s for s in doc['history']if s['label']=='Raise the bell tower and its open crown / carve')['nodes'].extend(refs)
        # Match all rises to the landing and reconnect each post to the rail.
        one('stair-landing').update(position=[.195,-.009,-.086],half_extent=[.075,.010,.028])
        steps=groups['terrace-step'];rise=(.004-ground)/13
        for i,n in enumerate(steps):n['position'][1]=ground+(i+1)*rise-n['half_extent'][1]-n['rounding']
        first=ground+rise;last=ground+12*rise
        start_z,end_z=.192,-.045
        def rail_y(z):return first+.058+(start_z-z)/(start_z-end_z)*(last-first)
        for n in groups['stair-rail']:
            x=n['position'][0];beam(n,[x,rail_y(start_z),start_z],[x,rail_y(end_z),end_z])
        for n in groups['stair-baluster']:
            x,_,z=n['position'];idx=min(range(12),key=lambda i:abs(steps[i]['position'][2]-z))
            top=ground+(idx+1)*rise
            beam(n,[x,top-.001,z],[x,rail_y(z),z])
    elif subject!='robot':raise ValueError(subject)
    doc['root']=prune(doc['root'],removed)
    for step in doc['history']:
        if 'nodes'in step:step['nodes']=[n for n in step['nodes']if n not in removed]
    doc['history']=[s for s in doc['history']if s.get('nodes',[s.get('node')])]
    doc['history'].extend(new_history)
    if subject!='robot':
        doc['name']=SOURCES[subject][1]+' / joined forms'
        doc['authoring']['polish']='Continuous landings, supported contacts and restrained water; 2026-11-04'
    checks=validate(doc)
    final={n['name']:{k:v for k,v in n.items()if k!='children'}for n in walk(doc['root'])}
    checks.update(removed=sorted(removed),before_leaves=len(leaves(document['root'])),
        changed=[dict(name=n,before=original[n],after=final[n])for n in sorted(original.keys()&final.keys())if original[n]!=final[n]],
        added=[final[n]for n in sorted(final.keys()-original.keys())])
    if subject=='robot':checks['joints']=audit_robot(doc);assert checks['joints']['passed'];assert doc==document
    return doc,checks


def prepare(scene_dir,preset_dir,before_dir):
    rows=[]
    for subject,(stem,title)in SOURCES.items():
        original=ROOT/'projects/sdf/scenes'/(stem+'.json')
        prior,_=refine(json.loads(original.read_text()),subject)
        frozen=before_dir/'scenes'/(subject+'.json')
        assert prior==json.loads(frozen.read_text()),subject+' prior source drift'
        result,checks=polish(prior,subject)
        scene=scene_dir/(subject+'.json');save(scene,result)
        config=json.loads((before_dir/'presets'/(subject+'.json')).read_text())
        config['authored-scene']=str(scene.relative_to(ROOT))
        preset=preset_dir/(subject+'.json');save(preset,config)
        rows.append(dict(subject=subject,title=title,source=str(frozen.relative_to(ROOT)),source_sha256=sha(frozen),
            scene=str(scene.relative_to(ROOT)),scene_sha256=sha(scene),preset=str(preset.relative_to(ROOT)),checks=checks))
    a={n['name']:n for n in leaves(json.loads((scene_dir/'observatory.json').read_text())['root'])}
    b={n['name']:n for n in leaves(json.loads((scene_dir/'observatory-section.json').read_text())['root'])}
    assert set(a)<set(b)and len(b)-len(a)==1 and all(a[n]==b[n]for n in a)
    return rows


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene-dir',type=Path,default=SCENES)
    parser.add_argument('--preset-dir',type=Path,default=PRESETS)
    parser.add_argument('--before-dir',type=Path,required=True)
    parser.add_argument('--manifest',type=Path,required=True)
    parser.add_argument('--capture-dir',type=Path)
    parser.add_argument('--hero-only',action='store_true')
    parser.add_argument('--extra-views',action='store_true',help='Also capture matched close, rear and overhead views')
    args=parser.parse_args()
    rows=prepare(args.scene_dir.resolve(),args.preset_dir.resolve(),args.before_dir.resolve())
    save(args.manifest,rows)
    print([(r['subject'],r['checks']['before_leaves'],r['checks']['leaves'])for r in rows],flush=True)
    if args.capture_dir:
        folder=args.capture_dir.resolve()
        capture(folder,rows,args.hero_only)
        receipt=json.loads((folder/'captures.json').read_text())
        receipt['capture_helper_sha256']=receipt['generator_sha256']
        receipt['generator_sha256']=sha(__file__)
        shutil.copy2(__file__,folder/Path(__file__).name)
        if args.extra_views:
            assert not args.hero_only
            for row in rows:
                subject=row['subject']
                if subject=='observatory-section':continue
                views=dict(rear=dict(yaw=2.5,pitch=.30),high=dict(yaw=.4,pitch=.75))
                if subject!='robot':views['junction']=dict(yaw=.55,pitch=.52,distance=.72,**{'target-y':-.10})
                for variant in ('before','after'):
                    prior=next(r for r in receipt['runs']if r['subject']==subject and r['variant']==variant)
                    for view,options in views.items():
                        image=folder/f'{subject}-{variant}-{view}.png'
                        command=[str(ROOT/'build/release/projects/sdf/sdf'),'--config',prior['config_input'],
                            '--headless','--require-validation','--frames','1','--width','1440','--height','1080','--output',str(image)]
                        for k,v in options.items():command.extend(['--'+k,str(v)])
                        run=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=180)
                        log=run.stdout+run.stderr;image.with_suffix('.log').write_text(log)
                        assert run.returncode==0 and 'VUID-'not in log and 'Validation Error'not in log,log[-2000:]
                        receipt['runs'].append(dict(subject=subject,variant=variant,view=view,command=command,
                            image=str(image),image_sha256=sha(image),scene_input=prior['scene_input'],scene_sha256=prior['scene_sha256'],
                            config_input=prior['config_input'],config_sha256=prior['config_sha256'],
                            stats=[s for s in log.splitlines()if s.startswith('sdf:')]))
                        print(subject,variant,view,'PASS',flush=True)
        save(folder/'captures.json',receipt)


if __name__=='__main__':main()
