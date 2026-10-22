#!/usr/bin/env python3
"""A field-kit variant: layered optics, tapered housings and serviceable assemblies."""
import argparse
import copy
from pathlib import Path
from author_robot import robot, robot_history
from sdf_author import box, capped_cone, cylinder, material, rotation, smooth_subtract, torus, union, write_scene


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
    for index, y in enumerate((.102, .117)):
        name = f'neck-collar-{index}'
        root['children'].append(torus(name, (0, y, 0), .035, .0045, 'teal'))
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
        forearm = copy.deepcopy(find(f'{side}-forearm'))
        replace(f'{side}-forearm', capped_cone(f'{side}-forearm', forearm['position'],
            .021, .030, forearm['half_length'], 'teal', rotation_xyzw=forearm['rotation_xyzw']))
        # The original capsule endpoints give a stable local Y axis for both arms.
        arm = find(f'{side}-arm')
        wrist = (-.20, -.177, .043) if side == 'left' else (.241, .025, .075)
        elbow = (-.19, -.09, .008) if side == 'left' else (.205, -.047, .022)
        cuff = [e + .85 * (w-e) for e, w in zip(elbow, wrist)]
        arm['children'].extend([
            torus(f'{side}-wrist-seal', cuff, .028, .005, 'rubber', rotation_xyzw=forearm['rotation_xyzw']),
            cylinder(f'{side}-elbow-cap', (elbow[0], elbow[1], elbow[2]+.026), .018, .004,
                     'teal', rotation_xyzw=front),
            torus(f'{side}-elbow-rim', (elbow[0], elbow[1], elbow[2]+.030), .020, .0035,
                  'ivory', rotation_xyzw=front)])
        insert_after(f'{side}-forearm', (f'{side}-wrist-seal', f'Seat the {side} wrist seal'))
        insert_after(f'{side}-elbow', (f'{side}-elbow-cap', f'Cap the {side} elbow bearing'),
                     (f'{side}-elbow-rim', f'Fit its ivory retaining ring'))
        shin = copy.deepcopy(find(f'{side}-shin'))
        replace(f'{side}-shin', capped_cone(f'{side}-shin', shin['position'], .038, .027, .038, 'ivory'))
        x = -.075 if side == 'left' else .075
        find(f'{side}-leg')['children'].append(torus(f'{side}-ankle-seal', (x, -.298, .008), .036, .005, 'teal'))
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
