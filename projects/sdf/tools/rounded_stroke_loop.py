#!/usr/bin/env python3
"""Independent numerical hull oracle and serial rounded-stroke qualification."""
import argparse
import copy
import json
import math
from pathlib import Path
import random

import cached_edit_loop as edit
import local_edit_loop as local
import stroke_loop as stroke
from interactive_edit_loop import identity, save, RAW
from investigate_guardian import ROOT, digest
from sdf_author import capsule, round_cone
from tracing_comparison import document, sphere, ray, dot, unit, H


def rotate(v, q):
    x, y, z, w = q
    cross = lambda a, b: [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
    a = cross([x, y, z], v)
    b = cross([x, y, z], a)
    return [v[i] + 2*(w*a[i]+b[i]) for i in range(3)]


def endpoints(shape):
    p = shape.get('position', [0, 0, 0]); scale = shape.get('scale', 1)
    if shape['type'] == 'sphere':
        return p, p, shape['radius']*scale, shape['radius']*scale
    axis = rotate([0, shape['half_length']*scale, 0], shape.get('rotation_xyzw', [0, 0, 0, 1]))
    a, b = ([p[i]-axis[i] for i in range(3)], [p[i]+axis[i] for i in range(3)])
    ra = shape.get('radius', shape.get('bottom_radius'))*scale
    rb = shape.get('radius', shape.get('top_radius'))*scale
    return a, b, ra, rb


def minimum(f, a, b, iterations=48):
    # Ternary minimization of a convex function; no conical intersection formula.
    for _ in range(iterations):
        c, d = (2*a+b)/3, (a+2*b)/3
        if f(c) < f(d):
            b = d
        else:
            a = c
    x = (a+b)/2
    return x, f(x)


def field(p, shape):
    a, b, ra, rb = endpoints(shape)
    delta = [b[i]-a[i] for i in range(3)]
    def value(t):
        return math.sqrt(sum((p[i]-a[i]-t*delta[i])**2 for i in range(3))) - (ra+(rb-ra)*t)
    if sum(x*x for x in delta) < 1e-30:
        return math.dist(p, a)-max(ra, rb)
    return min(value(0), value(1), minimum(value, 0, 1)[1])


def interval(origin, direction, shape):
    if shape['type'] in ('sphere', 'box'):
        return stroke.interval(origin, direction, shape)
    a, b, ra, rb = endpoints(shape)
    center = [(x+y)/2 for x, y in zip(a, b)]
    bound = sphere('bound', math.dist(a, b)/2+max(ra, rb), center)
    limits = stroke.interval(origin, direction, bound)
    if not limits:
        return None
    f = lambda t: field([o+d*t for o, d in zip(origin, direction)], shape)
    middle, value = minimum(f, *limits)
    if value > 1e-10:
        return None
    if abs(value) < 1e-10:
        return middle, middle
    low, high = limits
    a, b = low, middle
    for _ in range(48):
        x = (a+b)/2
        if f(x) > 0:
            a = x
        else:
            b = x
    entry = (a+b)/2
    a, b = middle, high
    for _ in range(48):
        x = (a+b)/2
        if f(x) <= 0:
            a = x
        else:
            b = x
    return entry, (a+b)/2


def oracle(origin, direction, bodies, tools, cut):
    parts = []
    for kind, shapes in [('base', bodies), ('tool', tools), ('clip', [dict(type='box', half_extent=[.499]*3)])]:
        for shape in shapes:
            limits = interval(origin, direction, shape)
            if limits:
                parts.append((kind, shape, limits))
    def inside(t):
        state = {k: any(a < t < b for kind, _, (a, b) in parts if kind == k) for k in ('base', 'tool', 'clip')}
        return state['clip'] and (state['base'] and not state['tool'] if cut else state['base'] or state['tool'])
    for t in sorted(set(t for _, _, limits in parts for t in limits if 0 <= t <= 3)):
        if inside(t-1e-8) == inside(t+1e-8):
            continue
        q = ray(origin, direction, t)
        owners = [(kind, shape) for kind, shape, limits in parts if min(abs(t-v) for v in limits) < 1e-9]
        if len(owners) == 1:
            kind, shape = owners[0]
            p = [o+d*t for o, d in zip(origin, direction)]
            if kind == 'clip':
                axis = max(range(3), key=lambda i: abs(p[i]))
                n = [0, 0, 0]; n[axis] = 1 if p[axis] > 0 else -1
            else:
                gradient = []
                for i in range(3):
                    a, b = p.copy(), p.copy(); a[i] += 1e-6; b[i] -= 1e-6
                    gradient.append(field(a, shape)-field(b, shape))
                n = unit(gradient)
                q['expected_pending'] = kind == 'tool'
            if cut and kind == 'tool':
                n = [-v for v in n]
            a, b, ra, rb = endpoints(shape) if kind != 'clip' else ([0]*3, [0]*3, 1, 1)
            singular = (ra == 0 and math.dist(p, a) < 1e-5) or (rb == 0 and math.dist(p, b) < 1e-5)
            if not singular:
                q['expected_normal'] = n
            else:
                q['singular_tip_normal_diagnostic'] = True
        return q
    return ray(origin, direction, None)


def segment(name, a, ra, b, rb, material='blue'):
    if any(not math.isfinite(v) for v in [*a, *b, ra, rb]) or min(ra, rb) < 0:
        raise ValueError('invalid endpoint/radius')
    if max(ra, rb) == 0:
        return None
    length = math.dist(a, b)
    if length <= abs(ra-rb):
        return dict(sphere(name, max(ra, rb), a if ra >= rb else b), material=material)
    if length < .000002:
        # A sphere at the first center changes the swept balls by at most L+dr.
        if length+abs(ra-rb) > H/2:
            raise ValueError('unrepresentable segment outside tolerance')
        return dict(sphere(name, max(ra, rb), a), material=material)
    return round_cone(name, a, b, ra, rb, material)


def inputs(args):
    rng = random.Random(731)
    shapes = [
        round_cone('unequal', [0, -.08, 0], [0, .08, 0], .025, .055, 'blue'),
        round_cone('reversed', [0, .08, 0], [0, -.08, 0], .055, .025, 'blue'),
        round_cone('equal', [0, -.08, 0], [0, .08, 0], .035, .035, 'blue'),
        capsule('capsule', [0, -.08, 0], [0, .08, 0], .035, 'blue'),
        round_cone('near-equal', [0, -.08, 0], [0, .08, 0], .035, .03500001, 'blue'),
        round_cone('pointed', [0, -.08, 0], [0, .08, 0], .045, 0, 'blue'),
        round_cone('contained', [0, -.02, 0], [0, .02, 0], .07, .02, 'blue'),
        round_cone('contain-boundary', [0, -.025, 0], [0, .025, 0], .07, .02, 'blue'),
        round_cone('near-containment', [0, -.025001, 0], [0, .025001, 0], .07, .02, 'blue'),
        round_cone('tiny', [0, -.000005, 0], [0, .000005, 0], .035, .03500001, 'blue'),
        dict(round_cone('transformed', [-.06, -.04, -.02], [.06, .08, .04], .02, .055, 'blue'), scale=1.2),
    ]
    geometry = []
    for shape in shapes:
        a, b, _, _ = endpoints(shape); center = [(x+y)/2 for x, y in zip(a, b)]
        rays = [([0, .35, 0], [0, -1, 0]), ([0, -.35, 0], [0, 1, 0]),
                ([.35, 0, 0], [-1, 0, 0]), ([0, 0, .35], [0, 0, -1]),
                (center, [1, 0, 0]), (center, unit([.2, .8, .3]))]
        for _ in range(8):
            d = unit([rng.uniform(-1, 1) for _ in range(3)])
            rays.append(([center[i]+d[i]*.35 for i in range(3)], [-x for x in d]))
        geometry.append(stroke.stroke_case(shape['name'], document(None), [shape], False,
            [oracle(o, d, [], [shape], False) for o, d in rays]))
    body = sphere('body', .22)
    for n in (0, 1, 2, 4, 8):
        for cut in (False, True):
            tools = [round_cone(f'tool-{i}', [(i-(n-1)/2)*.027, -.03, .19],
                [(i-(n-1)/2)*.027, .03, .21], .012+i*.001, .022-i*.001, 'blue') for i in range(n)]
            rays = [([.01, 0, .7], [0, 0, -1]), ([0, 0, .2], [1, 0, 0]),
                    ([0, 0, 0], [0, 0, 1]), ([-.7, 0, .2], [1, 0, 0])]
            geometry.append(stroke.stroke_case(f'count-{n}-'+('cut' if cut else 'add'), document(body), tools, cut,
                [oracle(o, d, [body], tools, cut) for o, d in rays]))
    for cut in (False, True):
        shape = round_cone('workspace', [-.04, -.02, .48], [.04, .02, .5], .025, .04, 'blue')
        body = sphere('body', .12, [0, 0, .43])
        regular = [([0, 0, .7], [0, 0, -1]), ([0, 0, .498], [0, 0, 1])]
        geometry.append(stroke.stroke_case('workspace-'+str(cut), document(body), [shape], cut,
            [oracle(o, d, [body], [shape], cut) for o, d in regular]))
        # The ray remains 0.0001 from the workspace plane, below the Cached
        # surface tolerance. Retain it alongside an unchanged-Cached control;
        # its near-plane hit ambiguity is separate from the tool intersection.
        o, d = [.03, 0, .4989], [1, 0, 0]
        c = stroke.stroke_case('workspace-parallel-subgrid-'+str(cut), document(body), [shape], cut,
                              [oracle(o, d, [body], [shape], cut)])
        c['diagnostic'] = True; geometry.append(c)
        c = stroke.stroke_case('workspace-parallel-zero-control-'+str(cut), document(body), [], cut,
                              [oracle(o, d, [body], [], cut)])
        c['diagnostic'] = True; geometry.append(c)
    shape = round_cone('empty-workspace', [-.04, -.02, .48], [.04, .02, .5], .025, .04, 'blue')
    geometry.append(stroke.stroke_case('workspace-tool-only', document(None), [shape], False,
        [oracle([.03, 0, .4989], [1, 0, 0], [], [shape], False)]))
    save(args.output / 'geometry-input.json', edit.manifest(geometry))
    # Reuse exactly these source shapes for canonical cache identity too.
    save(args.output / 'fixture-cache-input.json', edit.manifest(geometry))
    rows = []
    for subject in ('guardian', 'robot'):
        base = edit.subject_scene(subject)
        basefile = args.output / f'{subject}-base.json'; save(basefile, base)
        for op in ('add', 'cut'):
            c = edit.CUT_CENTERS[subject] if op == 'cut' else ([.03, .075, .28] if subject == 'guardian' else [0, .1, .15])
            for n in (0, 1, 2, 4, 8):
                tools = [round_cone(f'pending-{i}', [c[0]+(i-(n-1)/2)*.008, c[1]-.016, c[2]],
                    [c[0]+(i-(n-1)/2)*.008+.004, c[1]+.016, c[2]+.002],
                    .018+i*.0008, .027-i*.0008, base['materials'][0]['name']) for i in range(n)]
                case = stroke.stroke_case(f'{subject}-{op}-{n}', base, tools, op == 'cut')
                inp = args.output / (case['name']+'.json'); save(inp, edit.manifest([case]))
                scene = args.output / (case['name']+'-scene.json'); save(scene, case['scene'])
                rows.append(dict(subject=subject, operation=op, count=n, input=str(inp), scene=str(scene), base=str(basefile)))
    save(args.output / 'inputs.json', dict(geometry_input=str(args.output / 'geometry-input.json'),
        fixture_cache_input=str(args.output / 'fixture-cache-input.json'), rows=rows, complete=True))
    print(len(geometry), 'geometry fixtures;', len(rows), 'subject controls', flush=True)


def geometry(args):
    source = json.loads(args.inputs.read_text()); inp = Path(source['geometry_input'])
    out = args.output / 'native.json'
    result = local.run(edit.native(args, inp, out, extra=['--edit-rounded-segments', '--edit-queries']), out)
    data = json.loads(inp.read_text()); actual = result['native']['queries']['cases']
    score = edit.score(data, actual)
    owners = []
    for c, r in zip(data['cases'], actual):
        for q, hit in zip(c['queries'], r['results']):
            if 'expected_pending' in q:
                owners.append(q['expected_pending'] == hit['pending_surface'])
    score.update(owner_checks=len(owners), owners_passed=all(owners))
    save(args.output / 'geometry.json', dict(identity(args), score=score, run=result, complete=True))
    print(json.dumps(score, indent=2), flush=True)
    assert score['passed'] and all(owners)


def caches(args):
    source = json.loads(args.inputs.read_text())
    rows = [dict(name='fixtures', input=source['fixture_cache_input'])] if args.fixtures else source['rows']
    receipt = dict(identity(args), runs=[], complete=False)
    for row in rows:
        name = row.get('name', Path(row['input']).stem)
        out = args.output / (name+'.json')
        extra = ['--edit-rounded-segments', '--edit-cache-qualify', '--edit-commit-builder', args.mode,
                 '--edit-slice-groups', str(args.groups)]
        if not args.dump:
            extra += ['--edit-cache-no-dump']
        result = local.run(edit.native(args, Path(row['input']), out, extra=extra), out)
        assert result['native']['cache_passed']
        receipt['runs'].append(dict(case=row, **result)); save(args.output / 'caches.json', receipt)
        print(name, 'PASS', flush=True)
    receipt['complete'] = True; save(args.output / 'caches.json', receipt)


def admission(args):
    base = document(sphere('body', .15))
    tools = [round_cone(f'tool-{i}', [i*.005, -.02, .15], [i*.005, .02, .15],
                       .018+i*.001, .027-i*.001, 'blue') for i in range(9)]
    cases = [(f'count-{n}', stroke.stroke_case(f'count-{n}', base, tools[:n]), True, True)
             for n in (0, 1, 2, 4, 8)]
    good = stroke.stroke_case('good', base, tools[:2])
    cases += [('nine', stroke.stroke_case('nine', base, tools), True, False),
              ('legacy-rejects-taper', good, False, False)]
    varying_spheres = stroke.stroke_case('varying-spheres', base,
        [dict(sphere(f's-{i}', .02+i*.001), material='blue') for i in range(2)])
    cases += [('legacy-rejects-variable-radius', varying_spheres, False, False),
              ('rounded-admits-variable-radius', varying_spheres, True, True)]
    for label in ('mixed', 'material', 'scale', 'prefix', 'palette', 'smoothing', 'box', 'cut-material', 'zero-radii'):
        c = copy.deepcopy(good); tool = c['scene']['root']['children'][1]
        if label == 'mixed': c['scene']['root']['type'] = 'subtract'
        elif label == 'material': tool['material'] = 'red'
        elif label == 'scale': tool['scale'] = 1.1
        elif label == 'prefix': c['scene']['root']['children'][0]['children'][0]['radius'] = .16
        elif label == 'palette': c['scene']['materials'][0]['roughness'] = .99
        elif label == 'smoothing': c['scene']['root'].update(type='smooth-union', radius=.01)
        elif label == 'box': c['scene']['root']['children'][1] = dict(name='box', type='box', half_extent=[.02]*3)
        elif label == 'cut-material':
            c = stroke.stroke_case('cut-material', base, tools[:2], True)
            c['scene']['root']['cut_material'] = 'red'
        elif label == 'zero-radii': tool.update(bottom_radius=0, top_radius=0)
        cases.append((label, c, True, False))
    for label, a, ra, b, rb, wanted in [
        ('stationary', [0, 0, 0], .02, [0, 0, 0], .03, 'sphere'),
        ('contained', [0, 0, 0], .02, [0, .005, 0], .03, 'sphere'),
        ('subminimum', [0, 0, 0], .02, [0, .000001, 0], .02, 'sphere'),
        ('taper', [0, 0, 0], .02, [0, .04, 0], .03, 'round-cone'),
        ('lift', [0, 0, 0], 0, [0, .04, 0], 0, None)]:
        tool = segment(label, a, ra, b, rb)
        assert (tool['type'] if tool else None) == wanted
        cases.append((label, stroke.stroke_case(label, base, [tool] if tool else []), True, True))
    receipt = dict(identity(args), cases=[], complete=False)
    for label, c, rounded, expected in cases:
        inp, out = args.output/(label+'-input.json'), args.output/(label+'.json')
        save(inp, edit.manifest([c]))
        flags = ['--edit-validate-only'] + (['--edit-rounded-segments'] if rounded else [])
        result = local.run(edit.native(args, inp, out, extra=flags), out, allow_failure=True)
        assert (result['exit_code'] == 0) == expected, label
        receipt['cases'].append(dict(name=label, expected=expected, **result))
    receipt['complete'] = True; save(args.output/'admission.json', receipt)
    print(len(cases), 'admission/canonicalization checks passed', flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase', choices=['inputs', 'geometry', 'caches', 'admission'])
    p.add_argument('--output', type=Path, required=True); p.add_argument('--inputs', type=Path)
    p.add_argument('--binary', type=Path, default=ROOT / 'build/release/projects/sdf/cubey_project_sdf_cached_stroke_study')
    p.add_argument('--fixtures', action='store_true'); p.add_argument('--dump', action='store_true')
    p.add_argument('--mode', choices=['local', 'local-dom'], default='local-dom')
    p.add_argument('--groups', type=int, default=0)
    args = p.parse_args(); args.output = args.output.resolve(); args.output.mkdir(parents=True, exist_ok=False)
    globals()[args.phase](args)


if __name__ == '__main__':
    main()
