#!/usr/bin/env python3
"""Independent, deterministic M0 oracle. Python stdlib; no production SDF evaluator.

write INPUT freezes cases and numeric contracts before GPU execution.
check INPUT RESULT scores only those contracts (sampled is a diagnostic control).
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

H = 1 / (256 * 7)
EPS = H / 32


def add(a, b):
    return [x + y for x, y in zip(a, b)]


def mul(a, s):
    return [x * s for x in a]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def unit(a):
    return mul(a, 1 / math.sqrt(dot(a, a)))


def sphere(name, r, p=(0, 0, 0)):
    return dict(name=name, type="sphere", radius=r, position=list(p))


def document(root):
    return dict(format="cubey.sdf.scene", version=2, name="tracing comparison",
                materials=[dict(name="red", base_color=[1, 0, 0], roughness=.5),
                           dict(name="blue", base_color=[0, 0, 1], roughness=.7)], root=root)


def sphere_ray(o, d, r, center=(0, 0, 0)):
    v = add(o, mul(center, -1))
    b = dot(v, d)
    disc = b*b - dot(v, v) + r*r
    if disc < -1e-14:
        return None
    a, z = -b - math.sqrt(max(0, disc)), -b + math.sqrt(max(0, disc))
    return a if a >= 0 else z if z >= 0 else None


def box_ray(o, d, extent):
    enter, leave, axis, sign = -math.inf, math.inf, 0, 1
    exit_axis, exit_sign = 0, 1
    for i in range(3):
        if abs(d[i]) < 1e-15:
            if abs(o[i]) > extent[i]:
                return None, None
            continue
        a, b = (-extent[i] - o[i]) / d[i], (extent[i] - o[i]) / d[i]
        lo, hi = min(a, b), max(a, b)
        if lo > enter:
            enter, axis, sign = lo, i, -1 if d[i] > 0 else 1
        if hi < leave:
            leave, exit_axis, exit_sign = hi, i, 1 if d[i] > 0 else -1
    if enter > leave or leave < 0:
        return None, None
    n = [0, 0, 0]
    n[axis if enter >= 0 else exit_axis] = sign if enter >= 0 else exit_sign
    return max(enter, 0) if enter >= 0 else leave, n


def ray(o, d, t, normal=None, tolerance=4*EPS, label="regular"):
    q = dict(origin=o, direction=unit(d), kind="ray", expected_t=t,
             depth_tolerance=tolerance, classification=label)
    if normal is not None:
        q["expected_normal"] = normal
    return q


def fixtures():
    cases = []

    def balls(name, root, r, center=(0, 0, 0), tiny=False):
        qs = []
        for v in ([0, 0, 1], [1, 0, 0], [1, 2, 3], [-2, 1, .5]):
            d = unit(mul(v, -1))
            o = add(center, mul(d, -.7 if not tiny else -8*H))
            t = sphere_ray(o, d, r, center)
            n = unit(add(add(o, mul(d, t)), mul(center, -1)))
            qs.append(ray(o, d, t, n))
        qs.append(ray(list(center), [0, 0, 1], r, [0, 0, 1]))
        # Miss has a clearance much larger than the hit tolerance, even when tiny.
        qs.append(ray(add(center, [r + 8*EPS, 0, .7]), [0, 0, -1], None))
        qs.extend(dict(kind="point", position=add(center, p), expected_distance=d)
                  for p, d in [([0, 0, 0], -r), ([0, 0, r], 0), ([2*r, 0, 0], r)])
        cases.append(dict(name=name, scene=document(root), queries=qs))

    balls("sphere-control", sphere("ball", .2), .2)
    balls("subgrid-sphere", sphere("ball", .35*H, [.5*H]*3), .35*H, [.5*H]*3, True)
    balls("aligned-small-sphere", sphere("ball", .35*H), .35*H, tiny=True)
    for k in (.0001, .01, .1):
        root = dict(name="blend", type="smooth-union", radius=k,
                    children=[sphere("a", .15), sphere("b", .15)])
        balls(f"smooth-identical-{k}", root, .15+k/4)
    # Different bracketings of identical radial fields have different offsets.
    def sm(a, b, k=.1):
        return min(a, b) - max(0, k-abs(a-b))**2/(4*k)
    for left in (True, False):
        a, b, c = sphere("a", .15), sphere("b", .15), sphere("c", .1)
        def blend(name, x, y):
            return dict(name=name, type="smooth-union", radius=.1, children=[x, y])
        root = blend("outer", blend("inner", a, b), c) if left else blend("outer", a, blend("inner", b, c))
        r = -sm(sm(-.15, -.15), -.1) if left else -sm(-.15, sm(-.15, -.1))
        balls(f"nested-smooth-{'left' if left else 'right'}", root, r)
    for degrees in (0, 23, 45):
        angle = math.radians(degrees)
        def rotate(p):
            return [math.cos(angle)*p[0]+math.sin(angle)*p[2], p[1],
                    -math.sin(angle)*p[0]+math.cos(angle)*p[2]]
        for phase in (0, .5*H):
            extent = [.22, .13, .08]
            root = dict(name="slab", type="box", half_extent=extent,
                        rotation_xyzw=[0, math.sin(angle/2), 0, math.cos(angle/2)], position=[phase]*3)
            qs = []
            for o, d in [([0, 0, .7], [0, 0, -1]), ([0, 0, 0], [1, 0, 0]),
                         ([.05, .02, .7], [0, 0, -1]), ([.4, 0, .7], [0, 0, -1])]:
                t, n = box_ray(o, d, extent)
                qs.append(ray(add(rotate(o), [phase]*3), rotate(d), t, rotate(n) if n else None))
            for o, d in [([0, 0, .7], unit([1e-7, 1e-8, -1])),
                         ([.125, .03125, .7], unit([1e-5, 1e-7, -1]))]:
                t, n = box_ray(o, d, extent)
                qs.append(ray(add(rotate(o), [phase]*3), rotate(d), t, rotate(n) if n else None,
                              label="near-parallel components"))
            cases.append(dict(name=f"box-{degrees}-{phase:.8g}", scene=document(root), queries=qs))
    for width in (.5*H, 1.5*H, 4*H):
        root = dict(name="stick", type="box", half_extent=[width/2, .2, width/2],
                    rotation_xyzw=[0, math.sin(math.radians(23)/2), 0, math.cos(math.radians(23)/2)])
        a = math.radians(23)
        n = [math.sin(a), 0, math.cos(a)]
        qs = [ray(mul(n, .01), mul(n, -1), .01-width/2, n),
              ray([0, .3, 0], [0, -1, 0], .1, [0, 1, 0])]
        cases.append(dict(name=f"thin-stick-{width/H:g}h", scene=document(root), queries=qs))
    # Shell interval subtraction; center starts in the cavity, then in solid.
    for cut in (False, True):
        root = dict(name="shell", type="subtract", children=[sphere("body", .3), sphere("cutter", .1)])
        if cut:
            root["cut_material"] = "blue"
        qs = [ray([0, 0, .7], [0, 0, -1], .4, [0, 0, 1]),
              ray([0, 0, 0], [0, 0, 1], .1, [0, 0, -1]),
              ray([0, 0, .2], [0, 0, 1], .1, [0, 0, 1])]
        cases.append(dict(name=f"cut-owner-{cut}", scene=document(root), queries=qs))
    # A half-grid cavity is absent from trilinear corner signs just like the sphere.
    center = [.5*H]*3
    root = dict(name="body-cut", type="subtract", children=[sphere("body", .2), sphere("hole", .35*H, center)])
    cases.append(dict(name="subgrid-cavity", scene=document(root), queries=[
        ray(center, [0, 0, 1], .35*H, [0, 0, -1])]))
    balls("uniform-group", dict(name="group", type="group", scale=2,
                               position=[.1, 0, 0], rotation_xyzw=[0, math.sin(math.radians(23)/2), 0, math.cos(math.radians(23)/2)],
                               children=[sphere("child", .1)]), .2, [.1, 0, 0])
    # All seven primitives get independent axial known answers, away from creases.
    for typ, params, surface in [
        ("capsule", dict(radius=.1, half_length=.2), .3),
        ("cylinder", dict(radius=.1, half_length=.2), .2),
        ("torus", dict(major_radius=.2, minor_radius=.05), .05),
        ("capped-cone", dict(bottom_radius=.15, top_radius=.05, half_length=.2), .2),
        ("round-cone", dict(bottom_radius=.15, top_radius=.05, half_length=.2), .25)]:
        root = dict(name="primitive", type=typ, **params)
        x = .2 if typ == "torus" else 0
        qs = [ray([x, .7, 0], [0, -1, 0], .7-surface, [0, 1, 0]),
              dict(kind="point", position=[x, surface+.01, 0], expected_distance=.01)]
        cases.append(dict(name=typ, scene=document(root), queries=qs))
    # Predeclared tangent bracket derives from residual <= epsilon, not a widened regular gate.
    root = sphere("tangent-ball", .2)
    bracket = math.sqrt(2*.2*EPS + EPS*EPS)
    cases.append(dict(name="sphere-tangent", scene=document(root), queries=[
        ray([.2, 0, .7], [0, 0, -1], .7, tolerance=bracket+4e-6, label="tangent"),
        ray([.2+8*EPS, 0, .7], [0, 0, -1], None)]))
    for k in (.0001, .01, .1):
        root = dict(name="smooth-cut", type="smooth-subtract", radius=k,
                    children=[sphere("body", .3), sphere("cutter", .1)])
        cases.append(dict(name=f"smooth-subtract-{k}", scene=document(root), queries=[
            dict(kind="point", position=[0, 0, .2], expected_distance=-.1+k/4),
            ray([0, 0, .7], [0, 0, -1], .4, [0, 0, 1]),
            ray([0, 0, 0], [0, 0, 1], .1, [0, 0, -1])]))
    cases.append(dict(name="workspace-clip", scene=document(sphere("clipped", .1, [0, 0, .45])),
                      queries=[ray([0, 0, .7], [0, 0, -1], .201, [0, 0, 1]),
                               ray([0, 0, .498], [0, 0, 1], .001, [0, 0, 1])]))
    root = dict(name="cross", type="union", children=[
        dict(name="x", type="box", half_extent=[.2, H/4, H/4]),
        dict(name="y", type="box", half_extent=[H/4, .2, H/4])])
    cases.append(dict(name="crossed-thin-sticks", scene=document(root), queries=[
        ray([0, 0, .01], [0, 0, -1], .01-H/4, [0, 0, 1]),
        ray([.1, .1, .01], [0, 0, -1], None)]))
    assert len(cases) <= 32
    return dict(format="cubey.sdf.tracing-study", version=1, fixture_revision=2,
                contracts=dict(h0=H, epsilon=EPS, normal_delta=H/16,
                               regular_depth_tolerance=4*EPS, normal_degrees=1,
                               point_tolerance=2e-6, max_steps=4096,
                               ambiguity_roster=["sphere-tangent:0 (declared depth bracket only)"]), cases=cases)


def check(manifest, result):
    expected = {c["name"]: c for c in manifest["cases"]}
    failures, scored = [], 0
    maxima = dict(depth=0., point=0., normal_degrees=0., residual=0.)
    for item in result["cases"]:
        case = expected[item["name"]]
        if len(case["queries"]) != len(item["results"]):
            raise ValueError("incomplete query result")
        for i, (q, a) in enumerate(zip(case["queries"], item["results"])):
            if item["mode"] == "sampled":
                continue
            label = f'{item["mode"]}/{item["name"]}/{i}'
            scored += 1
            if q["kind"] == "point":
                err = abs(a["distance"]-q["expected_distance"])
                maxima["point"] = max(maxima["point"], err)
                if err > manifest["contracts"]["point_tolerance"]:
                    failures.append([label, "point", err])
                continue
            want = q["expected_t"]
            if a["status"] != (0 if want is None else 1):
                failures.append([label, "status", a["status"]])
            elif want is not None:
                err = abs(a["t"]-want)
                maxima["depth"] = max(maxima["depth"], err)
                maxima["residual"] = max(maxima["residual"], abs(a["residual"]))
                if err > q["depth_tolerance"] or abs(a["residual"]) > EPS+2e-6:
                    failures.append([label, "depth/residual", err, a["residual"]])
                if "expected_normal" in q:
                    angle = math.degrees(math.acos(max(-1, min(1, dot(unit(a["normal"]), q["expected_normal"])))))
                    maxima["normal_degrees"] = max(maxima["normal_degrees"], angle)
                    if angle > 1:
                        failures.append([label, "normal", angle])
    # Coverage is part of acceptance; a truncated receipt cannot pass.
    modes = sorted({c["mode"] for c in result["cases"]})
    for mode in modes:
        names = [c["name"] for c in result["cases"] if c["mode"] == mode]
        if sorted(names) != sorted(expected):
            failures.append([mode, "missing or duplicate fixture"])
    if not modes or scored == 0:
        failures.append(["no candidate results"])
    return dict(passed=not failures, scored=scored, modes=modes, maxima=maxima, failures=failures)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=["write", "check"])
    p.add_argument("input", type=Path)
    p.add_argument("result", type=Path, nargs="?")
    args = p.parse_args()
    if args.action == "write":
        text = json.dumps(fixtures(), indent=2) + "\n"
        args.input.parent.mkdir(parents=True, exist_ok=True)
        args.input.write_text(text)
        print(json.dumps(dict(cases=len(fixtures()["cases"]), sha256=hashlib.sha256(text.encode()).hexdigest())))
    else:
        report = check(json.loads(args.input.read_text()), json.loads(args.result.read_text()))
        print(json.dumps(report, indent=2))
        raise SystemExit(0 if report["passed"] else 1)


if __name__ == "__main__":
    main()
