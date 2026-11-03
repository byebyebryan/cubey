#!/usr/bin/env python3
"""Small authored-content audition using the existing native SDF renderer."""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import subprocess

from sdf_author import box, capsule, cylinder, material, rotation, round_cone, subtract, torus, union, write_scene

ROOT = Path(__file__).resolve().parents[3]
SCENES = Path('projects/sdf/scenes/content-audition-2026-11')
PRESETS = Path('projects/sdf/presets/content-audition-2026-11')
SUBJECTS = {
    'spiral-shell': dict(title='Hollow spiral shell', yaw=.22, pitch=.20, distance=1.40,
        question='Can a tapered sweep read as an organic shell, with a continuous hollow interior?',
        demo='Grow the coil, open its mouth, then reveal the internal channel.'),
    'oculus-pavilion': dict(title='Oculus pavilion', yaw=.48, pitch=.36, distance=1.27,
        question='Can a compact architectural model read through arches, a dome and deliberately cut negative space?',
        demo='Assemble the walls and tower, carve passages, then open an architectural section.'),
    'cutaway-manifold': dict(title='Cutaway manifold', yaw=.58, pitch=.30, distance=1.45,
        question='Can local CSG groups preserve internal structure while the housing is cut away?',
        demo='Cast the housing, bore the connected channels and reveal the internal mechanism.'),
}


def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path, data): Path(path).write_text(json.dumps(data, indent=2, allow_nan=False)+'\n')


def leaves(node):
    if 'children' not in node: return [node['name']]
    return [name for child in node['children'] for name in leaves(child)]


class Author:
    def __init__(self): self.history=[]
    def phase(self, nodes, label):
        names=[name for node in nodes for name in leaves(node)]
        self.history.append(dict(nodes=names,label=label,seconds=1.1))


def spiral_shell(section):
    a=Author(); outer=[]; inner=[]; count=128; points=[]; radii=[]
    for i in range(count+1):
        u=i/count; angle=-.65*math.pi+2.5*math.pi*u
        radius=.055*math.exp(math.log(.26/.055)*u)
        points.append([radius*math.cos(angle),radius*math.sin(angle)+.025,0])
        radii.append(.5*radius+.006)
    for i in range(count):
        outer.append(round_cone(f'outer-{i}',points[i],points[i+1],radii[i],radii[i+1],'shell'))
        inner.append(round_cone(f'inner-{i}',points[i],points[i+1],radii[i]*.73,radii[i+1]*.73,'shell'))
    delta=[b-v for v,b in zip(points[-2],points[-1])]; length=math.sqrt(sum(v*v for v in delta))
    beyond=[v+d/length*.22 for v,d in zip(points[-1],delta)]
    inner.append(round_cone('mouth-bore',points[-1],beyond,radii[-1]*.73,radii[-1]*.73,'shell'))
    # Wide n-ary groups keep source depth independent of sweep sample count.
    for i in range(4):a.phase(outer[i*(count//4):(i+1)*(count//4)],f'Grow coil quarter {i+1}')
    body=union('outer-coil',*outer)
    bore=union('continuous-interior',*inner);a.phase([bore],'Hollow the continuous spiral and open its mouth')
    body=subtract('hollow-shell',body,bore)
    if section:
        cut=box('section-tool',(0,0,.27),(.48,.48,.25),.001,'shell')
        a.phase([cut],'Remove the front cap to expose the interior')
        body=subtract('shell-section',body,cut)
    base=box('display-foot',(0,-.345,-.025),(.19,.025,.105),.012,'foot')
    a.phase([base],'Place the shell on a simple display foot')
    return union('spiral-display',body,base),a.history,[material('shell','D8C8AC',.62),material('foot','657A76',.82)]


def oculus_pavilion(section):
    a=Author()
    base=cylinder('terrace',(0,-.255,0),.335,.028,'stone')
    steps=[box(f'step-{i}',(0,-.252-i*.017,.285+i*.025),(.105,.012,.028),.004,'stone') for i in range(4)]
    a.phase([base,*steps],'Establish the terrace and approach steps')
    walls=cylinder('drum',(0,-.055,0),.218,.155,'stone')
    dome=dict(type='sphere',name='dome',position=[0,.098,0],radius=.218,material='stone')
    a.phase([walls,dome],'Raise the drum and dome')
    inner=union('interior-volume',cylinder('room',(0,-.055,0),.171,.18,'stone'),
                dict(type='sphere',name='inner-dome',position=[0,.098,0],radius=.171,material='stone'))
    a.phase([inner],'Hollow the room and dome')
    body=subtract('hollow-pavilion',union('pavilion-mass',walls,dome),inner)
    doors=[]
    for i,angle in enumerate((0,90,-90)):
        local=union(f'arch-tool-{i}',box(f'portal-bottom-{i}',(0,-.12,.205),(.061,.078,.17),.002,'stone'),
                    cylinder(f'portal-crown-{i}',(0,-.042,.205),.061,.17,'stone',rotation_xyzw=rotation((1,0,0),90)))
        local['rotation_xyzw']=rotation((0,1,0),angle)
        doors.append(local)
    oculus=cylinder('oculus',(0,.30,0),.064,.16,'stone')
    a.phase([*doors,oculus],'Open three arched passages and the roof oculus')
    body=subtract('opened-pavilion',body,union('opening-tools',*doors,oculus))
    if section:
        cut=box('section-tool',(.275,.06,.25),(.245,.41,.22),.001,'stone')
        a.phase([cut],'Open the front-right architectural section')
        body=subtract('pavilion-section',body,cut)
    tower=box('tower',(-.267,.095,-.075),(.042,.305,.058),.006,'tower')
    a.phase([tower],'Raise the separate narrow tower')
    slots=[box('bell-opening',(-.267,.292,-.075),(.025,.058,.11),.014,'tower'),
           cylinder('tower-eye',(-.267,.17,-.075),.021,.11,'tower',rotation_xyzw=rotation((1,0,0),90))]
    a.phase(slots,'Cut the tower openings')
    tower=subtract('open-tower',tower,union('tower-tools',*slots))
    cap=box('tower-cap',(-.267,.402,-.075),(.055,.014,.071),.005,'stone')
    a.phase([cap],'Cap the tower')
    return union('pavilion-display',base,*steps,body,tower,cap),a.history,[material('stone','CAB99B',.82),material('tower','697C77',.75)]


def cutaway_manifold(section):
    a=Author()
    foot=box('foot',(0,-.245,0),(.25,.024,.19),.012,'dark')
    pads=[box(f'pad-{side}',(side*.16,-.205,0),(.038,.025,.12),.009,'housing') for side in (-1,1)]
    a.phase([foot,*pads],'Mount the manifold on two short feet')
    body=box('housing',(0,-.025,0),(.205,.15,.145),.035,'housing')
    neck=cylinder('neck',(0,.205,0),.062,.13,'housing')
    flange=cylinder('upper-flange',(0,.287,0),.103,.019,'housing')
    shoulders=[]
    for side in (-1,1):
        shoulders += [cylinder(f'port-{side}',(side*.251,-.025,0),.070,.07,'housing',rotation_xyzw=rotation((0,0,1),90)),
                      cylinder(f'flange-{side}',(side*.308,-.025,0),.103,.019,'teal',rotation_xyzw=rotation((0,0,1),90))]
    a.phase([body,neck,flange,*shoulders],'Cast the rounded body, neck and opposing flanges')
    bores=[cylinder('cross-bore',(0,-.025,0),.046,.43,'housing',rotation_xyzw=rotation((0,0,1),90)),
           cylinder('shaft-chamber',(0,-.025,0),.099,.13,'housing'),
           cylinder('neck-bore',(0,.235,0),.043,.18,'housing')]
    a.phase(bores,'Bore the connected horizontal and vertical passages')
    housing=subtract('machined-housing',union('casting',body,neck,flange,*shoulders),union('bore-tools',*bores))
    if section:
        cut=box('section-tool',(.245,.04,.19),(.245,.38,.19),.001,'housing')
        a.phase([cut],'Remove a quarter of the housing')
        housing=subtract('housing-section',housing,cut)
    shaft=cylinder('shaft',(0,.06,0),.022,.255,'accent')
    ring=torus('rotor-ring',(0,-.025,0),.071,.012,'teal')
    vanes=[]
    for i in range(6):
        angle=i*math.tau/6
        p=[math.cos(angle)*.078,-.025,math.sin(angle)*.078]
        vanes.append(capsule(f'vane-{i}',[0,-.025,0],p,.012,'teal'))
    a.phase([shaft,ring,*vanes],'Fit the shaft and simple internal rotor')
    top=box('handle',(0,.33,0),(.115,.016,.026),.011,'teal')
    a.phase([top],'Fit the cross handle')
    bolts=[]
    for side in (-1,1):
        for i in range(6):
            angle=i*math.tau/6
            p=[side*.331,-.025+.081*math.cos(angle),.081*math.sin(angle)]
            bolts.append(cylinder(f'bolt-{side}-{i}',p,.010,.010,'accent',rotation_xyzw=rotation((0,0,1),90)))
    a.phase(bolts,'Add the restrained flange fasteners')
    root=union('manifold-display',foot,*pads,housing,shaft,ring,*vanes,top,*bolts)
    return root,a.history,[material('housing','C7C3AC',.64),material('teal','4C9293',.55),material('dark','334447',.76),material('accent','C69555',.48)]


def prepare():
    records=[]
    for subject,author in [('spiral-shell',spiral_shell),('oculus-pavilion',oculus_pavilion),('cutaway-manifold',cutaway_manifold)]:
        for section in (False,True):
            variant='section' if section else 'intact';name=f'{subject}-{variant}'
            root,history,materials=author(section)
            if subject=='oculus-pavilion':root['scale']=.82
            names=leaves(root)
            assert len(names)==len(set(names)) and len(names)<=280
            assert sorted(names)==sorted(n for step in history for n in step['nodes'])
            scene=SCENES/(name+'.json');preset=PRESETS/(name+'.json')
            write_scene(ROOT/scene,SUBJECTS[subject]['title']+' / '+variant,materials,root,history)
            config={'authored-scene':str(scene),'atlas-axis':64,'motion':'static','duration':12,'orbit-turns':1,
                    'yaw':SUBJECTS[subject]['yaw'],'pitch':SUBJECTS[subject]['pitch'],'distance':SUBJECTS[subject]['distance'],
                    'target-y':.015,'sun-elevation':45,'sun-azimuth':150,'shadows':1,'ground':False,'ao':False,
                    'exposure':.6,'supersample':1}
            (ROOT/preset).parent.mkdir(parents=True,exist_ok=True);save(ROOT/preset,config)
            records.append(dict(subject=subject,variant=variant,scene=str(scene),preset=str(preset),leaves=len(names),history_actions=len(history)))
    return records


def capture(output, records):
    output.mkdir(parents=True,exist_ok=False);(output/'inputs').mkdir()
    binary=ROOT/'build/release/projects/sdf/sdf'
    shaders={p.name:sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))}
    receipt=dict(format='cubey.sdf.content-audition',version=1,
        source_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        source_status=subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True),
        binary_sha256=sha(binary),generator_sha256=sha(__file__),shaders=shaders,subjects=SUBJECTS,runs=[],complete=False)
    (output/'author_content_audition.py').write_bytes(Path(__file__).read_bytes())
    for row in records:
        name=f"{row['subject']}-{row['variant']}"
        scene=output/'inputs'/(name+'-scene.json');shutil_source=ROOT/row['scene'];scene.write_bytes(shutil_source.read_bytes())
        config=json.loads((ROOT/row['preset']).read_text());config['authored-scene']=str(scene)
        config_path=output/'inputs'/(name+'-config.json');save(config_path,config)
        for view in (('hero','side') if row['variant']=='section' else ('hero',)):
            stem=name+'-'+view;image=output/(stem+'.png')
            command=[str(binary),'--config',str(config_path),'--headless','--require-validation','--frames','1',
                '--width','960','--height','720','--render-mode','cached','--supersample','1','--output',str(image)]
            if view=='side':command+=['--yaw','1.02','--pitch','.28']
            result=subprocess.run(command,cwd=ROOT,text=True,capture_output=True,timeout=120)
            log=result.stdout+result.stderr;(output/(stem+'.log')).write_text(log)
            if result.returncode or 'VUID-' in log or 'Validation Error' in log:
                raise RuntimeError(stem+': '+log[-3000:])
            assert image.is_file()
            receipt['runs'].append(dict(**row,view=view,command=command,image=str(image),image_sha256=sha(image),
                scene_input=str(scene),scene_sha256=sha(scene),config_input=str(config_path),config_sha256=sha(config_path),
                stats=[line for line in log.splitlines() if line.startswith('sdf:')]))
            save(output/'audition.json',receipt);print(stem,'PASS',row['leaves'],'leaves',flush=True)
    assert sha(binary)==receipt['binary_sha256']
    assert all(sha(binary.parent/'shaders'/name)==value for name,value in shaders.items())
    receipt['complete']=True;receipt['runtime_unchanged']=True;save(output/'audition.json',receipt)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture-dir',type=Path)
    p.add_argument('--subjects',nargs='+',choices=list(SUBJECTS),default=list(SUBJECTS))
    args=p.parse_args();records=prepare()
    print(json.dumps(records),flush=True)
    if args.capture_dir:capture(args.capture_dir.resolve(),[r for r in records if r['subject'] in args.subjects])
