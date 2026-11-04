#!/usr/bin/env python3
"""Clay-first SDF studies: gesture, continuous masses and deliberate carving."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from author_diorama_drafts import Draft, leaf_names, signed_leaves
from refinement_author import samples
from sdf_author import box, material, rotation, round_cone, shape, smooth_subtract, subtract, write_scene
from sdf_probe import distance

ROOT=Path(__file__).resolve().parents[3]
SCENES=Path('projects/sdf/scenes/sculptural-drafts-2026-11')
PRESETS=Path('projects/sdf/presets/sculptural-drafts-2026-11')
SUBJECTS={
 'coiled-sleeper':dict(title='Coiled sleeper',scale=.82,yaw=.38,pitch=.46,distance=1.02,target_y=-.015,profile=-1.20,
    idea='A resting creature whose back, folded limbs and long tail settle into one curled gesture.',
    question='Does the pose read through the silhouette and broad volumes before the face is carved?',
    shaping='Closed eyelids and a shallow mouth cut establish a quiet expression.'),
 'forest-spirit':dict(title='Forest spirit',scale=.78,yaw=.28,pitch=.23,distance=1.18,target_y=.015,profile=1.15,
    idea='An uneven face emerges from a leaning trunk, with unequal boughs and flowing roots.',
    question='Can the face and tree feel like one carved mass, with a clear lean and quieter broad surfaces?',
    shaping='Eye sockets, a mouth, a broken crown hollow and a low trunk cavity turn the mass into a spirit.'),
 'eroded-refuge':dict(title='Eroded refuge',scale=.83,yaw=.40,pitch=.28,distance=1.16,target_y=-.01,profile=1.15,
    idea='A small refuge hollowed from a continuous, wind-worn outcrop and a flowing approach.',
    question='Can a place feel grown and carved, with unequal supports and a sweeping canopy?',
    shaping='A vaulted chamber, a winding entrance and an upper lightwell connect inside the outcrop.'),
}
CLAY=[material('clay','BCB6A8',.84)]


class Clay(Draft):
    def __init__(self):
        super().__init__()
        self.expected={}

    def record(self,node,label,negative=False):
        names=leaf_names(node)
        assert not any(n in self.expected for n in names)
        self.expected.update({n:negative for n in names})
        self.history.append(dict(nodes=names,label=label,seconds=1.3))
        return node

    def blend(self,label,nodes,radius=.025):
        if len(nodes)==1:return nodes[0]
        return shape('smooth-union',self.name(label),children=nodes,radius=radius,blend_materials=False)

    def curve(self,label,points,radii,count=8,join=0):
        path=samples(points,radii,count=count)
        nodes=[round_cone(self.name(label),p,q,r,s,'clay') for (p,r),(q,s) in zip(path,path[1:])]
        return self.blend(label+'-flow',nodes,join) if join else self.u(label+'-flow',*nodes)

    def ball(self,label,p,r):return self.sp(label,p,r,'clay')

    def carve(self,label,body,tools,radius=.005):
        tool=self.u(label+'-tools',*tools)
        self.record(tool,label,True)
        return smooth_subtract(self.name(label+'-result'),body,tool,radius) if radius else subtract(self.name(label+'-result'),body,tool)

    def flatten(self,body,y):
        tool=box(self.name('contact-plane'),(0,y-.20,0),(.48,.20,.48),0,'clay')
        return self.carve('Flatten the contact surface',body,[tool],0)

    def groove(self,label,body,points,radii,count=2,depth=.20):
        path=[]
        for p,r in samples(points,radii,count=count):
            p=list(p);outside=.49
            for i in range(1,197):
                inside=.49-i*.005;p[2]=inside
                if distance(body,p)<=0:
                    for _ in range(18):
                        middle=(outside+inside)/2;p[2]=middle
                        if distance(body,p)>0:outside=middle
                        else:inside=middle
                    p[2]=(outside+inside)/2-r*depth;path.append((p.copy(),r));break
                outside=inside
            else:raise ValueError(label+': missed sculpt surface at '+str(p))
        return self.u(label,*[round_cone(self.name(label),p,q,r,s,'clay') for (p,r),(q,s) in zip(path,path[1:])])


def sleeper(final):
    a=Clay()
    back=a.record(a.curve('back',[(-.12,.025,-.07),(-.02,.045,-.11),(.13,.025,-.09),(.18,-.03,-.03)],
                         [.10,.14,.14,.12]),'Pull a weighted curve through the back and haunch')
    neck=a.record(a.curve('neck',[(-.10,.06,-.06),(-.16,.015,.04),(-.17,-.02,.12)],
                         [.085,.078,.070]),'Let the neck settle down onto the forepaws')
    head=a.record(a.blend('head',[
        a.ball('head-vault',(-.17,-.020,.105),.088),
        a.curve('muzzle',[(-.17,-.045,.15),(-.195,-.070,.19),(-.21,-.080,.215)],[.057,.042,.034]),
        a.ball('nose',(-.212,-.072,.229),.017)],.018),'Shape the lowered head and tapered muzzle')
    limbs=a.record(a.blend('folded-limbs',[
        a.curve('near-foreleg',[(-.14,-.08,.04),(-.20,-.13,.14),(-.21,-.135,.22)],[.045,.037,.029]),
        a.curve('far-foreleg',[(-.10,-.08,.035),(-.09,-.13,.13),(-.12,-.14,.22)],[.045,.033,.026]),
        a.curve('folded-haunch',[(.16,-.08,.045),(.12,-.13,.15),(.025,-.14,.17)],[.070,.050,.030])],.012),
        'Fold the forelegs and rear leg under the resting weight')
    belly=a.record(a.curve('tucked-belly',[(.12,-.05,.015),(.07,-.08,.115)],[.106,.095]),
                   'Settle the belly into the curled body')
    tail=a.record(a.curve('tail',[(.19,-.035,-.08),(.29,-.06,.045),(.25,-.12,.20),(.07,-.14,.265),(-.06,-.135,.245),(-.10,-.115,.195)],
                         [.090,.075,.060,.047,.028,.008],count=5),'Curl the thick tail around the body and paws')
    ears=a.record(a.blend('ears',[
        a.curve('near-ear',[(-.205,.035,.11),(-.235,.085,.070),(-.24,.120,.045)],[.038,.026,.003]),
        a.curve('far-ear',[(-.13,.045,.09),(-.11,.105,.065),(-.125,.130,.020)],[.034,.022,.003])],.008),
        'Pull two unequal ears back along the resting pose')
    body=a.blend('one-resting-mass',[back,neck,head,limbs,belly,tail,ears],.024)
    body=a.flatten(body,-.172)
    if not final:return a,body
    eyes=[a.groove('near-closed-eye',body,[(-.224,.010,0),(-.209,.003,0),(-.196,.006,0)],[.002,.005,.002]),
          a.groove('far-closed-eye',body,[(-.160,.018,0),(-.145,.012,0),(-.130,.018,0)],[.002,.0045,.002])]
    body=a.carve('Carve the relaxed eyelids',body,eyes,.002)
    mouth=a.groove('quiet-mouth',body,[(-.235,-.090,0),(-.212,-.100,0),(-.190,-.092,0)],[.001,.0025,.001])
    body=a.carve('Set the small resting mouth',body,[mouth],.0015)
    ear=a.curve('ear-bowl',[(-.216,.057,.126),(-.232,.088,.083)],[.014,.005],count=5)
    body=a.carve('Hollow the nearer ear',body,[ear],.003)
    return a,body


def spirit(final):
    a=Clay()
    trunk=a.record(a.curve('trunk',[(-.07,-.245,0),(.02,-.08,-.025),(-.005,.10,-.035),(.065,.23,-.025)],
                          [.105,.115,.140,.090],count=5),'Grow the leaning trunk through the face')
    roots=a.record(a.blend('root-flows',[
        a.curve('left-root',[(-.04,-.18,.01),(-.11,-.26,.10),(-.20,-.275,.21),(-.30,-.270,.20)],[.09,.065,.030,.004]),
        a.curve('right-root',[(.03,-.18,-.02),(.14,-.25,.06),(.22,-.276,.14),(.28,-.272,.11)],[.080,.055,.027,.004]),
        a.curve('rear-root',[(-.03,-.17,-.02),(-.02,-.25,-.13),(.08,-.275,-.22)],[.080,.045,.005])],.018),
        'Spread three unequal roots into the contact mass')
    crown=a.record(a.blend('broken-boughs',[
        a.curve('rising-bough',[(.085,.20,-.03),(.16,.31,-.08),(.10,.39,-.09)],[.055,.035,.009]),
        a.curve('low-bough',[(-.11,.18,-.025),(-.22,.27,-.04),(-.27,.23,-.005)],[.040,.018,.003])],.014),
        'Sweep the unequal upper boughs along the lean')
    face=a.record(a.blend('facial-masses',[
        a.ball('face-vault',(-.025,.13,.04),.120),
        a.curve('left-brow',[(-.10,.17,.085),(-.045,.19,.13),(0,.165,.145)],[.020,.028,.018]),
        a.curve('right-brow',[(.01,.175,.13),(.065,.20,.12),(.106,.17,.08)],[.021,.027,.015]),
        a.curve('nose',[(.005,.17,.125),(.02,.095,.185),(.01,.072,.190)],[.019,.022,.019]),
        a.curve('left-cheek',[(-.09,.06,.075),(-.075,.015,.12)],[.045,.030]),
        a.curve('right-cheek',[(.10,.06,.045),(.075,-.015,.11)],[.052,.040]),
        a.curve('chin',[(-.045,-.005,.09),(.005,-.03,.115),(.055,-.005,.09)],[.030,.034,.024])],.025),
        'Bring the brow, cheeks and nose out of the trunk')
    body=a.blend('continuous-spirit',[trunk,roots,crown,face],.034)
    body=a.flatten(body,-.310)
    if not final:return a,body
    body=a.carve('Open the eyes beneath the uneven brow',body,[
        a.groove('left-eye-cut',body,[(-.092,.125,0),(-.065,.115,0),(-.038,.120,0)],[.012,.021,.012],count=3,depth=.65),
        a.groove('right-eye-cut',body,[(.038,.135,0),(.061,.128,0),(.082,.137,0)],[.012,.021,.010],count=3,depth=.65)],.006)
    mouth=a.groove('mouth',body,[(-.045,-.027,0),(0,-.017,0),(.050,-.029,0)],[.002,.006,.002],count=3)
    body=a.carve('Carve the mouth into the lower facial mass',body,[mouth],.002)
    body=a.carve('Open a broken crown and a low trunk hollow',body,[
        a.curve('crown-hollow',[(.10,.28,.19),(.08,.295,.03),(.13,.33,-.07)],[.065,.054,.036]),
        a.curve('low-hollow',[(.10,-.19,.23),(.07,-.155,.065)],[.043,.036])],.008)
    return a,body


def refuge(final):
    a=Clay()
    cliff=a.record(a.blend('cliff-volumes',[
        a.curve('swept-cliff',[(-.20,-.17,-.01),(-.21,.02,-.04),(-.13,.21,-.06),(.055,.25,-.04),(.20,.16,-.07)],
                [.140,.160,.150,.110,.070],count=5),
        a.curve('right-flank',[(.13,-.19,-.04),(.17,-.03,-.075),(.13,.11,-.08)],[.120,.110,.100]),
        a.ball('rear-mass',(-.035,.02,-.11),.20),
        a.curve('erosion-bank',[(-.24,-.17,.075),(-.22,.06,.05),(-.12,.22,.02)],[.022,.025,.008])],.025),
        'Sweep the cliff up and over an unequal pair of supports')
    ground=a.record(a.blend('outcrop-and-approach',[
        a.curve('outcrop-foot',[(-.25,-.22,.04),(.01,-.235,.01),(.23,-.24,0)],[.075,.085,.060]),
        a.curve('front-shelf',[(-.18,-.24,.18),(.12,-.24,.20)],[.052,.040]),
        a.curve('approach',[(.23,-.247,.28),(.11,-.218,.18),(-.035,-.20,.09)],[.035,.045,.035])],.018),
        'Draw a soft approach into the low natural shelf')
    contact=box(a.name('contact-plane'),(0,-.495,0),(.48,.20,.48),0,'clay')
    a.record(contact,'Flatten the contact surface',True)
    if final:
        tools=[a.curve('vaulted-room',[(-.02,-.045,-.03),(-.09,.12,-.075)],[.130,.085]),
               a.curve('entrance',[(-.065,-.10,.36),(-.05,-.07,.14),(.01,0,-.025)],[.105,.125,.100],count=6),
               a.curve('upper-lightwell',[(-.19,.17,.29),(-.16,.18,.12),(-.11,.22,-.02),(-.10,.20,-.20)],[.026,.042,.028,.020]),
               a.ball('canopy-weathering',(.20,.23,.12),.060),
               a.b('broad-crest-plane',(-.12,.39,.04),(.30,.09,.25),'clay',.012,rotation_xyzw=rotation((0,0,1),20))]
        cliff=a.carve('Connect the chamber, entrance and upper lightwell',cliff,tools,.009)
    body=a.blend('refuge-ground-junction',[cliff,ground],.016)
    body=subtract(a.name('contact-result'),body,contact)
    return a,body


BUILDERS={'coiled-sleeper':sleeper,'forest-spirit':spirit,'eroded-refuge':refuge}


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,value):Path(path).write_text(json.dumps(value,indent=2,allow_nan=False)+'\n')


def prepare():
    records=[]
    for subject,build in BUILDERS.items():
        info=SUBJECTS[subject]
        for state in ('mass','carved'):
            a,root=build(state=='carved');root['scale']=info['scale']
            names=leaf_names(root)
            assert len(names)==len(set(names)) and len(names)<=600
            assert sorted(names)==sorted(n for step in a.history for n in step['nodes'])
            assert dict(signed_leaves(root))==a.expected
            scene=SCENES/(subject+'-'+state+'.json');preset=PRESETS/(subject+'-'+state+'.json')
            write_scene(ROOT/scene,info['title']+' / '+state,CLAY,root,a.history)
            config={'authored-scene':str(scene),'atlas-axis':64,'motion':'static','yaw':info['yaw'],'pitch':info['pitch'],
                    'distance':info['distance'],'target-y':info['target_y'],'sun-elevation':42,'sun-azimuth':140,
                    'shadows':1,'ao':True,'ground':False,'exposure':.45,'supersample':2}
            (ROOT/preset).parent.mkdir(parents=True,exist_ok=True);save(ROOT/preset,config)
            records.append(dict(subject=subject,state=state,scene=str(scene),preset=str(preset),leaves=len(names),history_actions=len(a.history)))
    return records


def capture(output,rows,hero_only=False):
    output.mkdir(parents=True,exist_ok=False);(output/'inputs').mkdir()
    binary=ROOT/'build/release/projects/sdf/sdf';shaders={p.name:sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))}
    receipt=dict(format='cubey.sdf.sculptural-drafts',version=1,source_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                 generator_sha256=sha(__file__),binary_sha256=sha(binary),shaders=shaders,subjects=SUBJECTS,runs=[],complete=False)
    for name in ('author_sculptural_drafts.py','author_diorama_drafts.py','refinement_author.py','author_sculptures.py','sdf_author.py','sdf_probe.py'):
        shutil.copy2(Path(__file__).with_name(name),output/name)
    save(output/'captures.json',receipt)
    for row in rows:
        if hero_only and row['state']=='mass':continue
        stem=row['subject']+'-'+row['state'];scene=output/'inputs'/(stem+'-scene.json');shutil.copy2(ROOT/row['scene'],scene)
        config=json.loads((ROOT/row['preset']).read_text());config['authored-scene']=str(scene)
        config_path=output/'inputs'/(stem+'-config.json');save(config_path,config)
        views={'hero':[]}
        if not hero_only and row['state']=='carved':views['profile']=['--yaw',str(SUBJECTS[row['subject']]['profile']),'--pitch','.24']
        for view,options in views.items():
            image=output/(stem+'-'+view+'.png')
            command=[str(binary),'--config',str(config_path),'--headless','--require-validation','--frames','1','--width','1440','--height','1080',
                     '--render-mode','balanced','--supersample','2','--output',str(image),*options]
            result=subprocess.run(command,cwd=ROOT,text=True,capture_output=True,timeout=240)
            log=result.stdout+result.stderr;image.with_suffix('.log').write_text(log)
            if result.returncode or 'VUID-' in log or 'Validation Error' in log:
                receipt['failure']=dict(subject=row['subject'],state=row['state'],view=view,returncode=result.returncode,log=str(image.with_suffix('.log')))
                save(output/'captures.json',receipt);raise RuntimeError(log[-3500:])
            assert image.is_file()
            receipt['runs'].append(dict(**row,view=view,command=command,image=str(image),image_sha256=sha(image),scene_input=str(scene),scene_sha256=sha(scene),
                                        config_input=str(config_path),config_sha256=sha(config_path),stats=[s for s in log.splitlines() if s.startswith('sdf:')]))
            save(output/'captures.json',receipt);print(stem,view,'PASS',row['leaves'],'leaves',flush=True)
    assert sha(binary)==receipt['binary_sha256'] and all(sha(binary.parent/'shaders'/n)==h for n,h in shaders.items())
    receipt.update(complete=True,runtime_unchanged=True);save(output/'captures.json',receipt)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture-dir',type=Path)
    p.add_argument('--subjects',choices=list(SUBJECTS),nargs='+',default=list(SUBJECTS));p.add_argument('--hero-only',action='store_true')
    args=p.parse_args();rows=prepare();print(json.dumps(rows),flush=True)
    if args.capture_dir:capture(args.capture_dir.resolve(),[r for r in rows if r['subject'] in args.subjects],args.hero_only)
