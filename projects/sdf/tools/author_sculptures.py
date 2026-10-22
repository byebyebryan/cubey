#!/usr/bin/env python3
"""Deterministic native-CSG sculptures, with retained clay blockout auditions."""
import argparse
import json
from pathlib import Path
from sdf_probe import front_surface

from sdf_author import (box, capsule, capped_cone, cylinder, material, rotation,
                        round_cone, shape, smooth_subtract, sphere, subtract, torus, union,
                        write_scene)

SUBJECTS = ('guardian', 'vessel', 'arch', 'driftwood')
TITLES = dict(guardian='Stone guardian', vessel='Pierced ceramic',
              arch='Wind-carved arch', driftwood='Twisted driftwood')
PALETTES = dict(guardian=('A6B5A8', .76), vessel=('3E8D91', .38),
                arch=('C99D74', .84), driftwood=('AA7955', .75))


class Sculpt:
    """Small authoring convenience; all runtime geometry remains explicit JSON."""
    def __init__(self, tapered=False):
        self.history = []
        self.tapered = tapered

    def leaf(self, node, label=None, seconds=.65):
        self.history.append(dict(node=node['name'],
                                 label=label or node['name'].replace('-', ' ').capitalize(),
                                 seconds=seconds))
        return node

    def blend(self, name, nodes, radius=.035):
        if len(nodes) == 1:
            return nodes[0]
        return shape('smooth-union', name, children=nodes, radius=radius,
                     blend_materials=False)

    def stroke(self, name, points, radii, label, blend=.025, curved=True):
        """Retain original capsule auditions; finished pieces use rounded tapers."""
        assert len(points) == len(radii) >= 2
        if self.tapered and curved and len(points)>2:
            # Interpolate the authoring path, retaining explicit bounded leaves.
            sampled, widths = [], []
            for i in range(len(points)-1):
                p0,p1,p2,p3=points[max(0,i-1)],points[i],points[i+1],points[min(len(points)-1,i+2)]
                for j in range(5):
                    t=j/5
                    sampled.append([.5*((2*b)+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+
                                          (-a+3*b-3*c+d)*t*t*t) for a,b,c,d in zip(p0,p1,p2,p3)])
                    widths.append(radii[i]*(1-t)+radii[i+1]*t)
            points,radii=[*sampled,points[-1]],[*widths,radii[-1]]
        segments = [self.leaf(round_cone(f'{name}-{i}', a, b, ra, rb) if self.tapered else
                              capsule(f'{name}-{i}', a, b, (ra+rb)/2),
                              label if i == 0 else f'{label} / continue', .42)
                    for i, (a, b, ra, rb) in enumerate(zip(points, points[1:], radii, radii[1:]))]
        join=0 if self.tapered else blend
        return self.blend(name,segments,join) if join else union(name,*segments)

    def surface_groove(self, name, body, points, radius, label, blend=.003):
        """Place each cutter on the actual front surface, like a projected brush."""
        samples=[]; widths=[]
        radii=radius if isinstance(radius,(tuple,list)) else [radius]*len(points)
        for index,(a,b) in enumerate(zip(points,points[1:])):
            for i in range(3):
                t=i/3
                x,y=[u*(1-t)+v*t for u,v in zip(a,b)]
                r=radii[index]*(1-t)+radii[index+1]*t
                samples.append((x,y,front_surface(body,x,y)-r*.2)); widths.append(r)
        x,y=points[-1]
        samples.append((x,y,front_surface(body,x,y)-radii[-1]*.2)); widths.append(radii[-1])
        cutter=self.stroke(name,samples,widths,label,0,curved=False)
        return self.cut(name+'-carve',body,cutter,blend)

    def cut(self, name, body, cutter, blend=.008):
        return smooth_subtract(name, body, cutter, blend) if blend else subtract(name, body, cutter)


def guardian(variant=0, detail=False):
    s = Sculpt(detail)
    broad = 1 if variant == 0 else .88
    masses = [
        s.leaf(box('bust-base', (0, -.275, -.025), (.14*broad, .035, .095), .025), 'Establish the cropped bust'),
        s.leaf(capsule('neck', (0, -.24, -.03), (0, -.08, -.025), .088), 'Raise the neck'),
        s.leaf(sphere('cranium', (0, .11, -.035), .169), 'Block the skull'),
        s.leaf(box('jaw', (0, -.065, .045), (.082*broad, .052, .065), .042), 'Blend the jaw into the head'),
    ]
    for side in (-1, 1):
        tag = 'left' if side < 0 else 'right'
        masses.append(s.leaf(sphere(f'{tag}-cheek', (side*.092*broad, .005, .052), .091), 'Raise the cheek mass'))
        masses.append(s.leaf(capsule(f'{tag}-brow', (side*.025, .111, .101),
                                   (side*.128*broad, .126, .065), .035), 'Sweep the heavy brow'))
    masses += [s.leaf(capsule('nose-bridge', (0, .103, .11), (0, .005, .173), .032), 'Pull the nose bridge forward'),
               s.leaf(sphere('nose-tip', (0, -.003, .174), .045), 'Round the nose tip'),
               s.leaf(capsule('muzzle', (-.055, -.045, .123), (.055, -.045, .123), .035), 'Blend a broad muzzle')]
    if detail:
        masses += [s.leaf(capsule('shoulder-mantle', (-.14, -.217, -.035), (.14, -.217, -.035), .065),
                          'Blend the shoulders into the cropped base'),
                   s.leaf(sphere('chin', (0, -.099, .073), .057), 'Strengthen the chin')]
    for side in (-1, 1):
        tag = 'left' if side < 0 else 'right'
        points = [(side*.124, .205, -.035), (side*.21, .255, -.05),
                  (side*.247, .335 if variant == 0 else .30, -.075)]
        radii = [.051, .036, .007]
        if detail:
            points = [(side*.124, .20, -.035), (side*.203, .254, -.05),
                      (side*.238, .322, -.077), (side*.223, .375-(.012 if side<0 else 0), -.115)]
            radii = [.05, .034, .014, .001]
            masses.append(s.leaf(round_cone(f'{tag}-ear', (side*.13, .074, -.017),
                                           (side*.211, .111, -.04), .043, .012), 'Pull a small pointed ear'))
        horn = s.stroke(f'{tag}-horn', points, radii,
                       'Grow the swept horn', .025)
        masses.append(horn)
    body = s.blend('fused-bust', masses, .042)
    for side in (-1, 1):
        tag = 'left' if side < 0 else 'right'
        cutter = s.leaf(capsule(f'{tag}-eye-socket', (side*.055, .065, .145),
                                (side*.112, .074, .122), .039), 'Carve the eye socket', 1.2)
        body = s.cut(f'{tag}-socket-carve', body, cutter, .008)
    if detail:
        body=s.surface_groove('mouth-cut',body,[(-.066,-.074),(-.027,-.061),(.027,-.061),(.066,-.074)],
                              .009,'Carve the mouth and expression')
    else:
        mouth=s.leaf(capsule('mouth-cut',(-.067,-.067,.151),(.067,-.067,.151),.013),
                     'Carve the mouth and expression',1.2)
        body=s.cut('mouth-carve',body,mouth,.005)
    for side in (-1, 1):
        cut = s.leaf(sphere(f'nostril-{side}', (side*.023, -.019, .204), .013), 'Open a nostril', .8)
        body = s.cut(f'nostril-carve-{side}', body, cut, .003)
    if detail:
        for side in (-1, 1):
            body=s.surface_groove(f'cheek-flow-{side}',body,[(side*.15,.018),(side*.132,-.017),(side*.105,-.046)],
                                  .0055,'Carve the cheek flow')
        body=s.surface_groove('brow-expression',body,[(-.025,.15),(0,.125),(.025,.15)],
                              .005,'Sharpen the central brow crease')
    return s, body


def vessel(variant=0, detail=False):
    s = Sculpt(detail)
    radius = .19 if variant == 0 else .16
    body = s.blend('vessel-mass', [
        s.leaf(capped_cone('body-profile', (0, -.01, 0), .135, .17, .245), 'Raise the vessel profile'),
        s.leaf(sphere('belly', (-.024 if variant == 0 else -.04, -.095, 0), radius), 'Give the body an asymmetric belly'),
    ], .055)
    inside = s.leaf(capped_cone('interior', (0, .045, 0), .098, .143, .238),
                    'Hollow the body and open its mouth', 1.5)
    if detail:
        inside = s.blend('rounded-interior', [inside,
            s.leaf(sphere('belly-interior', (-.024, -.095, 0), .148), 'Round the inside of the belly', 1.1)], .035)
    body = s.cut('hollow-body', body, inside, .012)
    # The analytic torus reads more cleanly than the sampled handle audition.
    handle = s.leaf(torus('handle-loop', (.191, .035, 0), .126, .033,
                         rotation_xyzw=rotation((1, 0, 0), 90)), 'Fuse a flowing handle', 1.2)
    body = s.blend('handled-vessel', [body, handle], .035)
    rim = s.leaf(torus('mouth-rim', (0, .235, 0), .157, .016), 'Soften the open lip')
    body = s.blend('rimmed-vessel', [body, rim], .012)
    for i, (x, y, r) in enumerate(((-.065, .07, .039), (.025, .015, .045), (-.06, -.055, .034))):
        cut = s.leaf(capsule(f'piercing-{i}', (x, y-.015 if detail else y, .075 if detail else .105),
                            (x, y-.015 if detail else y, .30), r),
                     'Pierce the wall into the hollow interior', 1.2)
        body = s.cut(f'open-piercing-{i}', body, cut, .007)
    bottom = s.leaf(box('base-trim', (0, -.48, 0), (.4, .2, .4), .001), 'Trim a stable ceramic foot', .6)
    return s, s.cut('flat-vessel-base', body, bottom, 0)


def arch(variant=0, detail=False):
    s = Sculpt(detail)
    masses = [
        s.leaf(capsule('left-support', (-.225, -.245, -.005), (-.17, .075, -.02), .105), 'Build the broad left support'),
        s.leaf(capsule('right-support', (.205, -.24, .012), (.155, .055, .015), .12), 'Raise the narrower leaning support'),
        s.leaf(sphere('left-crown', (-.115, .12 if variant == 0 else .16, -.015), .185), 'Fuse the bridge crown'),
        s.leaf(sphere('right-crown', (.115, .09, -.005), .18), 'Carry the crown across the opening'),
    ]
    body = s.blend('rock-mass', masses, .065)
    for name, start, end, r in [
        ('front-cavity', (-.018, -.115, .35), (-.02, -.105, .015), .145),
        ('rear-cavity', (.018, -.12, -.35), (-.025, -.09, -.015), .135),
    ]:
        cut = s.leaf(capsule(name, start, end, r),
                     'Carve the front alcove' if name == 'front-cavity' else 'Meet the alcove and break through', 1.8)
        body = s.cut(f'{name}-carve', body, cut, .025)
    channel = s.stroke('wind-channel', [(-.34, .12, .155), (-.22, .17, .181),
                       (-.09, .19, .188), (.095, .16, .17)], [.035, .035, .027, .012],
                       'Sweep a channel along the crown', .012) if detail else s.leaf(
                       capsule('wind-channel', (-.34, .12, .14), (.01, .16, .15), .052),
                       'Sweep a channel along the crown', 1.4)
    body = s.cut('wind-channel-carve', body, channel, .015)
    if detail:
        for name, position, extent, angle in (
            ('crown-plane', (-.08, .345, 0), (.32, .10, .3), 12),
            ('outer-right-plane', (.39, .12, 0), (.09, .38, .3), 10),
            ('wind-face', (0, .12, .28), (.5, .45, .13), 0),
        ):
            tool = s.leaf(box(name, position, extent, .002, rotation_xyzw=rotation((0, 0, 1), angle)),
                          'Weather a broad exposed rock plane', 1.2)
            body = s.cut(name+'-carve', body, tool, .007)
        for i, points in enumerate((
            [(-.29,-.21),(-.245,-.19),(-.20,-.175)],
            [(-.295,-.075),(-.25,-.052),(-.205,-.035)],
            [(-.12,.19),(0,.175),(.16,.16)],
            [(.19,-.18),(.245,-.16),(.285,-.14)],
        )):
            body=s.surface_groove(f'erosion-seam-{i}',body,points,.007,'Cut a directed erosion seam')
    trim = s.leaf(box('support-trim', (0, -.49, 0), (.45, .18, .4), .001), 'Trim the exposed support bases')
    return s, s.cut('trimmed-arch', body, trim, 0)


def driftwood(variant=0, detail=False):
    s = Sculpt(detail)
    reach = 1 if variant == 0 else .85
    trunk = s.stroke('trunk', [(-.15, -.265, -.005), (-.085, -.13, .005),
                     (0, .015, .015), (.065, .17, -.025), (.025, .305, -.06)],
                     [.10, .095, .08, .055, .015], 'Pull the twisting trunk', .035)
    branches = [trunk,
        s.stroke('left-branch', [(-.015, -.005, .01), (-.145, .075, .025), (-.25*reach, .205, .015)],
                 [.077, .048, .008], 'Sweep the left branch', .03),
        s.stroke('right-branch', [(.025, .06, 0), (.17, .13, .03), (.275*reach, .26, .005)],
                 [.063, .038, .008], 'Sweep the right branch', .025),
        s.stroke('root', [(-.10, -.19, -.02), (-.24, -.28, .045), (-.31, -.29, .09)],
                 [.075, .04, .009], 'Flow a root away from the trunk', .025),
    ]
    if detail:
        branches.append(s.stroke('rear-branch',[(.005,.04,-.015),(.05,.12,-.14),(.16,.20,-.19)],
                                 [.055,.034,.008],'Twist a branch behind the main fork',.02))
    body = s.blend('fused-driftwood', branches, .045)
    hollow = s.leaf(capsule('trunk-hollow', (-.22, -.43, .025) if detail else (-.18, -.31, .025),
                           (-.075, -.10, .04), .042 if detail else .045),
                    'Hollow the broken trunk end', 1.5)
    body = s.cut('hollowed-trunk', body, hollow, .012)
    knot = s.leaf(sphere('knot-hollow', (.016, .035, .106), .056), 'Carve a hollow knot at the fork', 1.4)
    body = s.cut('open-knot', body, knot, .009)
    if detail:
        trim = s.leaf(box('broken-tip-plane', (.025, .39, -.055), (.105, .09, .11), .001,
                          rotation_xyzw=rotation((0, 0, 1), -18)), 'Break the upper trunk across the grain', 1)
        body = s.cut('broken-trunk-tip', body, trim, 0)
        tool = s.leaf(round_cone('upper-hollow', (.025, .40, -.06), (.06, .19, -.025), .023, .012),
                      'Open the broken upper end', 1.2)
        body = s.cut('open-upper-end', body, tool, .004)
        for index,points in enumerate((
            [(-.175,-.255),(-.13,-.18),(-.07,-.10)],
            [(-.14,-.295),(-.075,-.18),(-.03,-.06)],
            [(-.105,-.22),(-.05,-.115),(.004,-.03)],
        )):
            body=s.surface_groove(f'grain-groove-{index}',body,points,[.003,.006,.0015],
                                  'Carve a long groove with the trunk flow')
        for side in (-1, 1):
            points = [(side*.105,.052 if side<0 else .10),(side*.17,.10 if side<0 else .13),
                      (side*.22,.16 if side<0 else .20)]
            body=s.surface_groove(f'branch-grain-{side}',body,points,[.002,.0045,.0015],
                                  'Follow the grain into the branch')
    return s, body


def make(subject, variant=0, detail=False):
    sculpt, tree = globals()[subject](variant, detail)
    def leaves(node):
        return [node['name']] if 'children' not in node else [n for c in node['children'] for n in leaves(c)]
    names = leaves(tree)
    assert len(names) == len(set(names)) == len(sculpt.history) <= 128
    assert set(names) == {step['node'] for step in sculpt.history}
    return tree, sculpt.history


def preset(scene, subject, build=False):
    return {'authored-scene': str(scene), 'atlas-axis': 64,
            'motion': 'presentation' if build else 'static',
            'duration': 50 if build else 12, **({'construction-seconds': 40} if build else {}),
            'orbit-turns': 1, 'pitch': .22 if subject != 'vessel' else .32,
            'yaw': .4 if subject != 'vessel' else .32, 'distance': 1.45,
            'target-y': .015, 'sun-elevation': 45, 'sun-azimuth': 150,
            'shadows': 1, 'ground': False, 'ao': False, 'exposure': .6, 'supersample': 2}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', choices=('blockout', 'final'), default='blockout')
    parser.add_argument('--subject', choices=SUBJECTS, action='append')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    for subject in args.subject or SUBJECTS:
        for variant in (range(2) if args.stage == 'blockout' else [0]):
            suffix = f'sculpture-blockouts/{subject}-{"ab"[variant]}' if args.stage == 'blockout' else f'sculpture-{subject}'
            scene = Path(f'projects/sdf/scenes/{suffix}.json')
            tree, history = make(subject, variant, args.stage == 'final')
            color, roughness = PALETTES[subject]
            write_scene(root/scene, TITLES[subject], [material('body', color, roughness)], tree, history)
            for build in (False, True) if args.stage == 'final' else (False,):
                path = root/f'projects/sdf/presets/{suffix}{"-build" if build else ""}.json'
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(preset(scene, subject, build), indent=2)+'\n')
            print(subject, variant, len(history), 'leaves')


if __name__ == '__main__':
    main()
