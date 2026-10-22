#!/usr/bin/env python3
"""A field-kit variant: layered optics, tapered housings and serviceable assemblies."""
import argparse
import copy
import math
from pathlib import Path
from author_robot import robot, robot_history
from sdf_author import box, capsule, capped_cone, cylinder, material, rotation, smooth_subtract, sphere, torus, union, write_scene


def delta(a, b):
    return [x-y for x, y in zip(a, b)]


def unit(v):
    length = math.sqrt(sum(x*x for x in v))
    if length < 1e-9:
        raise ValueError('A joint axis needs a direction')
    return [x/length for x in v]


def offset(p, direction, distance):
    return [x + distance*y for x, y in zip(p, direction)]


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def axis_rotation(direction):
    """Map local Y onto a joint/segment axis; leave the saved scene explicit."""
    x, y, z = unit(direction)
    q = [z, 0, -x, 1+y]
    length = math.sqrt(sum(v*v for v in q))
    return [v/length for v in q] if length > 1e-8 else [1, 0, 0, 0]


def rotate(q, v):
    inner = offset(cross(q[:3], v), v, q[3])
    return offset(v, cross(q[:3], inner), 2)


def front_axis(a, b):
    axis = unit(cross(a, b))
    return axis if axis[2] >= 0 else [-v for v in axis]


def fieldkit():
    root = copy.deepcopy(robot(detailed=True))
    history = copy.deepcopy(robot_history(detailed=True))
    def find(name, node=None):
        node = root if node is None else node
        if node['name'] == name:
            return node
        for child in node.get('children', []):
            found = find(name, child)
            if found is not None:
                return found
        return None
    def replace(name, replacement):
        node = find(name)
        assert node is not None, name
        node.clear()
        node.update(replacement)
    def insert_after(name, *steps):
        index = next(i for i, step in enumerate(history) if step['node'] == name)
        for node, label in reversed(steps):
            history.insert(index + 1, dict(node=node, label=label, seconds=.65))
    front = rotation((1, 0, 0), 90)
    replace('optic-barrel', capped_cone('optic-barrel', (-.027, .006, .097),
        .070, .079, .026, 'teal', rotation_xyzw=front))
    head = find('head')
    head['children'].extend([
        torus('optic-seal', (-.027, .006, .124), .055, .004, 'rubber', rotation_xyzw=front),
        torus('optic-lip', (-.027, .006, .129), .065, .007, 'teal', rotation_xyzw=front)])
    insert_after('optic-bore', ('optic-seal', 'Seat the optic seal'),
                 ('optic-lip', 'Roll the front optic lip'))
    # Tilt the head about its neck bearing, rather than about the shell center.
    neck_pivot = (0, .108, 0)
    head_up = rotate(head['rotation_xyzw'], (0, 1, 0))
    head['position'] = offset(neck_pivot, head_up, .124)
    for index, y in enumerate((.102, .117)):
        name = f'neck-collar-{index}'
        position = (0, y, 0) if index == 0 else offset(neck_pivot, head_up, .009)
        orientation = [0, 0, 0, 1] if index == 0 else axis_rotation(head_up)
        root['children'].append(torus(name, position, .035, .0045, 'teal', rotation_xyzw=orientation))
        insert_after('neck' if index == 0 else 'neck-collar-0', (name, 'Fit a neck joint collar'))
    # Scope the rounded recess to the body, preserving the separate chest panel.
    body = copy.deepcopy(find('torso-form'))
    replace('torso-form', smooth_subtract('abdomen-recess', body,
        box('abdomen-door-cut', (0, -.132, .091), (.061, .024, .016), .006), .004,
        cut_material='rubber'))
    root['children'].extend([
        box('abdomen-door', (0, -.132, .078), (.054, .018, .004), .005, 'ivory'),
        cylinder('abdomen-latch', (.032, -.132, .089), .009, .003, 'teal', rotation_xyzw=front)])
    insert_after('hip-housing', ('abdomen-door-cut', 'Round a service-door recess'),
                 ('abdomen-door', 'Seat the ivory service door'),
                 ('abdomen-latch', 'Fit its quarter-turn latch'))
    display = find('chest-display-recess')
    display.update(type='smooth-subtract', radius=.003)
    for side in ('left', 'right'):
        sign = -1 if side == 'left' else 1
        shoulder = (sign*.161, .005, 0)
        wrist = (-.20, -.177, .043) if side == 'left' else (.241, .025, .075)
        elbow = (-.19, -.09, .008) if side == 'left' else (.205, -.047, .022)
        upper_axis, lower_axis = unit(delta(elbow, shoulder)), unit(delta(wrist, elbow))
        shoulder_axis = front_axis((0, -1, 0), upper_axis)
        elbow_axis = front_axis(upper_axis, lower_axis)
        shoulder_frame, elbow_frame = axis_rotation(shoulder_axis), axis_rotation(elbow_axis)
        replace(f'{side}-shoulder-joint', sphere(f'{side}-shoulder-joint', shoulder, .038, 'rubber'))
        replace(f'{side}-upper-arm', capsule(f'{side}-upper-arm', shoulder, elbow, .023, 'ivory'))
        replace(f'{side}-shoulder-cap', cylinder(f'{side}-shoulder-cap', offset(shoulder, shoulder_axis, .036),
            .018, .005, 'teal', rotation_xyzw=shoulder_frame))
        replace(f'{side}-shoulder-slot', box(f'{side}-shoulder-slot', offset(shoulder, shoulder_axis, .041),
            (.011, .012, .0025), .0015, rotation_xyzw=shoulder_frame))
        forearm_center = [(a+b)/2 for a, b in zip(elbow, wrist)]
        forearm_half_length = math.sqrt(sum(v*v for v in delta(wrist, elbow)))/2
        forearm_frame = axis_rotation(lower_axis)
        replace(f'{side}-forearm', capped_cone(f'{side}-forearm', forearm_center,
            .021, .030, forearm_half_length, 'teal', rotation_xyzw=forearm_frame))
        arm = find(f'{side}-arm')
        cuff = [e + .85 * (w-e) for e, w in zip(elbow, wrist)]
        arm['children'].extend([
            torus(f'{side}-wrist-seal', cuff, .028, .005, 'rubber', rotation_xyzw=forearm_frame),
            cylinder(f'{side}-elbow-cap', offset(elbow, elbow_axis, .026), .018, .004,
                     'teal', rotation_xyzw=elbow_frame),
            torus(f'{side}-elbow-rim', offset(elbow, elbow_axis, .030), .020, .0035,
                  'ivory', rotation_xyzw=elbow_frame)])
        insert_after(f'{side}-forearm', (f'{side}-wrist-seal', f'Seat the {side} wrist seal'))
        insert_after(f'{side}-elbow', (f'{side}-elbow-cap', f'Cap the {side} elbow bearing'),
                     (f'{side}-elbow-rim', f'Fit its ivory retaining ring'))
        # One hip-to-ankle centerline places both leg segments and the ankle seal.
        x = sign*.075
        hip, ankle = (x, -.17, .006), (x, -.311, .018)
        leg_up = unit(delta(hip, ankle))
        knee, shin_bottom = offset(hip, leg_up, -.055), offset(ankle, leg_up, .014)
        shin_center = [(a+b)/2 for a, b in zip(knee, shin_bottom)]
        shin_half_length = math.sqrt(sum(v*v for v in delta(knee, shin_bottom)))/2
        leg_frame = axis_rotation(leg_up)
        replace(f'{side}-hip-link', capsule(f'{side}-hip-link', hip, knee, .024, 'rubber'))
        replace(f'{side}-shin', capped_cone(f'{side}-shin', shin_center, .038, .027, shin_half_length,
                                          'ivory', rotation_xyzw=leg_frame))
        find(f'{side}-ankle')['position'] = list(ankle)
        find(f'{side}-leg')['children'].append(torus(f'{side}-ankle-seal', offset(ankle, leg_up, .0135),
            .036, .005, 'teal', rotation_xyzw=leg_frame))
        for name in ('foot', 'sole', 'toe-groove'):
            find(f'{side}-{name}')['position'][0] = x
        insert_after(f'{side}-shin', (f'{side}-ankle-seal', f'Seat the {side} ankle seal'))
    backpack = find('backpack')
    for side, x in (('left', -.105), ('right', .105)):
        backpack['children'].extend([
            capped_cone(f'{side}-power-pod', (x, -.031, -.139), .022, .018, .054, 'ivory'),
            torus(f'{side}-pod-lower-seal', (x, -.078, -.139), .020, .004, 'teal'),
            torus(f'{side}-pod-upper-seal', (x, .014, -.139), .019, .004, 'teal')])
        insert_after('backpack-spine' if side == 'left' else 'left-pod-upper-seal',
                     (f'{side}-power-pod', f'Attach the {side} tapered power pod'),
                     (f'{side}-pod-lower-seal', f'Secure its lower collar'),
                     (f'{side}-pod-upper-seal', f'Secure its upper collar'))
    # Check the generated history independently of the native parser.
    def leaves(node):
        return [node['name']] if 'children' not in node else [n for c in node['children'] for n in leaves(c)]
    names = leaves(root)
    assert len(set(names)) == len(names) == len(history)
    assert set(names) == {step['node'] for step in history}
    return root, history


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('projects/sdf/scenes/survey-robot-fieldkit.json'))
    args = parser.parse_args()
    root, history = fieldkit()
    write_scene(args.output, 'Survey robot field kit', [
        material('ivory', 'E8DDC8', .52), material('teal', '398E92', .31),
        material('rubber', '25333B', .8), material('amber', 'F6A83B', .29, .18),
    ], root, history=history)
    print(args.output, len(history), 'edits')
