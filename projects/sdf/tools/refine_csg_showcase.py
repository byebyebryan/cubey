#!/usr/bin/env python3
"""Refine saved CSG scenes around form and purposeful detail; preserve originals."""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess

from sdf_author import box, capsule, rotation
from check_robot_joints import audit as audit_robot

ROOT = Path(__file__).resolve().parents[3]
SCENES = ROOT / 'projects/sdf/scenes/csg-showcase-2026-11'
PRESETS = ROOT / 'projects/sdf/presets/csg-showcase-2026-11'
SOURCES = {
    'robot': ('survey-robot-fieldkit', 'Survey robot'),
    'canal-mill': ('diorama-drafts-2026-11/canal-mill', 'Canal mill'),
    'observatory': ('observatory-cleanup-2026-11/square', 'Hill observatory'),
    'courtyard': ('diorama-drafts-2026-11/terraced-courtyard', 'Terraced courtyard'),
    'observatory-section': ('observatory-cleanup-2026-11/square-section', 'Observatory section'),
}
CAMERAS = {
    'robot': dict(yaw=.45, pitch=.14, distance=1.25, **{'target-y':.035}),
    'canal-mill': dict(yaw=.63, pitch=.36, distance=1.10, **{'target-y':-.005}),
    'observatory': dict(yaw=.55, pitch=.36, distance=1.15, **{'target-y':-.005}),
    'courtyard': dict(yaw=.50, pitch=.39, distance=1.16, **{'target-y':.015}),
    'observatory-section': dict(yaw=.27, pitch=.29, distance=1.06, **{'target-y':.005}),
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, data):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(data, indent=2, allow_nan=False)+'\n')


def walk(node):
    yield node
    for child in node.get('children', []):
        yield from walk(child)


def leaves(node):
    return [n for n in walk(node) if 'children' not in n]


def family(node):
    return re.sub(r'-\d+$', '', node['name'])


def serial(node):
    match = re.search(r'-(\d+)$', node['name'])
    return int(match[1]) if match else 0


def qmul(a, b):
    x,y,z,w=a;u,v,t,s=b
    return [w*u+x*s+y*t-z*v,w*v-x*t+y*s+z*u,w*t+x*v-y*u+z*s,w*s-x*u-y*v-z*t]


def rotate(q, v):
    p=qmul(qmul(q,[*v,0]),[-q[0],-q[1],-q[2],q[3]])
    return p[:3]


def collapse(parent, child):
    """Retain inherited transforms/material when pruning a single-child group."""
    child=copy.deepcopy(child)
    scale=parent.get('scale',1)
    q=parent.get('rotation_xyzw',[0,0,0,1])
    offset=rotate(q,[v*scale for v in child.get('position',[0,0,0])])
    child['position']=[a+b for a,b in zip(parent.get('position',[0,0,0]),offset)]
    if 'rotation_xyzw' in parent or 'rotation_xyzw' in child:
        child['rotation_xyzw']=qmul(q,child.get('rotation_xyzw',[0,0,0,1]))
    if 'scale' in parent or 'scale' in child:
        child['scale']=scale*child.get('scale',1)
    if 'material' in parent and 'material' not in child:
        child['material']=parent['material']
    return child


def prune(node, removed):
    if 'children' not in node:
        return None if node['name'] in removed else node
    original=node['children']
    children=[prune(c,removed) for c in original]
    if node['type'] in ('subtract','smooth-subtract'):
        if children[0] is None:
            return None
        tools=[c for c in children[1:] if c is not None]
        if not tools:
            return collapse(node,children[0])
        node['children']=[children[0],*tools]
    else:
        children=[c for c in children if c is not None]
        if not children:
            return None
        if len(children)==1:
            return collapse(node,children[0])
        node['children']=children
    return node


def validate(document):
    all_nodes=list(walk(document['root']))
    names=[n['name'] for n in all_nodes]
    assert len(names)==len(set(names)), 'Duplicate CSG node names'
    leaf_names={n['name'] for n in leaves(document['root'])}
    polarity={}
    def signed(node,negative=False):
        if 'children' not in node:
            polarity[node['name']]=negative
        else:
            assert len(node['children'])>=2
            for i,c in enumerate(node['children']):
                signed(c,negative ^ (node['type'] in ('subtract','smooth-subtract') and i>0))
    signed(document['root'])
    recorded=[]
    for step in document['history']:
        refs=step.get('nodes',[step.get('node')])
        assert refs and set(refs)<=leaf_names
        assert len({polarity[n] for n in refs})==1, 'Mixed history polarity'
        recorded.extend(refs)
    assert len(recorded)==len(set(recorded)) and set(recorded)==leaf_names
    assert len(leaf_names)<=1024 and len(document['materials'])<=16
    return dict(leaves=len(leaf_names),tree_nodes=len(all_nodes),history_actions=len(document['history']),
                complete_history=True,unique_names=True,polarity=True)


def refine(document, subject):
    document=copy.deepcopy(document)
    items=leaves(document['root'])
    groups={}
    for node in items:
        groups.setdefault(family(node),[]).append(node)
    for nodes in groups.values():
        nodes.sort(key=serial)
    removed=set()
    edits=[]
    def drop(*prefixes):
        for prefix in prefixes:
            removed.update(n['name'] for n in groups.get(prefix,[]))
    def keep(prefix,indices):
        nodes=groups.get(prefix,[])
        removed.update(n['name'] for i,n in enumerate(nodes) if i not in indices)
        return [n for i,n in enumerate(nodes) if i in indices]
    def edit(node, **values):
        before=copy.deepcopy(node)
        node.update(values)
        edits.append(dict(name=node['name'],before=before,after=copy.deepcopy(node)))
    def first(prefix):
        return groups[prefix][0]
    if subject=='robot':
        by_name={n['name']:n for n in items}
        removed.update(['head-left-fastener','head-right-fastener','head-left-fastener-cut',
                        'head-right-fastener-cut','head-vent-cut-1','backpack-vent-cut-1','abdomen-latch'])
        edit(by_name['head-shell'],half_extent=[.136,.082,.065],rounding=.029)
        for side,sign in [('left',-1),('right',1)]:
            node=by_name[f'head-{side}-ear'];p=node['position'][:];p[0]-=sign*.009;edit(node,position=p)
        for name,y in [('head-vent-cut-0',-.022),('head-vent-cut-2',0),
                       ('backpack-vent-cut-0',.004),('backpack-vent-cut-2',-.028)]:
            node=by_name[name];p=node['position'][:];p[1]=y;edit(node,position=p)
    elif subject=='canal-mill':
        drop('tile-rib','tile-course','chimney-course','shutter-strap')
        keep('water-ripple',{0,4})
        # Eight well-spaced members explain the mechanism more clearly than a dense ring.
        for prefix in ['wheel-spoke','paddle']:
            kept=keep(prefix,set(range(8)))
            for i,node in enumerate(kept):
                t=i*math.tau/8;dy,dz=math.cos(t),math.sin(t)
                if prefix=='wheel-spoke':
                    replacement=capsule(node['name'],(.083,-.082+dy*.015,.047+dz*.015),
                                        (.083,-.082+dy*.122,.047+dz*.122),.007,'wood')
                else:
                    replacement=box(node['name'],(.083,-.082+dy*.132,.047+dz*.132),
                                    (.031,.011,.021),.003,'wood',rotation_xyzw=rotation((1,0,0),math.degrees(t)))
                edit(node,**replacement)
        caps=groups['bank-cap']
        for z in [.083,.282]:
            row=[n for n in caps if abs(n['position'][2]-z)<1e-8]
            for i,node in enumerate(row):
                if i>=4:removed.add(node['name']);continue
                p=node['position'][:];p[0]=-.27+i*.18
                extent=node['half_extent'][:];extent[0]=.083
                edit(node,position=p,half_extent=extent)
        door=keep('door-plank',{0})[0]
        edit(door,position=[-.16,-.169,.006],half_extent=[.029,.059,.006],rounding=.003)
        awning=keep('awning-stripe',{0,1,2})
        for node,x,width,mat in zip(awning,[-.1505,-.247,-.054],[.109,.012,.012],['linen','teal','teal']):
            edit(node,position=[x,-.062+(.001 if mat=='teal' else 0),.081],half_extent=[width,.007,.055],material=mat)
        node=first('island');p=node['position'][:];p[1]=-.280;e=node['half_extent'][:];e[1]=.035;edit(node,position=p,half_extent=e)
    elif subject.startswith('observatory'):
        drop('retaining-block','dome-rivet','tooth','bracket','bracket-stud','rail-stud','rail-finial',
             'lower-rail','armillary-mark','stone-block','annex-quoin','arch-stone-joint',
             'room-floorboard','moss','grass-tuft','rock-fracture')
        keep('standing-meridian',{0,3})
        keep('clock-tick',{0,3,6,9})
        keep('compass-ray',{0,2,4,6})
        keep('annex-standing-seam',{2,7})
        keep('baluster',{0,3,6,9,12,15})
        for node in groups['baluster']:
            if node['name']not in removed:edit(node,radius=.0045)
        path=keep('approach-slab',{0,5,10})
        for node in path:
            e=node['half_extent'][:];e[0]=.046;edit(node,half_extent=e)
        books=keep('book-spine',{0,3,6,9,12,15,18,21})
        for index,node in enumerate(books):
            p=node['position'][:];p[0]=-.075-.050+(index%4)*.033
            e=node['half_extent'][:];e[0]=.0105;edit(node,position=p,half_extent=e)
        node=first('room-subfloor');p=node['position'][:];p[1]=-.0735;edit(node,position=p)
        node=first('rock-core');p=node['position'][:];p[1]=-.244;e=node['half_extent'][:];e[1]=.040;edit(node,position=p,half_extent=e)
        node=first('display-foot');p=node['position'][:];p[1]=-.294;edit(node,position=p)
        keep('tube-collar',{0,3})
    elif subject=='courtyard':
        drop('tile-rib','tile-course','cornice-block')
        floor=keep('paving-slab',{0})[0]
        edit(floor,position=[-.0225,-.231,.112],half_extent=[.1915,.004,.133],material='stone')
        keep('stair-baluster',{0,1,4,5,8,9})
        for prefix in ['banner','banner-hem','banner-rod']:keep(prefix,{0})
        # Clear the entrance to the stair rather than filling every garden corner.
        for node in walk(document['root']):
            if family(node)=='garden-pot' and any(abs(c.get('position',[0,0,0])[0]-.09)<1e-8
                    and abs(c.get('position',[0,0,0])[2]-.23)<1e-8 for c in leaves(node)):
                removed.update(c['name'] for c in leaves(node))
        node=first('island');p=node['position'][:];p[1]=-.276;e=node['half_extent'][:];e[1]=.032;edit(node,position=p,half_extent=e)
    else:
        raise ValueError(subject)
    original_names={n['name']for n in items}
    assert removed<=original_names, removed-original_names
    document['root']=prune(document['root'],removed)
    new_history=[]
    for step in document['history']:
        refs=step.get('nodes',[step.get('node')]);refs=[n for n in refs if n not in removed]
        if not refs:continue
        step=copy.deepcopy(step)
        if 'nodes'in step:step['nodes']=refs
        else:step['node']=refs[0]
        step['label']=step['label'].replace('twelve paddles','eight paddles').replace('Tile the','Shape the')
        step['label']=step['label'].replace('Forge the balcony rail and brass finials','Frame the balcony with an open rail')
        step['label']=step['label'].replace('Lay the room and keeper-cabin timber floors','Lay the room and keeper-cabin floors')
        step['label']=step['label'].replace('Hang banners and furnish the garden edges','Hang a banner and furnish the garden edges')
        new_history.append(step)
    document['history']=new_history
    document['name']=SOURCES[subject][1]+' / form refinement'
    document['authoring']['refinement']='Form, spacing and purposeful detail; originals retained'
    result=validate(document)
    result.update(before_leaves=len(items),removed=sorted(removed),changed=edits)
    if subject=='robot':
        joint=audit_robot(document);assert joint['passed'];result['joint_audit']=joint
    return document,result


def prepare(scene_dir=SCENES,preset_dir=PRESETS):
    scene_dir.mkdir(parents=True,exist_ok=True);preset_dir.mkdir(parents=True,exist_ok=True)
    rows=[]
    for subject,(stem,title)in SOURCES.items():
        source=ROOT/'projects/sdf/scenes'/(stem+'.json')
        old_preset=ROOT/'projects/sdf/presets'/(stem+'.json')
        document,checks=refine(json.loads(source.read_text()),subject)
        scene=scene_dir/(subject+'.json');preset=preset_dir/(subject+'.json')
        save(scene,document)
        config=json.loads(old_preset.read_text());config.update(CAMERAS[subject]);config['authored-scene']=str(scene.relative_to(ROOT));config['render-mode']='balanced'
        save(preset,config)
        rows.append(dict(subject=subject,title=title,source=str(source.relative_to(ROOT)),source_sha256=sha(source),
                         old_preset=str(old_preset.relative_to(ROOT)),scene=str(scene.relative_to(ROOT)),
                         scene_sha256=sha(scene),preset=str(preset.relative_to(ROOT)),checks=checks))
    # The section adds exactly its cutter; all surviving named primitives agree.
    intact=json.loads((scene_dir/'observatory.json').read_text())
    section=json.loads((scene_dir/'observatory-section.json').read_text())
    a={n['name']:n for n in leaves(intact['root'])};b={n['name']:n for n in leaves(section['root'])}
    assert set(a)<set(b)and len(b)-len(a)==1
    assert all(a[n]==b[n]for n in a)
    return rows


def capture(out,rows,hero_only=False):
    out.mkdir(parents=True,exist_ok=False);(out/'inputs').mkdir()
    binary=ROOT/'build/release/projects/sdf/sdf'
    shaders={p.name:sha(p)for p in (binary.parent/'shaders').glob('*.spv')}
    receipt=dict(format='cubey.sdf.form-refinement',version=1,source_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                 generator_sha256=sha(__file__),binary_sha256=sha(binary),shaders=shaders,runs=[],complete=False)
    shutil.copy2(__file__,out/Path(__file__).name);save(out/'captures.json',receipt)
    for row in rows:
        subject=row['subject'];base=json.loads((ROOT/row['preset']).read_text())
        variants=['after']if hero_only else['before','after']
        views={'hero':{}}
        if not hero_only and subject!='observatory-section':
            views['alternate']=dict(yaw=2.35 if subject=='robot'else-.72,pitch=.18 if subject=='robot'else.40)
        for variant in variants:
            scene=out/'inputs'/f'{subject}-{variant}.json';shutil.copy2(ROOT/row['source'if variant=='before'else'scene'],scene)
            config=copy.deepcopy(base);config['authored-scene']=str(scene)
            cfg=out/'inputs'/f'{subject}-{variant}-config.json';save(cfg,config)
            for view,options in views.items():
                image=out/f'{subject}-{variant}-{view}.png'
                command=[str(binary),'--config',str(cfg),'--headless','--require-validation','--frames','1',
                         '--width','1440','--height','1080','--render-mode','balanced','--supersample','2','--output',str(image)]
                for key,value in options.items():command.extend(['--'+key,str(value)])
                run=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=180)
                log=run.stdout+run.stderr;image.with_suffix('.log').write_text(log)
                if run.returncode or 'VUID-'in log or 'Validation Error'in log:raise RuntimeError(log[-3500:])
                receipt['runs'].append(dict(subject=subject,variant=variant,view=view,command=command,image=str(image),image_sha256=sha(image),
                                             scene_input=str(scene),scene_sha256=sha(scene),config_input=str(cfg),config_sha256=sha(cfg),
                                             stats=[line for line in log.splitlines()if line.startswith('sdf:')]))
                save(out/'captures.json',receipt);print(subject,variant,view,'PASS',flush=True)
    assert sha(binary)==receipt['binary_sha256']and all(sha(binary.parent/'shaders'/n)==s for n,s in shaders.items())
    receipt.update(complete=True,runtime_unchanged=True);save(out/'captures.json',receipt)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene-dir',type=Path,default=SCENES)
    parser.add_argument('--preset-dir',type=Path,default=PRESETS)
    parser.add_argument('--capture-dir',type=Path)
    parser.add_argument('--manifest',type=Path,default=ROOT/'docs/evidence/sdf-csg-form-refinement-2026-11/refinement.json')
    parser.add_argument('--hero-only',action='store_true')
    args=parser.parse_args()
    rows=prepare(args.scene_dir.resolve(),args.preset_dir.resolve())
    save(args.manifest.resolve(),rows)
    print(json.dumps([dict(subject=r['subject'],before=r['checks']['before_leaves'],after=r['checks']['leaves'],
                           history=r['checks']['history_actions'])for r in rows]),flush=True)
    if args.capture_dir:capture(args.capture_dir.resolve(),rows,args.hero_only)


if __name__=='__main__':main()
