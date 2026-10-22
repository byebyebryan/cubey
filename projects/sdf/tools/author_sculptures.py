#!/usr/bin/env python3
"""Deterministic native-CSG sculptures, with retained clay blockout auditions."""
import argparse
import json
from pathlib import Path

from sdf_author import (box, capsule, capped_cone, cylinder, material, rotation,
                        shape, smooth_subtract, sphere, subtract, torus, union,
                        write_scene)

SUBJECTS = ('guardian', 'vessel', 'arch', 'driftwood')
TITLES = dict(guardian='Stone guardian', vessel='Pierced ceramic',
              arch='Wind-carved arch', driftwood='Twisted driftwood')
PALETTES = dict(guardian=('A6B5A8', .76), vessel=('3E8D91', .38),
                arch=('C99D74', .84), driftwood=('AA7955', .75))


class Sculpt:
    """Small authoring convenience; all runtime geometry remains explicit JSON."""
    def __init__(self):
        self.history = []

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

    def stroke(self, name, points, radii, label, blend=.025):
        """Bounded piecewise capsules, using only already-supported primitives."""
        assert len(points) == len(radii) >= 2
        segments = [self.leaf(capsule(f'{name}-{i}', a, b, (ra+rb)/2),
                              label if i == 0 else f'{label} / continue', .42)
                    for i, (a, b, ra, rb) in enumerate(zip(points, points[1:], radii, radii[1:]))]
        return self.blend(name, segments, blend)

    def cut(self, name, body, cutter, blend=.008):
        return smooth_subtract(name, body, cutter, blend) if blend else subtract(name, body, cutter)


def guardian(variant=0, detail=False):
    s = Sculpt()
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
    for side in (-1, 1):
        tag = 'left' if side < 0 else 'right'
        horn = s.stroke(f'{tag}-horn', [(side*.124, .205, -.035), (side*.21, .255, -.05),
                       (side*.247, .335 if variant == 0 else .30, -.075)], [.051, .036, .007],
                       'Grow the swept horn', .025)
        masses.append(horn)
    body = s.blend('fused-bust', masses, .042)
    for side in (-1, 1):
        tag = 'left' if side < 0 else 'right'
        cutter = s.leaf(capsule(f'{tag}-eye-socket', (side*.055, .065, .145),
                                (side*.112, .074, .122), .039), 'Carve the eye socket', 1.2)
        body = s.cut(f'{tag}-socket-carve', body, cutter, .008)
    mouth = s.leaf(capsule('mouth-cut', (-.067, -.067, .151), (.067, -.067, .151), .013),
                   'Carve the mouth and expression', 1.2)
    body = s.cut('mouth-carve', body, mouth, .005)
    for side in (-1, 1):
        cut = s.leaf(sphere(f'nostril-{side}', (side*.023, -.019, .204), .013), 'Open a nostril', .8)
        body = s.cut(f'nostril-carve-{side}', body, cut, .003)
    return s, body


def vessel(variant=0, detail=False):
    s = Sculpt()
    radius = .19 if variant == 0 else .16
    body = s.blend('vessel-mass', [
        s.leaf(capped_cone('body-profile', (0, -.01, 0), .135, .17, .245), 'Raise the vessel profile'),
        s.leaf(sphere('belly', (-.024 if variant == 0 else -.04, -.095, 0), radius), 'Give the body an asymmetric belly'),
    ], .055)
    inside = s.leaf(capped_cone('interior', (0, .045, 0), .098, .143, .238),
                    'Hollow the body and open its mouth', 1.5)
    body = s.cut('hollow-body', body, inside, .012)
    handle = s.leaf(torus('handle-loop', (.191, .035, 0), .126, .033,
                         rotation_xyzw=rotation((1, 0, 0), 90)), 'Fuse a flowing handle', 1.2)
    body = s.blend('handled-vessel', [body, handle], .035)
    rim = s.leaf(torus('mouth-rim', (0, .235, 0), .157, .016), 'Soften the open lip')
    body = s.blend('rimmed-vessel', [body, rim], .012)
    for i, (x, y, r) in enumerate(((-.065, .07, .039), (.025, .015, .045), (-.06, -.055, .034))):
        cut = s.leaf(capsule(f'piercing-{i}', (x, y, .105), (x, y, .30), r),
                     'Pierce the wall into the hollow interior', 1.2)
        body = s.cut(f'open-piercing-{i}', body, cut, .007)
    bottom = s.leaf(box('base-trim', (0, -.48, 0), (.4, .2, .4), .001), 'Trim a stable ceramic foot', .6)
    return s, s.cut('flat-vessel-base', body, bottom, 0)


def arch(variant=0, detail=False):
    s = Sculpt()
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
    channel = s.leaf(capsule('wind-channel', (-.34, .12, .14), (.01, .16, .15), .052),
                     'Sweep a channel along the crown', 1.4)
    body = s.cut('wind-channel-carve', body, channel, .015)
    trim = s.leaf(box('support-trim', (0, -.49, 0), (.45, .18, .4), .001), 'Trim the exposed support bases')
    return s, s.cut('trimmed-arch', body, trim, 0)


def driftwood(variant=0, detail=False):
    s = Sculpt()
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
    body = s.blend('fused-driftwood', branches, .045)
    hollow = s.leaf(capsule('trunk-hollow', (-.18, -.31, .025), (-.075, -.10, .04), .045),
                    'Hollow the broken trunk end', 1.5)
    body = s.cut('hollowed-trunk', body, hollow, .012)
    knot = s.leaf(sphere('knot-hollow', (.016, .035, .106), .056), 'Carve a hollow knot at the fork', 1.4)
    return s, s.cut('open-knot', body, knot, .009)


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
