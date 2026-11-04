#!/usr/bin/env python3
"""Observatory layout cleanup: supported circulation and a coherent dome slit."""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess

from author_observatory import (CX, CZ, Observatory, add_annex, add_balcony, add_drum,
                                add_furnishings, add_instruments_and_garden, add_telescope)
from author_diorama_drafts import PALETTE, leaf_names
from sdf_author import material, rotation, subtract, write_scene

ROOT = Path(__file__).resolve().parents[3]
SCENES = Path('projects/sdf/scenes/observatory-cleanup-2026-11')
PRESETS = Path('projects/sdf/presets/observatory-cleanup-2026-11')
VARIANTS = ('round', 'square', 'square-section')
SURVEY_CENTER = (-.27, .07)
STAIR_X, STAIR_START_Z = .17, .228
ROUND_RADIUS = .37
SQUARE_HALF = (.351, .326)


class CleanObservatory(Observatory):
    def beam(self, label, start, end, radius=.005, mat='wood'):
        if label == 'annex-standing-seam':
            # Use the panel's plane and normal, so the seams sit in the metal
            # instead of rising clear of the roof toward the eave.
            side = 1 if start[0]>.165 else -1
            angle = math.atan2(.071, .080)
            offset = .012
            endpoints = []
            for distance in (.006, .075):
                endpoints.append((.165+side*(distance+math.sin(angle)*offset),
                                  .179-distance*.071/.080+math.cos(angle)*offset, start[2]))
            start, end = endpoints
        return super().beam(label, start, end, radius, mat)

    def phase(self, label, *nodes, shell=False):
        if label == 'Calibrate the armillary and terrace survey instrument':
            survey, rest = [], []
            for node in nodes:
                (survey if node['name'].startswith(('tripod-', 'survey-')) else rest).append(node)
            assert len(survey)==6
            # The old tripod center was (.14,.17). Keep its assembly pose;
            # translate it as a group onto the separate observing apron.
            relocated = self.place('survey-on-observing-pad', survey,
                                   (SURVEY_CENTER[0]-.14, 0, SURVEY_CENTER[1]-.17))
            armillary = self.place('armillary-clear-of-survey-instrument', rest, (.05, 0, .024))
            nodes = (armillary, relocated)
        elif label == 'Plant the terrace and cliff ledges':
            edited = []
            for node in nodes:
                if node['name'].startswith(('moss-', 'grass-tuft-')):
                    continue
                if node['name'].startswith('garden-pot-'):
                    old_x = node['children'][0]['position'][0]
                    shift = (-.09, 0, -.21) if old_x<0 else (.25, 0, -.235)
                    node = self.place('plant-clear-of-circulation', [node], shift)
                edited.append(node)
            nodes = tuple(edited)
        super().phase(label, *nodes, shell=shell)


def in_floor(x, z, shape, margin=0):
    if shape == 'round':
        return math.hypot(x, z)<=ROUND_RADIUS-margin
    return abs(x)<=SQUARE_HALF[0]-margin and abs(z)<=SQUARE_HALF[1]-margin


def layout_checks(shape):
    # Check the actual rotated/slightly rounded tread footprint, rather than
    # only each step's center. The whole supported landing is checked too.
    stair_corners = []
    for i in range(10):
        z = STAIR_START_Z-i*.021
        stair_corners.extend((STAIR_X+sx*.048, z+sz*.015) for sx in (-1,1) for sz in (-1,1))
    landing_corners = [(x,z) for x in (.16-.059,.16+.059) for z in (.255-.026,.255+.026)]
    assert all(in_floor(x,z,shape,.010) for x,z in stair_corners+landing_corners)
    feet = [(-.31,.04),(-.23,.04),(-.27,.11)]
    for x,z in feet:
        assert in_floor(x,z,shape,.015)
        assert math.hypot(x-SURVEY_CENTER[0],z-SURVEY_CENTER[1])+.0045<.065
        # A conservative box around the steps plus their widened landing.
        assert x<.10-.025 or x>.235+.025 or z<.025-.025 or z>.281+.025
    minimum = min(.10-(x+.0045) for x,z in feet)
    return dict(base=shape,stair_corners_checked=len(stair_corners),landing_corners_checked=4,
                supported_with_margin=.010,survey_feet_checked=3,
                survey_to_stair_horizontal_clearance=round(minimum,4),
                survey_center=list(SURVEY_CENTER),round_radius=ROUND_RADIUS,square_half=list(SQUARE_HALF))


def add_site(a, shape):
    if shape == 'round':
        foot = a.cy('display-foot', (0,-.340,0), .40, .010, 'dark')
        rock = a.cy('rock-core', (0,-.267,0), .385, .063, 'earth')
        terrain_tools=[]
        for i in range(9):
            t=i*math.tau/9
            r=.357+(i%3)*.008
            terrain_tools.append(a.b('rock-plane', (math.sin(t)*(r+.15),-.26,math.cos(t)*(r+.15)),
                                     (.21,.16,.15),'earth',.002,
                                     rotation_xyzw=rotation((0,1,0),math.degrees(t))))
        floor = a.cy('lower-terrace',(0,-.207,0),ROUND_RADIUS,.016,'stone')
        coping = [a.tor('lower-coping',(0,-.191,0),ROUND_RADIUS-.003,.006,'light-stone')]
    else:
        foot = a.b('display-foot',(0,-.340,0),(.375,.010,.35),'dark',.018)
        rock = a.b('rock-core',(0,-.267,0),(.355,.063,.330),'earth',.012)
        terrain_tools=[]
        for x,z,angle in ((-.355,.16,-18),(.355,-.13,15),(-.12,.334,13),(.20,-.334,-12)):
            terrain_tools.append(a.b('rock-fracture',(x,-.268,z),(.005,.075,.038),'earth',.001,
                                     rotation_xyzw=rotation((0,1,0),angle)))
        floor = a.b('lower-terrace',(0,-.210,0),(SQUARE_HALF[0],.016,SQUARE_HALF[1]),'stone',.003)
        coping=[a.b('terrace-border',(0,-.190,z),(.348,.005,.006),'light-stone',.002) for z in (-.328,.328)]
        coping += [a.b('terrace-border',(x,-.190,0),(.006,.005,.321),'light-stone',.002) for x in (-.353,.353)]
    a.phase('Enlarge the supported diorama footprint',foot,a.cut('shaped-rock-foundation',rock,*terrain_tools),floor,*coping)
    a.phase('Retain the upper building terrace',
            a.cy('upper-terrace',(-.045,-.138,-.07),.215,.048,'stone'),
            a.cy('upper-coping',(-.045,-.087,-.07),.221,.006,'light-stone'))
    blocks=[]
    for row in range(2):
        for i in range(22):
            angle=i*360/22+row*8
            if 58<angle<115:
                continue
            t=math.radians(angle)
            blocks.append(a.b('retaining-block',(-.045+math.sin(t)*.213,-.160+row*.039,-.07+math.cos(t)*.213),
                              (.028,.017,.012),'light-stone' if (i+row)%4 else 'stone',.002,
                              rotation_xyzw=rotation((0,1,0),angle)))
    a.phase('Face the upper retaining wall with offset courses',*blocks)
    path=[]
    for i in range(11):
        t=i/10
        x=-.205+.365*t
        z=.255+.035*math.sin(math.pi*t)
        path.append(a.b('approach-slab',(x,-.192,z),(.017,.004,.018),'light-stone',.001,
                        rotation_xyzw=rotation((0,1,0),-15+30*t)))
    stairs=[]
    for i in range(10):
        top=-.187+i*.0115
        z=STAIR_START_Z-i*.021
        stairs.extend([a.b('stair-mass',(STAIR_X,(-.208+top)/2,z),(.044,(top+.208)/2,.013),'stone'),
                       a.b('stair-tread',(STAIR_X,top,z),(.047,.004,.014),'light-stone',.001)])
    for i in range(5):
        z=STAIR_START_Z-i*.047
        y=-.187+i*.02575
        stairs.extend([a.beam('stair-post',(.222,y,z),(.222,y+.062,z),.004,'dark'),
                       a.sp('rail-finial',(.222,y+.064,z),.005,'brass')])
    stairs.extend([a.beam('stair-handrail',(.222,-.122,STAIR_START_Z),(.222,-.020,STAIR_START_Z-.188),.005,'dark'),
                   a.b('bottom-landing',(.16,-.193,.255),(.058,.005,.025),'light-stone',.001),
                   a.b('top-landing-support',(.17,-.145,.018),(.058,.057,.029),'stone'),
                   a.b('top-landing',(.17,-.082,.018),(.057,.008,.027),'light-stone')])
    a.phase('Keep the approach and both stair landings inside the terrace',*path,*stairs)
    a.phase('Give the survey telescope its own observing pad',
            a.cy('survey-pad',(SURVEY_CENTER[0],-.192,SURVEY_CENTER[1]),.065,.004,'light-stone'))
    a.phase('Support the keeper annex and connecting gallery',
            a.b('annex-foundation',(.165,-.139,-.144),(.075,.053,.087),'stone'),
            a.b('annex-foot-course',(.165,-.085,-.144),(.078,.008,.089),'light-stone'),
            a.b('gallery-floor',(.075,-.077,-.126),(.036,.009,.040),'light-stone'))


def meridian_rotation(angle):
    # Y(angle) * X(90 degrees), rotating the torus into a vertical great circle.
    qx=rotation((1,0,0),90); qy=rotation((0,1,0),angle)
    x1,y1,z1,w1=qy; x2,y2,z2,w2=qx
    return [w1*x2+x1*w2+y1*z2-z1*y2,w1*y2-x1*z2+y1*w2+z1*x2,
            w1*z2+x1*y2-y1*x2+z1*w2,w1*w2-x1*x2-y1*y2-z1*z2]


def add_dome(a):
    shell=a.cut('hollow-observing-dome',a.sp('copper-dome',(CX,.160,CZ),.144,'teal'),
                a.sp('dome-interior',(CX,.160,CZ),.130,'teal'),
                a.b('hemisphere-trim',(CX,-.10,CZ),(.22,.26,.22),'teal',0),
                a.b('observing-slit',(CX,.272,CZ+.090),(.036,.122,.125),'teal',.002))
    seams=[a.tor('standing-meridian',(CX,.160,CZ),.145,.0031,'patina-dark',
                 rotation_xyzw=meridian_rotation(i*30)) for i in range(6)]
    seams=a.cut('seams-seated-under-aperture-trim',a.u('meridian-seams',*seams),
                a.b('seam-bottom-trim',(CX,-.10,CZ),(.24,.26,.24),'patina-dark',0),
                a.b('seam-opening-clearance',(CX,.272,CZ+.090),(.039,.122,.129),'patina-dark',.001))
    parts=[shell,seams,a.tor('dome-turntable',(CX,.162,CZ),.146,.006,'brass'),
           a.tor('turntable-shadow-gap',(CX,.151,CZ),.144,.004,'dark')]
    # Exact small-circle arcs lie on the dome. Side guides now reach the rear
    # of the opening; the old hand-placed lines stopped at its crown.
    radius=.146
    rim_x=.041
    side_radius=math.sqrt(radius*radius-rim_x*rim_x)
    rear_z=-.040
    for side in (-1,1):
        rim=a.tor('curved-slit-guide',(CX+side*rim_x,.160,CZ),side_radius,.003,'brass',
                  rotation_xyzw=rotation((0,0,1),90))
        rim=a.cut('trimmed-slit-guide',rim,
                  a.b('guide-below-base',(CX,-.133,CZ),(.30,.300,.30),'brass',0),
                  a.b('guide-behind-opening',(CX,.20,CZ-.24),(.30,.40,.20),'brass',0))
        parts.append(rim)
    header_radius=math.sqrt(radius*radius-rear_z*rear_z)
    header=a.tor('curved-aperture-header',(CX,.160,CZ+rear_z),header_radius,.003,'brass',
                 rotation_xyzw=rotation((1,0,0),90))
    header=a.cut('trimmed-aperture-header',header,
                 a.b('header-left-trim',(CX-.241,.20,CZ),(.20,.40,.30),'brass',0),
                 a.b('header-right-trim',(CX+.241,.20,CZ),(.20,.40,.30),'brass',0),
                 a.b('header-bottom-trim',(CX,-.14,CZ),(.40,.30,.30),'brass',0))
    parts.append(header)
    for i in range(24):
        parts.append(a.radial('ring-gear-tooth',[a.b('tooth',(0,.154,.149),(.003,.004,.004),'patina-dark',.001)],i*15))
        if i%2==0:
            parts.append(a.radial('turntable-bracket',[
                a.b('bracket',(0,.137,.133),(.005,.012,.007),'patina-dark'),
                a.sp('bracket-stud',(0,.139,.142),.0032,'brass')],i*15))
    for latitude in (25,55):
        v=math.radians(latitude)
        for i in range(16):
            t=i*math.tau/16
            x=CX+radius*math.cos(v)*math.sin(t);y=.160+radius*math.sin(v);z=CZ+radius*math.cos(v)*math.cos(t)
            if abs(x-CX)<.058 and z>CZ-.058:
                continue
            parts.append(a.sp('dome-rivet',(x,y,z),.0025,'brass'))
    a.phase('Form a coherent slit, curved guides and inset dome detailing',*parts,shell=True)


def build(variant):
    shape='round' if variant=='round' else 'square'
    check=layout_checks(shape)
    a=CleanObservatory()
    add_site(a,shape)
    add_drum(a)
    add_dome(a)
    add_telescope(a,3)
    add_annex(a)
    add_balcony(a)
    add_instruments_and_garden(a,3)
    add_furnishings(a,3)
    if variant=='square-section':
        indices=set(a.shell_indices)
        shell=a.u('sectionable-architecture',*[n for i,n in enumerate(a.parts) if i in indices])
        tool=a.b('architectural-section-tool',(0,.12,.20),(.34,.26,.30),'plaster',.002)
        clipped=subtract(a.name('sectioned-architecture'),shell,tool)
        root=a.u('clean-observatory-section',clipped,*[n for i,n in enumerate(a.parts) if i not in indices],scale=.68)
        a.history.append(dict(nodes=[tool['name']],label='Section the architecture and retain its contents',seconds=1.3))
    else:
        root=a.u('clean-observatory',*a.parts,scale=.68)
    names=leaf_names(root)
    assert len(names)==len(set(names)) and len(names)<=900
    assert sorted(names)==sorted(n for step in a.history for n in step['nodes'])
    palette=[copy.deepcopy(m) for m in PALETTE if m['name']!='water']
    palette.append(material('patina-dark','335E59',.78))
    return root,a.history,palette,check


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,value):Path(path).write_text(json.dumps(value,indent=2,allow_nan=False)+'\n')


def prepare():
    rows=[]
    for variant in VARIANTS:
        root,history,palette,check=build(variant)
        scene=SCENES/(variant+'.json');preset=PRESETS/(variant+'.json')
        write_scene(ROOT/scene,'Observatory layout cleanup / '+variant,palette,root,history)
        config={'authored-scene':str(scene),'atlas-axis':64,'motion':'static','yaw':.53,'pitch':.43,
                'distance':1.34,'target-y':-.015,'sun-elevation':43,'sun-azimuth':135,'shadows':1,
                'ao':True,'ground':False,'exposure':.4,'supersample':2}
        (ROOT/preset).parent.mkdir(parents=True,exist_ok=True);save(ROOT/preset,config)
        rows.append(dict(variant=variant,scene=str(scene),preset=str(preset),leaves=len(leaf_names(root)),
                         history_actions=len(history),layout_check=check))
    return rows


def capture(output,rows,hero_only):
    output.mkdir(parents=True,exist_ok=False);(output/'inputs').mkdir()
    binary=ROOT/'build/release/projects/sdf/sdf'
    shaders={p.name:sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))}
    receipt=dict(format='cubey.sdf.observatory-cleanup',version=1,
                 source_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                 binary_sha256=sha(binary),shaders=shaders,generator_sha256=sha(__file__),runs=[],complete=False)
    for name in ('author_observatory_cleanup.py','author_observatory.py','author_diorama_drafts.py','sdf_author.py'):
        shutil.copy2(Path(__file__).with_name(name),output/name)
    save(output/'captures.json',receipt)
    selected_rows=list(rows)
    if not hero_only and any(r['variant']=='square' for r in rows):
        selected_rows.insert(0,dict(variant='before',scene='projects/sdf/scenes/observatory-2026-11/furnished.json',
                                   preset='projects/sdf/presets/observatory-2026-11/furnished.json',leaves=569,
                                   history_actions=26,layout_check=None))
    for row in selected_rows:
        variant=row['variant'];scene=output/'inputs'/(variant+'-scene.json');shutil.copy2(ROOT/row['scene'],scene)
        config=json.loads((ROOT/row['preset']).read_text());config['authored-scene']=str(scene)
        config['distance']=1.34
        config_path=output/'inputs'/(variant+'-config.json');save(config_path,config)
        views={'hero':[]}
        if not hero_only:
            if variant=='square':
                views.update(alternate=['--yaw','-.62','--pitch','.48'],
                             plan=['--yaw','.15','--pitch','1.10','--distance','1.35','--target-y','-.06'],
                             slit=['--yaw','0','--pitch','.35','--distance','.72','--target-y','.12'],
                             slit_rear=['--yaw','3.14','--pitch','.75','--distance','.82','--target-y','.10'])
            if variant=='square-section':
                views['interior']=['--yaw','.14','--pitch','.30','--distance','.78','--target-y','.035']
        for view,options in views.items():
            image=output/(variant+'-'+view+'.png')
            command=[str(binary),'--config',str(config_path),'--headless','--require-validation','--frames','1',
                     '--width','1600','--height','1200','--render-mode','balanced','--supersample','2','--output',str(image),*options]
            result=subprocess.run(command,cwd=ROOT,text=True,capture_output=True,timeout=240)
            log=result.stdout+result.stderr;image.with_suffix('.log').write_text(log)
            if result.returncode or 'VUID-' in log or 'Validation Error' in log:
                receipt['failure']=dict(variant=variant,view=view,returncode=result.returncode,log=str(image.with_suffix('.log')))
                save(output/'captures.json',receipt);raise RuntimeError(log[-3500:])
            assert image.is_file()
            receipt['runs'].append(dict(**row,view=view,command=command,image=str(image),image_sha256=sha(image),
                                        scene_input=str(scene),scene_sha256=sha(scene),config_input=str(config_path),config_sha256=sha(config_path),
                                        stats=[s for s in log.splitlines() if s.startswith('sdf:')]))
            save(output/'captures.json',receipt);print(variant,view,'PASS',row['leaves'],'leaves',flush=True)
    assert sha(binary)==receipt['binary_sha256'] and all(sha(binary.parent/'shaders'/n)==h for n,h in shaders.items())
    receipt.update(complete=True,runtime_unchanged=True);save(output/'captures.json',receipt)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture-dir',type=Path)
    p.add_argument('--variants',choices=list(VARIANTS),nargs='+',default=list(VARIANTS));p.add_argument('--hero-only',action='store_true')
    args=p.parse_args();rows=prepare();print(json.dumps(rows),flush=True)
    if args.capture_dir:capture(args.capture_dir.resolve(),[r for r in rows if r['variant'] in args.variants],args.hero_only)
