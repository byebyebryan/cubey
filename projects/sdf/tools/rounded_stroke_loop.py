#!/usr/bin/env python3
"""Independent numerical hull oracle and serial rounded-stroke qualification."""
import argparse
import copy
import json
import math
from pathlib import Path
from sdf_paths import resolve_sdf_path
import random
import stroke_sampling
import statistics

import cached_edit_loop as edit
import local_edit_loop as local
import stroke_loop as stroke
from interactive_edit_loop import identity, save, RAW
from query_study import ROOT, digest, PRESETS, camera_queries
from quality_speed_loop import profile
from benchmark_tracing import percentile
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
        bottom = shape.get('radius', shape.get('bottom_radius'))
        top = shape.get('radius', shape.get('top_radius'))
        h = shape['half_length']; slope = (top-bottom)/(2*h)
        if abs(slope) < 1:
            radial = math.sqrt(1-slope*slope)
            radius = (bottom+slope*h)/radial
            extra = [([radius*.999, 0, .35], [0, 0, -1]),
                     ([radius*1.001, 0, .35], [0, 0, -1]),
                     ([radius*.9, 0, 0], [slope, radial, 0]),
                     ([bottom*radial*.9, -h-bottom*slope, .35], [0, 0, -1])]
            q = shape.get('rotation_xyzw', [0, 0, 0, 1]); scale = shape.get('scale', 1)
            for o, d in extra:
                position = rotate([x*scale for x in o], q)
                rays.append(([x+y for x, y in zip(position, shape['position'])], unit(rotate(d, q))))
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
        c = copy.deepcopy(good); c['base'] = copy.deepcopy(c['base'])
        tool = c['scene']['root']['children'][1]
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


def coverage_inputs(args):
    cases = []
    for count in (2, 4, 8):
        for mixed in ([False, True] if count == 8 else [False]):
            tools = []
            centers = [[0, 0, .35-i*.1] for i in range(count)]
            for i, p in enumerate(centers):
                a, b = [p[0], -.01, p[2]], [p[0], .01, p[2]]
                tool = round_cone(f'tool-{i}', a, b, .02, .025, 'blue')
                if mixed and i % 3 == 1: tool = capsule(f'tool-{i}', a, b, .023, 'blue')
                if mixed and i % 3 == 2: tool = dict(sphere(f'tool-{i}', .023, p), material='blue')
                tools.append(tool)
            for cut in (False, True):
                bodies = [sphere(f'body-{i}', .014, p) for i, p in enumerate(centers)] if cut else [sphere('side-body', .03, [.25, 0, 0])]
                if cut: bodies.append(sphere('surviving-tail', .012, [0, 0, centers[-1][2]-.1]))
                base = document(dict(name='bodies', type='union', children=bodies)) if len(bodies) > 1 else document(bodies[0])
                rays = [([0, 0, .7], [0, 0, -1]), (centers[0], [0, 0, -1]),
                        ([0, 0, -.7], [0, 0, 1]), ([.08, 0, .7], [0, 0, -1])]
                name = f'disjoint-{count}-mixed-{mixed}-cut-{cut}'
                cases.append(stroke.stroke_case(name, base, tools, cut,
                    [oracle(o, d, bodies, tools, cut) for o, d in rays]))
    body = sphere('solid', .3)
    tool = round_cone('buried-cavity', [0, -.08, 0], [0, .08, 0], .02, .04, 'blue')
    cases.append(stroke.stroke_case('buried-tapered-cavity', document(body), [tool], True,
        [oracle(o, d, [body], [tool], True) for o, d in [([0, 0, 0], [0, 1, 0]),
            ([0, 0, 0], [1, 0, 0]), ([0, 0, .2], [0, 0, -1]), ([0, 0, .7], [0, 0, -1])]]))
    inp = args.output/'geometry-input.json'; save(inp, edit.manifest(cases))
    save(args.output/'inputs.json', dict(geometry_input=str(inp), fixture_cache_input=str(inp), rows=[], complete=True))
    print(len(cases), 'disjoint/mixed/buried coverage cases', flush=True)


def sampling(args):
    tolerance = H/4  # reserve the other quarter-spacing for scripted input interpolation
    rows = []
    for hz in (120, 240):
        count = int(.05*hz)+1
        for name in ('straight', 'corner', 'curve', 'width-bulge', 'stationary-width'):
            def sample(t):
                p, r = [-.02+.04*t, 0, 0], .02
                if name == 'corner':
                    p = [-.02+.04*min(t, .5), .04*max(t-.5, 0), 0]
                elif name == 'curve': p[1] = .004*math.sin(math.pi*t)
                elif name == 'width-bulge': r += .003*math.sin(math.pi*t)
                elif name == 'stationary-width': p = [0, 0, 0]; r += .005*t
                return p, r
            poses = []
            for i in range(count):
                p, r = sample(i/(count-1))
                poses.append(dict(t=i/hz, p=p, r=r, corner=name == 'corner' and i == (count-1)//2))
            spans = stroke_sampling.segment(poses, tolerance)
            metrics = stroke_sampling.validate(poses, spans, tolerance)
            dense_error = 0
            for first, last in spans:
                for j in range(65):
                    u = j/64
                    t = (first+(last-first)*u)/(count-1)
                    p, r = sample(t)
                    a, b = poses[first], poses[last]
                    fitted = [x+(y-x)*u for x, y in zip(a['p'], b['p'])]
                    dense_error = max(dense_error, math.dist(p, fitted)+abs(r-(a['r']+(b['r']-a['r'])*u)))
            assert dense_error <= H/2+1e-12, (name, hz, dense_error)
            row = dict(name=f'{name}-{hz}', raw_hz=hz, duration_seconds=.05, poses=poses,
                spans=spans, raw_segments=len(poses)-1, accepted_segments=len(spans),
                tolerance=tolerance, scripted_combined_error=dense_error, **metrics)
            rows.append(row)
    # Separate strokes are segmented separately; no geometry is synthesized in
    # the gap between release and restart, irrespective of timestamp spacing.
    lift_paths = [[dict(t=j*.01, p=[start+j*.02, 0, 0], r=.01) for j in range(3)]
                  for start in (-.12, .08)]
    lift_tools = []
    for index, poses in enumerate(lift_paths):
        for a, b in stroke_sampling.segment(poses, tolerance):
            lift_tools.append(segment(f'lift-{index}', poses[a]['p'], .01, poses[b]['p'], .01))
    assert all(field([0, 0, 0], tool) > .05 for tool in lift_tools)
    bench = []
    for subject in ('guardian', 'robot'):
        for row in rows:
            for representation in ('raw', 'segmented'):
                poses = row['poses']; center = edit.CUT_CENTERS[subject]
                pairs = list(zip(range(len(poses)-1), range(1, len(poses)))) if representation == 'raw' else row['spans']
                tools = [segment(f'sample-{i}', [x+y for x, y in zip(poses[a]['p'], center)], poses[a]['r'],
                                 [x+y for x, y in zip(poses[b]['p'], center)], poses[b]['r'],
                                 edit.subject_scene(subject)['materials'][0]['name']) for i, (a, b) in enumerate(pairs)]
                base = edit.subject_scene(subject); cases = []
                for offset in range(0, len(tools), 8):
                    c = stroke.stroke_case(f'{row["name"]}-{offset}', base, tools[offset:offset+8], True)
                    cases.append(c); base = c['scene']
                inp = args.output / f'{subject}-{row["name"]}-{representation}.json'
                save(inp, edit.manifest(cases))
                bench.append(dict(subject=subject, path=row['name'], representation=representation,
                                  input=str(inp), tools=len(tools), batches=len(cases)))
    save(args.output/'sampling.json', dict(identity(args), rows=rows, benchmark=bench,
        recorded_combined_tolerance=tolerance, scripted_combined_tolerance=H/2,
        lift_restart_no_bridge=True, complete=True))
    print([(r['name'], r['raw_segments'], r['accepted_segments']) for r in rows], flush=True)


def sampling_work(args):
    source = json.loads(args.inputs.read_text()); receipt = dict(identity(args), runs=[], complete=False)
    rows = source['benchmark']
    for repeat in range(3):
        for row in rows if repeat % 2 == 0 else reversed(rows):
            out = args.output/f'{row["subject"]}-{row["path"]}-{row["representation"]}-{repeat}.json'
            flags = ['--edit-rounded-segments', '--edit-cache-qualify', '--edit-commit-builder', 'local-dom',
                     '--edit-cache-no-dump', '--edit-cache-chain']
            result = local.run(edit.native(args, Path(row['input']), out, extra=flags), out)
            assert result['native']['cache_passed']
            receipt['runs'].append(dict(case=row, repeat=repeat, **result)); save(args.output/'sampling-work.json', receipt)
            print(out.stem, 'PASS', flush=True)
    receipt['complete'] = True; save(args.output/'sampling-work.json', receipt)


def selected(args):
    return [r for r in json.loads(args.inputs.read_text())['rows'] if r['count'] == 8 or r['count'] == 0 and r['operation'] == 'add']


def captures(args):
    receipt = dict(identity(args), runs=[], complete=False)
    for row in selected(args):
        for view in ('preset', 'close'):
            for phase in (['preview'] if row['count'] == 0 else ['base', 'preview', 'committed']):
                prefix = args.output/f'{row["subject"]}-{row["operation"]}-{row["count"]}-{view}-{phase}'
                flags = ['--edit-rounded-segments', '--frames', '1', '--output', str(prefix.with_suffix('.png'))]
                if phase == 'committed': flags += ['--edit-commit-builder', 'local-dom']
                result = local.run(edit.native(args, Path(row['input']), prefix.with_suffix('.json'), phase,
                    flags, row['subject'], view, (960, 540)), prefix.with_suffix('.json'))
                image_hash = digest(prefix.with_suffix('.png'))
                entry = dict(case=row, view=view, phase=phase, image=str(prefix.with_suffix('.png')),
                             image_sha256=image_hash, **result)
                if phase == 'committed' or row['count'] == 0:
                    reference = prefix.with_suffix('.original.png')
                    cmd = edit.public_command(args.public_binary, row['subject'],
                        row['base'] if row['count'] == 0 else row['scene'], reference, view=view)
                    ref = local.run(cmd, prefix.with_suffix('.public.json'))
                    assert digest(reference) == image_hash, prefix
                    entry.update(original_cached=str(reference), original_cached_byte_identical=True, reference=ref)
                receipt['runs'].append(entry); save(args.output/'captures.json', receipt)
                print(prefix.stem, 'PASS', flush=True)
    receipt['complete'] = True; save(args.output/'captures.json', receipt)


def replay(args):
    receipt = dict(identity(args), runs=[], complete=False)
    for row in selected(args):
        if not row['count']: continue
        cfg = json.loads((ROOT/str(resolve_sdf_path(f'projects/sdf/presets/{PRESETS[row["subject"]]}.json'))).read_text())
        for view in ('preset', 'close'):
            camera = dict(cfg)
            if view == 'close': camera['distance'] = .8
            data = json.loads(Path(row['input']).read_text())
            data['cases'][0]['queries'] = camera_queries(camera, 160, 90)
            control = copy.deepcopy(data['cases'][0]); control['name'] += '-zero-control'; control['scene'] = copy.deepcopy(control['base'])
            data['cases'].append(control)
            prefix = args.output/f'{row["subject"]}-{row["operation"]}-{view}'
            inp, out = prefix.with_suffix('.input.json'), prefix.with_suffix('.json'); save(inp, data)
            result = local.run(edit.native(args, inp, out, extra=['--edit-rounded-segments', '--edit-queries']), out)
            actual, baseline = [c['results'] for c in result['native']['queries']['cases']]
            assert not any(r['status'] == 2 for r in actual)
            changed = sum(a['status'] != b['status'] or a['status'] == 1 and abs(a['t']-b['t']) > H for a, b in zip(actual, baseline))
            assert changed >= 10, (row, view, changed)
            elevation, azimuth = math.radians(cfg['sun-elevation']), math.radians(cfg['sun-azimuth'])
            light = [math.cos(elevation)*math.sin(azimuth), math.sin(elevation), -math.cos(elevation)*math.cos(azimuth)]
            shadows = [dict(kind='ray', origin=[q['origin'][i]+q['direction'][i]*r['t']+r['normal'][i]*2*H for i in range(3)], direction=light)
                       for q, r in zip(data['cases'][0]['queries'], actual) if r['status'] == 1]
            data['cases'] = data['cases'][:1]; data['cases'][0]['queries'] = shadows
            sinp, sout = prefix.with_suffix('.shadow-input.json'), prefix.with_suffix('.shadow.json'); save(sinp, data)
            shadow = local.run(edit.native(args, sinp, sout, extra=['--edit-rounded-segments', '--edit-queries']), sout)
            assert not any(r['status'] == 2 for r in shadow['native']['queries']['cases'][0]['results'])
            receipt['runs'].append(dict(case=row, view=view, camera_rays=len(actual), shadow_rays=len(shadows),
                exhausted=0, changed_cached_rays=changed, camera=result, shadow=shadow))
            save(args.output/'replay.json', receipt); print(prefix.stem, 'PASS', flush=True)
    receipt['complete'] = True; save(args.output/'replay.json', receipt)


def timing(args):
    rows = selected(args); cases = []
    for row in rows:
        for view in ('preset', 'close'):
            cases.append((row, view, 'rounded'))
            if not row['count']: cases.append((row, view, 'original'))
    frames, warmup = (36, 6) if args.pilot else (120, 20)
    receipt = dict(identity(args), runs=[], complete=False, frames=frames, warmup=warmup)
    for repeat in range(1 if args.pilot else 3):
        for row, view, route in cases if repeat % 2 == 0 else reversed(cases):
            prefix = args.output/f'{row["subject"]}-{row["operation"]}-{row["count"]}-{view}-{route}-{repeat}'
            flags = ['--headless-render-only', '--frames', str(frames), '--profile-warmup-frames', str(warmup), '--profile-output', str(prefix)]
            if route == 'original':
                cmd = edit.public_command(args.public_binary, row['subject'], row['base'], None, size=(1920, 1080), view=view)+flags
            else:
                cmd = edit.native(args, Path(row['input']), prefix.with_suffix('.json'), 'preview',
                    flags+['--edit-rounded-segments'], row['subject'], view, (1920, 1080))
            result = local.run(cmd, prefix.with_suffix('.json'))
            passes, metrics = profile(prefix, frames, warmup); values = passes['sdf.whole_scene']['values_ms']
            receipt['runs'].append(dict(case=row, view=view, route=route, repeat=repeat, **result,
                passes=passes, metrics=metrics, stats=dict(median_ms=statistics.median(values),
                    p95_ms=percentile(values, .95), max_ms=max(values), over_16_67=sum(v > 16.67 for v in values), over_11_11=sum(v > 11.11 for v in values))))
            save(args.output/'timing.json', receipt); print(prefix.stem, round(statistics.median(values), 3), flush=True)
    receipt['complete'] = True; save(args.output/'timing.json', receipt)


def prefixes(args):
    receipt = dict(identity(args), runs=[], complete=False)
    for row in selected(args):
        if row['count'] != 8: continue
        data = json.loads(Path(row['input']).read_text()); case = data['cases'][0]
        prefix = copy.deepcopy(case['scene'])
        for _ in range(4): prefix['root'] = prefix['root']['children'][0]
        if prefix.get('history'): prefix['history'] = prefix['history'][:len(case['base'].get('history', []))+4]
        case['prefix_scene'] = prefix
        reference_input = args.output/f'{row["subject"]}-{row["operation"]}-tail-input.json'
        save(reference_input, edit.manifest([dict(name='tail-reference', base=prefix, scene=case['scene'])]))
        tail = reference_input.with_suffix('.png')
        local.run(edit.native(args, reference_input, tail.with_suffix('.json'), 'preview',
            ['--edit-rounded-segments', '--frames', '1', '--output', str(tail)], row['subject'], size=(320, 180)), tail.with_suffix('.json'))
        settled = args.output/f'{row["subject"]}-{row["operation"]}-settled.png'
        local.run(edit.public_command(args.public_binary, row['subject'], row['scene'], settled, size=(320, 180)), settled.with_suffix('.public.json'))
        for fault in ('none', 'allocation', 'cancel-before-submit', 'stale', 'discard-after-submit', 'build-capacity', 'append-capacity'):
            data['prefix_fault'] = fault
            inp = args.output/f'{row["subject"]}-{row["operation"]}-{fault}-input.json'; save(inp, data)
            out = inp.with_suffix('.result.json')
            result = local.run(edit.native(args, inp, out, 'prefix', ['--edit-rounded-segments', '--edit-commit-builder', 'local-dom',
                '--headless-render-only', '--frames', str(4 if fault == 'none' else 5)], row['subject'], size=(320, 180)), out)
            native = result['native']; assert native['complete'] and native['drained']
            for frame in native['frames'][1:]:
                assert frame['baked_ids']+frame['render_pending_ids'] == list(range(1, 9))
                assert frame['total_uncommitted_before_completion'] <= 8
                assert frame['retired_after_completed_frame'] == frame['baked_prefix']
            middle = next(f for f in native['frames'] if f['baked_prefix'] == 4)
            assert digest(Path(middle['image'])) == digest(tail)
            assert digest(Path(native['frames'][-1]['image'])) == digest(settled)
            if fault != 'none':
                assert not native['attempts'][0]['published']
                assert digest(Path(native['frames'][1]['image'])) == digest(Path(native['frames'][2]['image']))
            retained = Path(native['retained_source_file'])
            assert json.loads(retained.read_text())['root'] == case['scene']['root']
            if fault == 'none':
                replay_image = retained.with_suffix('.replay.png')
                local.run(edit.public_command(args.public_binary, row['subject'], retained, replay_image, size=(320, 180)), replay_image.with_suffix('.public.json'))
                assert digest(replay_image) == digest(settled)
            receipt['runs'].append(dict(case=row, fault=fault, intermediate_byte_identical=True,
                settled_byte_identical=True, retained_source_matches=True, **result))
            save(args.output/'prefixes.json', receipt); print(row['subject'], row['operation'], fault, 'PASS', flush=True)
    receipt['complete'] = True; save(args.output/'prefixes.json', receipt)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase', choices=['inputs', 'coverage_inputs', 'geometry', 'caches', 'admission', 'sampling', 'sampling_work',
                                   'captures', 'replay', 'timing', 'prefixes'])
    p.add_argument('--output', type=Path, required=True); p.add_argument('--inputs', type=Path)
    p.add_argument('--binary', type=Path, default=ROOT / 'build/release/projects/sdf/cubey_project_sdf_cached_stroke_study')
    p.add_argument('--fixtures', action='store_true'); p.add_argument('--dump', action='store_true')
    p.add_argument('--mode', choices=['local', 'local-dom'], default='local-dom')
    p.add_argument('--groups', type=int, default=0)
    p.add_argument('--pilot', action='store_true')
    p.add_argument('--public-binary', type=Path, default=RAW/'baseline-sdf')
    args = p.parse_args(); args.output = args.output.resolve(); args.output.mkdir(parents=True, exist_ok=False)
    globals()[args.phase](args)


if __name__ == '__main__':
    main()
