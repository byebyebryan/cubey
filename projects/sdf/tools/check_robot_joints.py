#!/usr/bin/env python3
"""Audit the saved field-kit joint geometry independently of its authoring pivots."""
import argparse
import hashlib
import json
import math
from pathlib import Path


def add(a, b): return [x+y for x, y in zip(a, b)]
def sub(a, b): return [x-y for x, y in zip(a, b)]
def scale(v, s): return [x*s for x in v]
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def length(v): return math.sqrt(dot(v, v))
def unit(v): return scale(v, 1/length(v))


def rotate(q, v):
    # Matrix expansion, independent of the author's cross-product rotation.
    x, y, z, w = q
    return [(1-2*y*y-2*z*z)*v[0] + (2*x*y-2*z*w)*v[1] + (2*x*z+2*y*w)*v[2],
            (2*x*y+2*z*w)*v[0] + (1-2*x*x-2*z*z)*v[1] + (2*y*z-2*x*w)*v[2],
            (2*x*z-2*y*w)*v[0] + (2*y*z+2*x*w)*v[1] + (1-2*x*x-2*y*y)*v[2]]


def compose(a, b):
    x, y, z, w = a; u, v, t, s = b
    return [w*u+x*s+y*t-z*v, w*v-x*t+y*s+z*u,
            w*t+x*v-y*u+z*s, w*s-x*u-y*v-z*t]


def audit(document):
    nodes, checks = {}, []
    def walk(node, position=(0, 0, 0), rotation=(0, 0, 0, 1), size=1):
        center = add(position, rotate(rotation, scale(node.get('position', (0, 0, 0)), size)))
        q = node.get('rotation_xyzw', (0, 0, 0, 1)); q = scale(q, 1/length(q))
        q = compose(rotation, q); size *= node.get('scale', 1)
        nodes[node['name']] = dict(node, center=center, axis=rotate(q, (0, 1, 0)), world_scale=size)
        for child in node.get('children', []): walk(child, center, q, size)
    walk(document['root'])
    def point(name): return nodes[name]['center']
    def axis(name): return nodes[name]['axis']
    def near(name, error, tolerance=1e-6):
        checks.append(dict(name=name, passed=error <= tolerance, error=error, tolerance=tolerance))
    def truth(name, passed, **metrics): checks.append(dict(name=name, passed=bool(passed), **metrics))
    def endpoints(name):
        node = nodes[name]; offset = scale(node['axis'], node['half_length']*node['world_scale'])
        return sub(node['center'], offset), add(node['center'], offset)
    def on_line(name, p, origin, direction):
        v = sub(p, origin); d = unit(direction)
        near(name, length(sub(v, scale(d, dot(v, d)))))
    for side in ('left', 'right'):
        shoulder, elbow = point(f'{side}-shoulder-joint'), point(f'{side}-elbow')
        a, b = endpoints(f'{side}-upper-arm')
        near(f'{side}: upper arm meets both pivots',
             min(length(sub(a, shoulder))+length(sub(b, elbow)), length(sub(b, shoulder))+length(sub(a, elbow))))
        lower_start, wrist = endpoints(f'{side}-forearm')
        near(f'{side}: forearm starts at elbow', length(sub(lower_start, elbow)))
        hand_ends = endpoints(f'{side}-hand')
        near(f'{side}: hand meets wrist', min(length(sub(wrist, p)) for p in hand_ends))
        upper, lower = unit(sub(elbow, shoulder)), unit(sub(wrist, elbow))
        for joint, directions in (('shoulder', [upper]), ('elbow', [upper, lower])):
            center = shoulder if joint == 'shoulder' else elbow
            cap = f'{side}-{joint}-cap'; normal = axis(cap)
            for index, direction in enumerate(directions):
                near(f'{side}: {joint} axis perpendicular to link {index+1}', abs(dot(normal, direction)))
            on_line(f'{side}: {joint} cap centered on pivot axis', point(cap), center, normal)
            sphere = nodes[f'{side}-shoulder-joint' if joint == 'shoulder' else f'{side}-elbow']
            depth = dot(sub(point(cap), center), normal); half = nodes[cap]['half_length']
            truth(f'{side}: {joint} cap seats into bearing', depth-half < sphere['radius'] < depth+half,
                  inner=depth-half, bearing_radius=sphere['radius'], outer=depth+half)
        rim = f'{side}-elbow-rim'; cap = f'{side}-elbow-cap'
        near(f'{side}: elbow rim shares cap axis', abs(1-dot(axis(rim), axis(cap))))
        on_line(f'{side}: elbow rim centered on hinge', point(rim), elbow, axis(cap))
        near(f'{side}: elbow rim seated on cap face',
             abs(dot(sub(point(rim), point(cap)), axis(cap))-nodes[cap]['half_length']))
        truth(f'{side}: rim overlaps cap edge',
              nodes[rim]['major_radius']-nodes[rim]['minor_radius'] < nodes[cap]['radius'] <
              nodes[rim]['major_radius']+nodes[rim]['minor_radius'])
        near(f'{side}: shoulder slot shares cap axis', abs(1-dot(axis(f'{side}-shoulder-slot'), axis(f'{side}-shoulder-cap'))))
        on_line(f'{side}: shoulder slot centered on bearing', point(f'{side}-shoulder-slot'), shoulder,
                axis(f'{side}-shoulder-cap'))
        near(f'{side}: wrist seal follows forearm', abs(1-dot(axis(f'{side}-wrist-seal'), axis(f'{side}-forearm'))))
        on_line(f'{side}: wrist seal centered on forearm', point(f'{side}-wrist-seal'), elbow, lower)
        near(f'{side}: gripper cut follows hand frame', abs(1-dot(axis(f'{side}-gripper-slot'), axis(f'{side}-hand'))))
        hip_ends = endpoints(f'{side}-hip-link')
        hip, knee = sorted(hip_ends, key=lambda p:p[1], reverse=True)
        ankle = point(f'{side}-ankle'); direction = sub(hip, ankle)
        for label, p in [('knee', knee), ('shin center', point(f'{side}-shin')),
                         ('ankle seal', point(f'{side}-ankle-seal'))]:
            on_line(f'{side}: {label} on hip-ankle centerline', p, ankle, direction)
        near(f'{side}: shin axis follows upper leg', abs(1-abs(dot(axis(f'{side}-shin'), unit(direction)))))
        near(f'{side}: ankle seal follows shin', abs(1-dot(axis(f'{side}-ankle-seal'), axis(f'{side}-shin'))))
        near(f'{side}: shin meets upper leg', min(length(sub(knee, p)) for p in endpoints(f'{side}-shin')))
        for part in ('foot', 'sole', 'toe-groove'):
            near(f'{side}: {part} centered beneath leg', abs(point(f'{side}-{part}')[0]-ankle[0]))
        for part in ('lower', 'upper'):
            pod, seal = f'{side}-power-pod', f'{side}-pod-{part}-seal'
            near(f'{side}: {part} power-pod collar axis', abs(1-dot(axis(pod), axis(seal))))
            on_line(f'{side}: {part} power-pod collar centered', point(seal), point(pod), axis(pod))
    near('neck: upper collar follows head tilt', abs(1-dot(axis('neck-collar-1'), axis('head'))))
    near('neck: head tilts about bearing', length(sub(sub(point('head'), scale(axis('head'), .124)), (0, .108, 0))))
    return dict(passed=all(c['passed'] for c in checks), checks=checks)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene', type=Path, default=Path('projects/sdf/scenes/survey-robot-fieldkit.json'))
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = audit(json.loads(args.scene.read_text()))
    result.update(scene=str(args.scene), scene_sha256=hashlib.sha256(args.scene.read_bytes()).hexdigest())
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2)+'\n')
    failed = [c['name'] for c in result['checks'] if not c['passed']]
    print(f'Joint audit: {len(result["checks"])-len(failed)}/{len(result["checks"])} passed')
    for name in failed: print('FAIL:', name)
    raise SystemExit(1 if failed else 0)
