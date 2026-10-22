#!/usr/bin/env python3
"""Author the original ivory/teal survey robot as an explicit grouped scene."""
import argparse
from pathlib import Path
from sdf_author import box, capsule, cylinder, material, rotation, smooth_union, sphere, subtract, union, write_scene


def robot():
    front = rotation((1, 0, 0), 90)
    shell = subtract('head-eye-socket',
        box('head-shell', (0, 0, 0), (.145, .082, .065), .03),
        cylinder('eye-socket-cut', (-.027, .006, .095), .069, .10, rotation_xyzw=front),
        cut_material='rubber')
    for index, y in enumerate((-.026, -.011, .004)):
        shell = subtract(f'head-vent-{index}', shell,
            box(f'head-vent-cut-{index}', (.105, y, .087), (.022, .0025, .018), .002), cut_material='rubber')
    eye_ring = subtract('optic-ring',
        cylinder('optic-barrel', (-.027, .006, .097), .074, .026, 'teal', rotation_xyzw=front),
        cylinder('optic-bore', (-.027, .006, .10), .058, .06, rotation_xyzw=front), cut_material='teal')
    head = union('head', shell, eye_ring,
        cylinder('sensor-seat', (-.027, .006, .10), .057, .014, 'rubber', rotation_xyzw=front),
        cylinder('sensor-amber', (-.027, .006, .119), .046, .005, 'amber', rotation_xyzw=front),
        cylinder('sensor-pupil', (-.027, .006, .126), .025, .003, 'rubber', rotation_xyzw=front),
        sphere('sensor-glint', (-.041, .024, .131), .006, 'ivory'),
        box('head-left-ear', (-.185, 0, 0), (.012, .029, .034), .008, 'teal'),
        box('head-right-ear', (.185, 0, 0), (.012, .029, .034), .008, 'teal'),
        capsule('antenna-stem', (.10, .105, -.025), (.122, .19, -.025), .005, 'rubber'),
        sphere('antenna-tip', (.122, .20, -.025), .015, 'amber'),
        position=(0, .232, 0), rotation_xyzw=rotation((0, 0, 1), -4), material='ivory')
    torso = smooth_union('torso-form',
        box('torso-shell', (0, -.04, 0), (.10, .11, .065), .027, 'ivory'),
        sphere('hip-housing', (0, -.14, 0), .075, 'ivory'), .025)
    chest = subtract('chest-panel',
        box('chest-accent', (0, -.035, .089), (.063, .048, .006), .014, 'teal'),
        box('chest-notch-cut', (.028, -.07, .10), (.022, .007, .015), .003), cut_material='rubber')
    backpack = union('backpack',
        box('backpack-case', (0, -.035, -.11), (.07, .08, .025), .018, 'teal'),
        box('backpack-spine', (0, -.035, -.16), (.018, .065, .005), .006, 'ivory'))
    parts = [head, cylinder('neck', (0, .101, 0), .035, .037, 'rubber'), torso, chest,
             box('chest-status', (-.033, -.019, .106), (.016, .004, .0025), .002, 'amber'), backpack]
    for side in (-1, 1):
        label = 'left' if side < 0 else 'right'
        shoulder = (side * .151, .005, 0)
        elbow = (-.19, -.09, .008) if side < 0 else (.205, -.047, .022)
        wrist = (-.20, -.177, .043) if side < 0 else (.241, .025, .075)
        hand = (-.20, -.211, .052) if side < 0 else (.245, .070, .082)
        parts.append(union(f'{label}-arm',
            sphere(f'{label}-shoulder-joint', shoulder, .035, 'rubber'),
            capsule(f'{label}-upper-arm', shoulder, elbow, .023, 'ivory'),
            sphere(f'{label}-elbow', elbow, .027, 'rubber'),
            capsule(f'{label}-forearm', elbow, wrist, .027, 'teal'),
            capsule(f'{label}-hand', wrist, hand, .026, 'ivory')))
        x = side * .075
        parts.append(union(f'{label}-leg',
            capsule(f'{label}-hip-link', (x, -.17, 0), (x, -.245, .005), .024, 'rubber'),
            cylinder(f'{label}-shin', (x, -.259, .008), .031, .038, 'ivory'),
            sphere(f'{label}-ankle', (x, -.311, .018), .024, 'rubber'),
            box(f'{label}-foot', (side * .085, -.344, .045), (.044, .024, .064), .015, 'ivory'),
            box(f'{label}-sole', (side * .085, -.372, .045), (.044, .006, .064), .01, 'rubber')))
    return union('survey-robot', *parts, material='ivory')


def robot_history():
    steps = []
    def add(node, label, seconds=.65):
        steps.append(dict(node=node, label=label, seconds=seconds))
    add('torso-shell', 'Start with a rounded box torso', 1.1)
    add('hip-housing', 'Blend a sphere into the torso', 1.1)
    add('neck', 'Add the neck joint')
    add('head-shell', 'Place the tilted head shell', 1.1)
    add('eye-socket-cut', 'Cut the sensor socket', 1.8)
    for i in range(3):
        add(f'head-vent-cut-{i}', f'Cut face vent {i+1} of 3', 1.1)
    add('optic-barrel', 'Fit the teal optic barrel', .85)
    add('optic-bore', 'Bore through the optic barrel', 1.6)
    for node, label in [('sensor-seat', 'Insert the dark sensor seat'),
                        ('sensor-amber', 'Add the amber sensor'),
                        ('sensor-pupil', 'Add its dark pupil'),
                        ('sensor-glint', 'Place the small ivory glint'),
                        ('head-left-ear', 'Fit the left side cap'),
                        ('head-right-ear', 'Fit the right side cap')]:
        add(node, label)
    add('chest-accent', 'Attach the teal chest panel', .85)
    add('chest-notch-cut', 'Cut the chest panel notch', 1.4)
    add('chest-status', 'Add the amber status indicator')
    for side in ('left', 'right'):
        for part, label in [('shoulder-joint', 'shoulder joint'), ('upper-arm', 'upper arm'),
                            ('elbow', 'elbow joint'), ('forearm', 'teal forearm'), ('hand', 'hand'),
                            ('hip-link', 'hip link'), ('shin', 'shin'), ('ankle', 'ankle joint'),
                            ('foot', 'rounded foot'), ('sole', 'dark sole')]:
            add(f'{side}-{part}', f'Add the {side} {label}', .55)
    add('backpack-case', 'Attach the backpack on the rear', .8)
    add('backpack-spine', 'Fit its ivory spine', .65)
    add('antenna-stem', 'Add the antenna stem', .8)
    add('antenna-tip', 'Finish with the amber antenna tip', 1.1)
    return steps


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('projects/sdf/scenes/survey-robot.json'))
    args = parser.parse_args()
    write_scene(args.output, 'Survey robot', [
        material('ivory', 'E8DDC8', .52), material('teal', '398E92', .31),
        material('rubber', '25333B', .8), material('amber', 'F6A83B', .29, .18),
    ], robot(), history=robot_history())
    print(args.output)
