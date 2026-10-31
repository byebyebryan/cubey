"""Private recorded-input segmentation; accepted spans are never rewritten."""
import math


def error(poses, start, end):
    first, last = poses[start], poses[end]
    duration = last['t']-first['t']
    if duration <= 0:
        raise ValueError('pose timestamps must strictly increase')
    combined = center = radius = 0.0
    for pose in poses[start:end+1]:
        t = (pose['t']-first['t'])/duration
        fitted = [a+(b-a)*t for a, b in zip(first['p'], last['p'])]
        e = math.dist(pose['p'], fitted)
        r = abs(pose['r']-(first['r']+(last['r']-first['r'])*t))
        center, radius, combined = max(center, e), max(radius, r), max(combined, e+r)
    return combined, center, radius


def segment(poses, tolerance):
    """Greedy provisional tip. A failing extension freezes its last valid tip.

    Position and radius interpolate linearly in recorded time. On each input
    span their difference from a candidate is affine; norm(position error) plus
    absolute radius error is convex. Checking all knots bounds between them too.
    """
    if tolerance <= 0 or len(poses) < 2:
        raise ValueError('invalid segmentation input')
    for i, pose in enumerate(poses):
        if not all(math.isfinite(v) for v in [pose['t'], *pose['p'], pose['r']]) or pose['r'] < 0:
            raise ValueError('nonfinite or negative input')
        if i and pose['t'] <= poses[i-1]['t']:
            raise ValueError('unordered input')
    mandatory = {0, len(poses)-1}
    for i in range(1, len(poses)-1):
        if poses[i].get('corner') or poses[i].get('boundary'):
            mandatory.add(i)
        before, after = poses[i]['r']-poses[i-1]['r'], poses[i+1]['r']-poses[i]['r']
        if before*after < 0 or ((before == 0) != (after == 0)):
            mandatory.add(i)
    spans = []
    start = 0
    for end in range(1, len(poses)):
        if error(poses, start, end)[0] > tolerance:
            # An adjacent original segment is exact under this contract.
            if end-1 == start:
                raise AssertionError('adjacent span failed its own interpolation')
            spans.append((start, end-1))
            start = end-1
        if end in mandatory:
            spans.append((start, end))
            start = end
    return spans


def validate(poses, spans, tolerance):
    if spans[0][0] != 0 or spans[-1][1] != len(poses)-1:
        raise AssertionError('lost endpoint')
    for previous, current in zip(spans, spans[1:]):
        assert previous[1] == current[0]
    maximum = center = radius = 0.0
    # Independent dense reconstruction at original time knots and eight points
    # between each pair checks the implementation of the interpolation contract.
    for start, end in spans:
        a, b = poses[start], poses[end]
        for j in range(start, end):
            for k in range(9):
                u = k/8
                p, q = poses[j], poses[j+1]
                t = p['t']+(q['t']-p['t'])*u
                v = (t-a['t'])/(b['t']-a['t'])
                wanted = [x+(y-x)*u for x, y in zip(p['p'], q['p'])]
                got = [x+(y-x)*v for x, y in zip(a['p'], b['p'])]
                pe = math.dist(wanted, got)
                re = abs(p['r']+(q['r']-p['r'])*u-(a['r']+(b['r']-a['r'])*v))
                maximum, center, radius = max(maximum, pe+re), max(center, pe), max(radius, re)
    assert maximum <= tolerance+1e-12
    return dict(max_combined_error=maximum, max_center_error=center, max_radius_error=radius)
