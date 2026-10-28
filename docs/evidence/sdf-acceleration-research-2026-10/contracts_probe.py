#!/usr/bin/env python3
"""Independent 1D planning counterexamples; not Cubey renderer qualification."""
import json
import math


def smooth_min(a, b, k):
    h = min(1.0, max(0.0, .5 + .5 * (b-a)/k))
    return b*(1-h) + a*h - k*h*(1-h)


def main():
    # A cell-wide lower distance bound permits a single jump past the cell face.
    center, radius, zero, position = 0.0, 1.0, 3.0, 1.0
    center_value = center-zero
    bound = abs(center_value)-radius
    assert abs(center_value) > 2*radius
    assert position+bound == zero and bound > 0
    # Reuse the parent's already-certified bound, not its center sample with a
    # smaller child's radius: the latter can overestimate the available distance.
    child_radius = .5
    wrong = abs(center_value)-child_radius
    assert wrong > zero-position
    # 1-Lipschitz alone does not turn a field-value offset into that many world units.
    k = .4
    naive_extent = 1+k/4
    real_extent = 1+k/(4*.1)
    assert real_extent > naive_extent
    assert abs(smooth_min(.1*(real_extent-1), .1*(real_extent-1), k)) < 1e-14
    # Same child zero-set and a conservative/Lipschitz rescaling can still alter
    # a parent smooth blend, which depends on values rather than just signs.
    old_zero = 1+k/4
    low, high = 1.0, 1.4
    for _ in range(80):
        mid=(low+high)/2
        if smooth_min(.1*(mid-1),mid-1,k) < 0:
            low=mid
        else:
            high=mid
    new_zero=(low+high)/2
    assert new_zero-old_zero > .1
    # A root that fills the workspace is clipped by the final max(box) operator.
    # Certifying the unclipped source would incorrectly discard a clip-face cell.
    c = .4921875
    r = math.sqrt(3)/128
    unclipped = -1.0
    clipped = max(unclipped, abs(c)-.499)
    assert abs(unclipped) > 2*r and abs(clipped) < r
    print(json.dumps(dict(
        scope='Independent mathematical examples only; no runtime/performance qualification',
        global_step=dict(cell=[-1,1],current=position,bound=bound,next=position+bound,zero=zero),
        inherited_certificate=dict(safe_parent_bound=bound,wrong_recentered_bound=wrong,true_available=zero-position),
        world_padding=dict(field_lipschitz=.1,blend_radius=k,naive_extent=naive_extent,true_extent=real_extent),
        smooth_parent=dict(old_zero=old_zero,new_zero=new_zero,shift=new_zero-old_zero),
        root_clip=dict(center=c,radius=r,unclipped_value=unclipped,clipped_value=clipped),
        assertions_passed=True),indent=2))


if __name__ == '__main__':
    main()
