#!/usr/bin/env python3
"""Organic guardian sculpt; retain the earlier 106-leaf guardian unchanged."""
import argparse
import json
import math
from pathlib import Path

from author_sculptures import PALETTES, preset
from refinement_author import Refinement, leaf_names, samples
from sdf_author import box, capsule, material, round_cone, sphere, union, write_scene
from sdf_probe import distance


def guardian(detail=True):
    s=Refinement();chapters=[]
    def chapter(label):chapters.append(dict(label=label,index=len(s.history)))
    chapter('Sculpt the cranium, mandible and nape')
    masses=[
        s.leaf(box('plinth',(0,-.275,-.025),(.14,.035,.095),.025),'Establish the stone plinth'),
        s.leaf(round_cone('neck',(0,-.23,-.045),(.008,-.07,-.052),.093,.073),'Taper the neck into the skull'),
        s.leaf(round_cone('cranial-vault',(-.005,.025,-.026),(.006,.16,-.035),.143,.116),'Pull the elongated cranial vault'),
        s.leaf(capsule('occiput',(-.012,.125,-.106),(.009,.008,-.099),.104),'Shape the occiput into the nape'),
        s.leaf(capsule('frontal-vault',(-.074,.155,-.013),(.067,.165,-.009),.086),'Broaden the asymmetric forehead'),
        s.leaf(capsule('shoulders',(-.133,-.222,-.037),(.135,-.218,-.037),.06),'Join the shoulder mantle'),
        s.curve('mandible',[(-.116,-.014,.028),(-.088,-.081,.072),(-.004,-.119,.113),
                           (.086,-.079,.074),(.117,-.005,.027)],
                [.047,.043,.038,.044,.049],'Sweep the curved tapered mandible',count=7),
        s.leaf(capsule('rounded-chin',(-.014,-.108,.116),(.017,-.112,.115),.039),'Round the chin under the lower lip'),
    ]
    horn_paths={}
    for side in (-1,1):
        tag='left' if side<0 else 'right'
        horn=[(side*.121,.202,-.038),(side*.2,.253,-.053),
              (side*.236,.321,-.077),(side*.221,.373-(.012 if side<0 else 0),-.114)]
        widths=[.049,.034,.014,.0015];horn_paths[tag]=(horn,widths)
        masses.extend([
            s.leaf(capsule(tag+'-zygomatic',(side*.096,.029,.049),(side*.111,-.015,.062),.075),'Carry the cheekbone into the jaw'),
            s.leaf(round_cone(tag+'-brow',(side*.022,.113,.104),(side*.125,.132,.069),.031,.025),'Compress the brow into the forehead'),
            s.leaf(round_cone(tag+'-ear',(side*.138,.067,-.012),(side*.209,.105,-.034),.038,.011),'Pull the pointed ear from the temple'),
            s.curve(tag+'-horn',horn,widths,'Grow the swept horn',count=12),
            s.curve(tag+'-neck-tendon',[(side*.052,-.098,-.025),(side*.055,-.16,.009),(side*.11,-.229,.016)],
                    [.025,.021,.027],'Carry the jaw into a curved neck tendon',count=5),
            s.curve(tag+'-lower-socket-rim',[(side*.036,.038,.116),(side*.072,.023,.114),(side*.119,.048,.091)],
                    [.008,.013,.009],'Turn the lower eyelid around the socket',count=5),
        ])
    masses.extend([
        s.leaf(round_cone('midface',(0,.035,.052),(0,-.044,.077),.094,.073),'Connect the malar planes beneath the nose'),
        s.leaf(round_cone('nose-bridge',(0,.105,.11),(0,.005,.175),.024,.03),'Pull a tapered nose bridge'),
        s.leaf(sphere('nose-tip',(0,-.006,.179),.032),'Round the nose tip'),
        s.leaf(capsule('muzzle',(-.041,-.043,.131),(.044,-.044,.132),.032),'Blend the muzzle into the cheeks'),
        s.curve('lower-lip',[(-.052,-.08,.14),(0,-.083,.151),(.051,-.079,.138)],
                [.008,.016,.008],'Turn a curved lower lip over the chin',count=6),
    ])
    for side in (-1,1):
        masses.append(s.leaf(sphere(f'alar-wing-{side}',(side*.024,-.016,.176),.021),'Resolve the nose wing'))
    body=s.blend('organic-primary-forms',masses,.022)
    chapter('Open sockets, nostrils and mouth')
    tools=[]
    for side in (-1,1):
        tag='left' if side<0 else 'right'
        tools.extend([
            s.leaf(capsule(tag+'-socket',(side*.047,.067,.145),(side*.108,.074,.121),.035),'Carve the eye socket',1.0),
            s.leaf(sphere(tag+'-nostril',(side*.023,-.02,.202),.011),'Open the nostril'),
            s.leaf(round_cone(tag+'-inner-ear',(side*.153,.081,.015),(side*.19,.099,-.009),.015,.004),'Carve the ear bowl'),
        ])
    body=s.cuts('feature-openings',body,tools,.005)
    mouth=s.groove('mouth',body,[(-.059,-.075,0),(-.027,-.066,0),(0,-.068,0),(.029,-.065,0),(.058,-.076,0)],
                   [.0015,.007,.008,.007,.0015],'Carve the curved mouth',count=5)
    body=s.cuts('mouth-opening',body,[mouth],.0018)
    if not detail:return s,body,chapters
    return detail_pass(s,body,chapters,horn_paths)


def detail_pass(s,body,chapters,horn_paths):
    form=body
    def chapter(label):chapters.append(dict(label=label,index=len(s.history)))
    def groove(name,points,radii,label,count=4,axis=2,side=1,depth=-.60,reference=None):
        return s.groove(name,body if reference is None else reference,points,radii,label,
                        count=count,axis=axis,side=side,depth=depth,normal_offset=True)
    chapter('Sculpt the brow, cheek folds and muzzle')
    raised=[]
    for side in (-1,1):
        tag='left' if side<0 else 'right'
        for i in range(3):
            x=.018+i*.012
            raised.append(groove(f'{tag}-glabellar-ridge-{i}',
                [(side*x,.12,0),(side*(x+.006),.148,0),(side*(x+.013),.181-i*.008,0)],
                [.003,.008-i*.001,.0018],'Model the compressed brow fold',count=5,depth=.78))
        raised.append(groove(tag+'-malar-ridge',
            [(side*.13,.025,0),(side*.113,-.001,0),(side*.075,-.03,0)],
            [.003,.009,.002],'Carry a cheek ridge into the muzzle',count=6,depth=.78))
        raised.append(groove(tag+'-nasolabial-bank',
            [(side*.038,-.026,0),(side*.055,-.047,0),(side*.064,-.075,0)],
            [.002,.006,.0015],'Raise the bank of the muzzle fold',count=5,depth=.8))
        for i in range(2):
            raised.append(groove(f'{tag}-nuchal-bank-{i}',
                [(side*(.027+i*.026),.07,0),(side*(.044+i*.022),-.012,0),
                 (side*(.028+i*.02),-.13,0)],
                [.003,.009,.002],'Model the nuchal transition into the skull',
                count=8,depth=.86,side=-1))
    body=s.blend('secondary-facial-relief',[body,*raised],.003)
    chapter('Carve expression and clustered facial folds')
    tools=[]
    for side in (-1,1):
        tag='left' if side<0 else 'right'
        tools.append(groove(tag+'-muzzle-crease',
            [(side*.034,-.027,0),(side*.048,-.048,0),(side*.064,-.062,0)],
            [.0008,.0032,.0006],'Carve the muzzle crease',count=6))
        # Unequal, nested fans follow the eye and malar forms, leaving cheek
        # centers quiet. These are designed folds, not random surface damage.
        for i,(begin,end) in enumerate(((.077,.101),(.065,.069),(.048,.033),(.032,.009))):
            tools.append(groove(f'{tag}-orbital-fold-{i}',
                [(side*(.122+i*.003),begin,0),(side*.145,(begin+end)/2-.002,0),
                 (side*(.16+i*.001),end,0)],
                [.0008,.003+(i%2)*.0005,.0006],
                'Fan a tapered fold around the outer socket',count=6))
        tools.append(groove(tag+'-malar-plane',
            [(side*.139,.012,0),(side*.119,-.023,0),(side*.092,-.049,0)],
            [.015,.028,.012],'Sculpt a broad shallow malar plane',count=8,depth=-.93))
        for i in range(3):
            tools.append(groove(f'{tag}-malar-fold-{i}',
                [(side*(.137-i*.008),-.014-i*.014,0),
                 (side*(.113-i*.006),-.037-i*.013,0),
                 (side*(.081-i*.004),-.055-i*.012,0)],
                [.0007,.0035,.0007],'Turn a cheek fold toward the jaw',count=5,depth=-.70))
        for i in range(3):
            x=.025+i*.015
            tools.append(groove(f'{tag}-brow-compression-{i}',
                [(side*x,.123,0),(side*(x+.003),.15,0),(side*(x+.012),.177-i*.009,0)],
                [.0008,.003,.0007],'Separate the compressed brow ridges',count=5))
    for i in range(3):
        y=.185+i*.014
        tools.append(groove(f'forehead-fold-{i}',
            [(-.096+i*.006,y-.006,0),(-.051,y+.008,0),(.006,y+.003,0),
             (.055,y+.01-i*.002,0),(.096-i*.009,y-.005,0)],
            [.0005,.0025,.002,.0023,.0005],'Sweep an asymmetric shallow forehead fold',count=5,depth=-.68))
    body=s.cuts('facial-folds',body,tools,.0012)
    chapter('Resolve the nape and throat')
    tools=[]
    for side in (-1,1):
        tag='left' if side<0 else 'right'
        for i in range(4):
            tools.append(groove(f'{tag}-throat-fold-{i}',
                [(side*.016,-.148-i*.016,0),(side*.044,-.151-i*.017,0),
                 (side*.075,-.143-i*.019,0)],
                [.0007,.003,.0007],'Turn a throat fold around the tendon',count=4,depth=-.66))
        for i in range(4):
            x=.018+i*.018
            tools.append(groove(f'{tag}-nape-fold-{i}',
                [(side*x,.055,0),(side*(x+.009),-.022,0),
                 (side*(x-.003),-.099,0)],
                [.0007,.003,.0007],'Carry a nape fold down from the skull',count=4,side=-1,depth=-.66))
    body=s.cuts('nape-and-throat-relief',body,tools,.001)
    chapter('Carve horn flutes and curved growth relief')
    tools=[]
    for side in (-1,1):
        tag='left' if side<0 else 'right'
        for i in (-1,0,1):
            offset=i*.006
            tools.append(groove(f'{tag}-horn-flute-{i+1}',
                [(side*(.157+offset),.222,0),(side*(.198+offset),.256,0),
                 (side*(.226+offset*.4),.302,0),(side*(.231+offset*.15),.335,0)],
                [.0006,.0025,.0018,.0006],'Flute the horn along its growth',count=5))
        # Curved, partial growth creases wrap the actual sampled horn; their
        # unequal spacing and taper avoid a regular stack of engraved rings.
        points,radii=horn_paths[tag]
        path=samples(points,radii,count=12)
        for i,t in enumerate((.12,.19,.27,.36,.46,.57,.69,.8)):
            f=t*(len(path)-1);j=int(f);u=f-j
            center=[a*(1-u)+b*u for a,b in zip(path[j][0],path[j+1][0])]
            radius=path[j][1]*(1-u)+path[j+1][1]*u
            tangent=[b-a for a,b in zip(path[max(0,j-1)][0],path[min(len(path)-1,j+1)][0])]
            length=math.sqrt(sum(v*v for v in tangent));tangent=[v/length for v in tangent]
            n=[-tangent[0]*tangent[2],-tangent[1]*tangent[2],1-tangent[2]**2]
            length=math.sqrt(sum(v*v for v in n));n=[v/length for v in n]
            b=[tangent[1]*n[2]-tangent[2]*n[1],tangent[2]*n[0]-tangent[0]*n[2],tangent[0]*n[1]-tangent[1]*n[0]]
            width=.0014*(1-t)+.0005
            ring=[]
            for k in range(7):
                angle=-1.05+2.1*k/6
                direction=[a*math.cos(angle)+bb*math.sin(angle) for a,bb in zip(n,b)]
                lo,hi=0,radius+.07
                for _ in range(18):
                    mid=(lo+hi)/2
                    point=[c+d*mid for c,d in zip(center,direction)]
                    if distance(form,point)>0:hi=mid
                    else:lo=mid
                p=[c+((lo+hi)/2+width*.60)*d+tt*width*.4*math.sin(angle*1.5+i*.7)
                   for c,d,tt in zip(center,direction,tangent)]
                ring.append((p,width*(.25+.75*math.sin(math.pi*k/6))))
            tools.append(s.segments(f'{tag}-horn-growth-{i}',ring,'Carve a curved partial horn growth crease',join=0))
    body=s.cuts('horn-surface-relief',body,tools,.0006)
    return s,body,chapters


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage',choices=('form','final'),default='final');a=p.parse_args()
    root=Path(__file__).resolve().parents[3]
    s,tree,chapters=guardian(a.stage=='final')
    names=leaf_names(tree)
    assert len(names)==len(set(names))==len(s.history)<=1024
    assert set(names)=={h['node'] for h in s.history}
    suffix='sculpture-guardian-organic'+('-form' if a.stage=='form' else '')
    scene=Path(f'projects/sdf/scenes/{suffix}.json')
    color,roughness=PALETTES['guardian']
    document=write_scene(root/scene,'Guardian / organic '+a.stage,[material('body',color,roughness)],tree,s.history)
    document['authoring']['chapters']=chapters
    document['authoring']['leaf_reveals']=len(s.history)
    document['authoring']['coherent_strokes']=sum(not h['label'].endswith(' / continue') for h in s.history)
    (root/scene).write_text(json.dumps(document,indent=2)+'\n')
    for build in (False,True):
        config=preset(scene,'guardian',build)
        if build:config.update({'construction-seconds':120,'duration':150})
        (root/f'projects/sdf/presets/{suffix}{"-build" if build else ""}.json').write_text(json.dumps(config,indent=2)+'\n')
    print(a.stage,len(names),'leaves',len(chapters),'chapters',flush=True)


if __name__=='__main__':main()
