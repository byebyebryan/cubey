#!/usr/bin/env python3
"""Second-pass sculptures, leaving every first-pass scene and preset intact."""
import argparse
import json
from pathlib import Path

from author_sculptures import PALETTES, preset
from refinement_author import Refinement, verify
from sdf_author import box, capsule, material, rotation, round_cone, sphere, write_scene


def guardian(variant=0,stage='final'):
    s=Refinement()
    masses=[s.leaf(box('bust-base',(0,-.275,-.025),(.14,.035,.095),.025),'Establish the cropped bust'),
        s.leaf(capsule('neck',(0,-.24,-.03),(0,-.08,-.025),.088),'Raise the neck'),
        s.leaf(sphere('cranium',(0,.11,-.035),.169),'Block the skull'),
        s.leaf(box('jaw',(0,-.065,.045),(.082,.052,.065),.042),'Blend the jaw into the head'),
        s.leaf(capsule('shoulders',(-.14,-.217,-.035),(.14,-.217,-.035),.065),'Join the shoulders'),
        s.leaf(sphere('chin',(0,-.099,.073),.057),'Strengthen the chin')]
    for side in (-1,1):
        tag='left' if side<0 else 'right'
        masses.extend([
            s.leaf(sphere(tag+'-cheek',(side*.092,.005,.052),.091),'Raise the cheek mass'),
            s.leaf(capsule(tag+'-brow',(side*.025,.111,.101),(side*.128,.126,.065),.035),'Sweep the heavy brow'),
            s.leaf(round_cone(tag+'-ear',(side*.13,.074,-.017),(side*.211,.111,-.04),.043,.012),'Pull a pointed ear'),
            s.curve(tag+'-horn',[(side*.124,.20,-.035),(side*.203,.254,-.05),
                (side*.238,.322,-.077),(side*.223,.375-(.012 if side<0 else 0),-.115)],
                [.05,.034,.014,.001],'Grow the swept horn',count=6),
            s.leaf(capsule(tag+'-lower-socket-rim',(side*.047,.034,.142),
                (side*.107,.037,.119),.013),'Resolve the lower socket rim'),
            s.leaf(capsule(tag+'-cheek-ridge',(side*.07,.023,.121),
                (side*.135,.043,.082),.022 if variant==0 else .029),'Resolve the cheekbone'),
            s.leaf(capsule(tag+'-brow-fold',(side*.019,.117,.12),(side*.04,.174,.112),.011),
                'Raise the central brow fold'),
            s.leaf(capsule(tag+'-neck-tendon',(side*.035,-.112,.012),(side*.09,-.235,.02),.027),
                'Carry the jaw into the shoulders')])
    masses.extend([
        s.leaf(capsule('nose-bridge',(0,.103,.11),(0,.005,.173),.032),'Pull the nose bridge forward'),
        s.leaf(sphere('nose-tip',(0,-.003,.174),.045),'Round the nose tip'),
        s.leaf(capsule('muzzle',(-.055,-.045,.123),(.055,-.045,.123),.035),'Blend the muzzle'),
        s.leaf(capsule('lower-lip',(-.05,-.079,.14),(.05,-.079,.14),.014),'Shape the lower lip')])
    for side in (-1,1):
        masses.append(s.leaf(sphere(f'nose-wing-{side}',(side*.031,-.012,.167),.023),'Resolve a nose wing'))
    body=s.blend('resolved-bust',masses,.03)
    tools=[]
    for side in (-1,1):
        tag='left' if side<0 else 'right'
        tools.append(s.leaf(capsule(tag+'-eye-socket',(side*.052,.065,.145),
                          (side*.112,.071,.12),.036),'Carve the eye socket',1.2))
    body=s.cuts('open-sockets',body,tools,.009)
    tools=[s.groove('mouth',body,[(-.067,-.074,0),(-.028,-.061,0),(.028,-.061,0),(.067,-.073,0)],
                    [.004,.009,.009,.004],'Carve the mouth and corners')]
    for side in (-1,1):
        tools.append(s.leaf(sphere(f'nostril-{side}',(side*.025,-.018,.207),.013),'Open a nostril',.8))
        tools.append(s.groove(f'cheek-fold-{side}',body,[(side*.145,.026,0),(side*.12,-.012,0),(side*.081,-.039,0)],
                    [.0015,.0055,.0015],'Carve the cheek-to-muzzle fold'))
    tools.append(s.groove('brow-crease',body,[(0,.182,0),(0,.151,0),(0,.13,0)],
                    [.0015,.0045,.002],'Cut the crease between raised brow folds'))
    body=s.cuts('facial-carving',body,tools,.0025)
    if stage=='form':return s,body
    tools=[]
    for side in (-1,1):
        tag='left' if side<0 else 'right'
        tools.append(s.leaf(round_cone(tag+'-inner-ear',(side*.151,.088,.018),
                                      (side*.19,.103,-.012),.017,.005),'Carve the inner ear'))
        tools.append(s.groove(tag+'-horn-flute',body,[(side*.164,.231,0),(side*.213,.28,0),(side*.232,.33,0)],
                    [.001,.0045,.0015],'Flute the horn root along its sweep'))
        tools.append(s.groove(tag+'-rear-neck',body,[(side*.05,-.115,0),(side*.075,-.178,0),(side*.12,-.23,0)],
                    [.002,.006,.002],'Carve the back of the shoulder',side=-1))
    body=s.cuts('structural-carving',body,tools,.002)
    wear=[s.leaf(box('base-chip',(-.139,-.275,.083),(.022,.016,.022),.001,
                rotation_xyzw=rotation((0,0,1),28)),'Chip an exposed base corner'),
          s.leaf(box('jaw-chip',(.099,-.102,.105),(.015,.01,.018),.001,
                rotation_xyzw=rotation((0,0,1),-23)),'Chip the jaw edge')]
    wear.append(s.groove('temple-crack',body,[(-.137,.148,0),(-.149,.12,0),(-.142,.105,0)],
                         [.001,.003,.001],'Cut a short weathered temple crack'))
    body=s.cuts('stone-wear',body,wear,.0008)
    return s,body


def driftwood(variant=0,stage='final'):
    s=Refinement()
    lean=.035 if variant else 0
    trunk=[(-.14,-.27,.005),(-.08,-.12,.025),(.012,.035,0),(.06+lean,.20,-.075),(.005+lean,.32,-.09)]
    masses=[s.curve('trunk',trunk,[.103,.096,.089,.074,.058],'Pull the twisted trunk',count=4),
        s.curve('left-limb',[(-.025,-.01,.005),(-.16,.075,.03),(-.255,.16,.075)],
                [.075,.059,.039],'Sweep the broad broken limb',count=4),
        s.curve('rear-limb',[(.005,.02,-.005),(.13,.11,-.115),(.21,.205,-.17)],
                [.067,.044,.027],'Turn the smaller limb into depth',count=4),
        s.curve('root',[(-.10,-.20,0),(-.22,-.275,.06),(-.30,-.265,.13)],
                [.078,.048,.025],'Twist the lower root forward',count=3)]
    body=s.blend('joined-root-fragment',masses,.028)
    trims=[s.leaf(box('upper-break',(.005+lean,.386,-.09),(.12,.09,.14),.001,
                      rotation_xyzw=rotation((0,0,1),-22)),'Break the upper end obliquely',1.2),
           s.leaf(box('left-break',(-.31,.20,.09),(.09,.11,.12),.001,
                      rotation_xyzw=rotation((0,0,1),-34)),'Break the side limb across its grain',1.2),
           s.leaf(box('rear-break',(.235,.272,-.185),(.09,.06,.09),.001,
                      rotation_xyzw=rotation((0,0,1),32)),'Break the rear limb at an unequal angle',1.1)]
    body=s.cuts('broken-ends',body,trims,.001)
    # Broad offset scoops flatten the tube locally and leave raised flowing shoulders.
    planes=[s.leaf(capsule('trunk-scoop',(-.20,-.27,.15),(-.045,-.06,.15),.066),'Wear a broad trunk face'),
            s.leaf(capsule('back-scoop',(.015,-.22,-.14),(.025,-.075,-.13),.045),'Weather a rear trunk plane')]
    body=s.cuts('weathered-planes',body,planes,.01)
    if stage=='form':return s,body
    hollows=[s.leaf(round_cone('upper-hollow',(.025+lean,.37,-.09),(.052+lean,.15,-.065),.038,.014),
                   'Hollow the broken upper end',1.5),
             s.leaf(round_cone('lower-hollow',(-.195,-.40,.018),(-.08,-.15,.045),.051,.021),
                   'Open the lower broken end',1.3),
             s.curve('knot-cavity',[(-.012,-.028,.104),(.02,.034,.104),(.026,.09,.07)],
                     [.017,.038,.01],'Carve an elongated irregular knot',count=2)]
    body=s.cuts('wood-hollows',body,hollows,.004)
    # A real split reaches the exposed rim and the hollow, rather than a drawn line.
    split=s.curve('end-split',[(.052+lean,.36,-.045),(.081+lean,.255,-.019),(.055+lean,.15,.017)],
                  [.012,.014,.0015],'Split the broken end along the grain',count=3)
    body=s.cuts('open-end-split',body,[split],.0015)
    if stage=='structure':return s,body
    channels=[]
    for name,points,widths in [
        ('left-trunk',[(-.19,-.27,0),(-.126,-.14,0),(-.057,-.025,0)],[.004,.014,.003]),
        ('center-trunk',[(-.125,-.29,0),(-.064,-.17,0),(-.019,-.075,0)],[.003,.011,.002]),
        ('knot-left',[(-.055,-.065,0),(-.075,.028,0),(-.13,.08,0)],[.002,.009,.002]),
        ('knot-right',[(.025,-.065,0),(.073,.02,0),(.093,.125,0)],[.002,.01,.002]),
        ('left-limb-grain',[(-.105,.043,0),(-.17,.092,0),(-.224,.127,0)],[.004,.009,.002]),
        ('upper-flow',[(.008,.13,0),(-.01+lean,.21,0),(-.027+lean,.275,0)],[.002,.007,.002]),
    ]:
        channels.append(s.groove(name,body,points,widths,'Carve a broad flowing grain channel',count=2))
    channels.append(s.groove('rear-grain',body,[(-.12,-.24,0),(-.055,-.095,0),(.018,.025,0)],
                      [.002,.009,.002],'Continue the grain around the rear',side=-1,count=3))
    channels.append(s.groove('side-grain',body,[(0,-.18,.035),(0,-.055,.065),(0,.025,.04)],
                      [.002,.008,.002],'Turn grain across the side',axis=0,side=-1,count=2))
    body=s.cuts('flowing-grain',body,channels,.002)
    fine=[]
    for name,pts in [
        ('fine-root',[(-.17,-.255,0),(-.11,-.13,0),(-.068,-.07,0)]),
        ('fine-limb',[(-.095,.056,0),(-.145,.104,0),(-.202,.142,0)]),
    ]:
        fine.append(s.groove(name,body,pts,[.001,.0035,.001],'Pick out a tapered fiber',count=2))
    # Unequal notches and a carved strip leave stout splinters around the broken rim.
    fine.extend([
        s.leaf(round_cone('rim-notch-a',(-.016+lean,.35,-.063),(.003+lean,.278,-.046),.014,.002),
               'Notch the broken rim into a stout splinter'),
        s.leaf(round_cone('rim-notch-b',(.058+lean,.35,-.12),(.072+lean,.277,-.108),.011,.002),
               'Vary the opposite broken rim'),
        s.leaf(capsule('limb-end-split',(-.26,.19,.08),(-.183,.105,.067),.008),
               'Split the smaller broken branch')])
    return s,s.cuts('fiber-and-break-detail',body,fine,.001)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--subject',choices=('guardian','driftwood'),action='append')
    parser.add_argument('--stage',choices=('form','structure','final'),default='final')
    parser.add_argument('--variant',type=int,choices=(0,1),default=0)
    parser.add_argument('--audition',action='store_true')
    args=parser.parse_args();root=Path(__file__).resolve().parents[3]
    for subject in args.subject or ('guardian','driftwood'):
        s,tree=globals()[subject](args.variant,args.stage);n=verify(s,tree)
        suffix=(f'refinement-auditions/{subject}-{args.stage}-{"ab"[args.variant]}' if args.audition
                else f'sculpture-{subject}-refined')
        scene=Path(f'projects/sdf/scenes/{suffix}.json')
        color,roughness=PALETTES[subject]
        write_scene(root/scene,f'{subject.capitalize()} / refined',
                    [material('body',color,roughness)],tree,s.history)
        for build in ((False,) if args.audition else (False,True)):
            config=preset(scene,subject,build)
            path=root/f'projects/sdf/presets/{suffix}{"-build" if build else ""}.json'
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text(json.dumps(config,indent=2)+'\n')
        print(subject,args.stage,args.variant,n,'leaves')


if __name__=='__main__':main()
