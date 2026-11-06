# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy==2.5.4"]
# ///
"""Independent double-precision GGX controls for the isolated native GPU probe.

Evaluation is written in terms of the microfacet D and Lambda equations. The
furnace and PDF integrals use solid-angle quadrature, never the ported sampler.
The separate normal-direction audit follows the pinned NVIDIA source mapping.
"""
from __future__ import annotations
import argparse
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import subprocess
import numpy as np

DIRECTION_LIMIT = 1e-5
ABSOLUTE_LIMIT, RELATIVE_LIMIT = 1e-6, 1e-4
INTEGRAL_LIMIT, REJECTION_LIMIT = .02, .025
F0 = np.array([.6795424696, .3864294338, .1384316150], dtype=np.float32).astype(float)


def normalize(v):
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def evaluate(v, l, roughness, regularized=True):
    """Correlated Smith reflection and the analytic VNDF reflection density."""
    h = normalize(v + l)
    nv, nl, nh = v[..., 2], l[..., 2], h[..., 2]
    alpha2 = roughness**4
    distribution = alpha2 / np.maximum(np.pi * (1 + nh*nh*(alpha2-1))**2, 1e-5)
    # G2 = 1/(1+Lambda(V)+Lambda(L)); V = G2/(4 NoV NoL).
    visibility = .5 / np.maximum(
        nl*np.sqrt(nv*nv + alpha2*(1-nv*nv)) +
        nv*np.sqrt(nl*nl + alpha2*(1-nl*nl)), 1e-5)
    fresnel = F0 + (1-F0)*np.clip(1-np.sum(v*h, axis=-1), 0, 1)[..., None]**5
    brdf = fresnel * (distribution*visibility)[..., None]
    exact_d = alpha2 / (np.pi * (np.sum(h[..., :2]**2, axis=-1) + alpha2*nh*nh)**2)
    if not regularized:
        denominator = (nl*np.sqrt(nv*nv+alpha2*np.sum(v[..., :2]**2,axis=-1))+
                       nv*np.sqrt(nl*nl+alpha2*np.sum(l[..., :2]**2,axis=-1)))
        brdf = fresnel*(exact_d*.5/np.maximum(denominator,1e-30))[...,None]
    g1 = 2*nv / (nv + np.sqrt(nv*nv + alpha2*np.sum(v[..., :2]**2, axis=-1)))
    pdf = exact_d * g1 / (4*nv)
    valid = (nv > 0) & (nl > 0) & (nh > 0)
    return np.where(valid[..., None], brdf, 0), np.where(valid, pdf, 0)


def donor_normal(v, roughness, xi):
    """Source-mapping audit, independent of the integration oracle below."""
    stretched = normalize(v*np.stack([roughness**2, roughness**2, np.ones_like(roughness)], -1))
    tangent = np.cross(stretched, [0, 0, 1])
    pole = stretched[..., 2] >= .99999
    tangent[pole] = [1, 0, 0]
    tangent = normalize(tangent)
    bitangent = np.cross(tangent, stretched)
    a = 1/(1+stretched[..., 2])
    first = xi[..., 1] < a
    phi = np.where(first, xi[..., 1]/a*np.pi,
                   np.pi + (xi[..., 1]-a)/np.maximum(1-a, 1e-30)*np.pi)
    p1 = np.sqrt(xi[..., 0])*np.cos(phi)
    p2 = np.sqrt(xi[..., 0])*np.sin(phi)*np.where(first, 1, stretched[..., 2])
    h = (p1[..., None]*tangent + p2[..., None]*bitangent +
         np.sqrt(np.maximum(0, 1-p1*p1-p2*p2))[..., None]*stretched)
    h[..., :2] *= roughness[..., None]**2
    h[..., 2] = np.maximum(h[..., 2], 0)
    return normalize(h)


@lru_cache(maxsize=4)
def gauss(order):
    return np.polynomial.legendre.leggauss(order)


def quadrature(cosine, roughness, order=512, regularized=True):
    # dOmega = dz dPhi. Integrate azimuth [0,pi] with endpoint-clustered
    # Gauss nodes around the grazing reflected peak, and use the y symmetry
    # of the BRDF/PDF. The two sun-cone masks remain separate below.
    nodes, weights = gauss(order)
    z, wz = (nodes+1)/2, weights/2
    phi, wphi = (nodes+1)*np.pi/2, weights*np.pi/2
    sine = np.sqrt(1-z*z)
    l = np.stack(np.broadcast_arrays(sine[:, None]*np.cos(phi),
                                     sine[:, None]*np.sin(phi), z[:, None]), -1)
    v = np.array([np.sqrt(1-cosine*cosine), 0, cosine])
    f, pdf = evaluate(v, l, roughness,regularized)
    measure = 2*wz[:, None]*wphi[None, :]
    energy = np.sum(f*l[..., 2, None]*measure[..., None], axis=(0, 1))
    mass = np.sum(pdf*measure)
    axis = normalize(np.array([.3, .4, .85]))
    inside_plus = np.sum(l*axis, -1) >= .95
    inside_minus = np.sum(l*axis*np.array([1,-1,1]), -1) >= .95
    cone_mask = (inside_plus.astype(float)+inside_minus.astype(float))/2
    sun = np.sum(f*l[..., 2, None]*cone_mask[..., None]*
                 3*measure[..., None], axis=(0, 1))
    return dict(energy=energy, mass=mass, sun=sun)


def score(path, width, height):
    actual = np.fromfile(path, dtype='<f4').reshape(height, width, 16, 4).astype(float)
    assert np.all(np.isfinite(actual)), 'nonfinite GPU observations'
    v, roughness = actual[..., 0, :3], actual[..., 0, 3]
    h, l = actual[..., 1, :3], actual[..., 2, :3]
    xi = np.stack([actual[..., 1, 3], actual[..., 2, 3]], -1)
    expected_h = donor_normal(v, roughness, xi)
    expected_l = 2*np.sum(v*expected_h, -1)[..., None]*expected_h-v
    direction_error = float(max(np.max(np.linalg.norm(h-expected_h, axis=-1)),
                                np.max(np.linalg.norm(l-expected_l, axis=-1))))
    assert direction_error <= DIRECTION_LIMIT, direction_error
    f, pdf = evaluate(v, l, roughness)
    expected = np.concatenate([f, pdf[..., None]], -1)
    difference = np.abs(actual[..., 3, :]-expected)
    allowance = ABSOLUTE_LIMIT + RELATIVE_LIMIT*np.abs(expected)
    assert np.all(difference <= allowance), ('arithmetic', float(np.max(difference/allowance)))
    sampled_f,sampled_pdf = evaluate(v,l,roughness,False)
    sampled_expected = np.concatenate([sampled_f,sampled_pdf[...,None]],-1)
    sampled_ratio = np.abs(actual[...,14,:]-sampled_expected)/(ABSOLUTE_LIMIT+RELATIVE_LIMIT*np.abs(sampled_expected))
    assert np.all(sampled_ratio <= 1), ('normalized arithmetic',float(np.max(sampled_ratio)))
    assert np.all(actual[..., 4, :3] >= 0), 'negative throughput'
    assert np.array_equal(actual[..., 4, 3] > .5, l[..., 2] > 0), 'rejection classification'
    assert np.all(actual[..., 4, :3][l[..., 2] <= 0] == 0), 'rejected samples were renormalized'
    axis = normalize(np.array([.3, .4, .85]))
    cones = actual[..., 5, :3]
    assert np.max(np.abs(np.linalg.norm(cones, axis=-1)-1)) < DIRECTION_LIMIT
    assert np.min(np.sum(cones*axis, -1)) >= .95-1e-6
    assert np.max(np.abs(actual[..., 5, 3] - 1/(2*np.pi*.05))) < 1e-5
    strata = actual[...,9,:2]
    assert np.all((strata > 0) & (strata < 1)), 'strata are open unit-square samples'
    for row in range(height):
        for begin in range(0,width,64):
            cells = np.floor(strata[row,begin:begin+64]*8).astype(int)
            assert len(np.unique(cells[:,0]+8*cells[:,1])) == 64, 'stratification omitted a cell'
    assert np.max(np.abs(actual[...,10,:3]@axis-actual[...,11,1])) < 1e-6
    assert np.max(np.abs(actual[...,11,2]-1)) < 2e-5, 'finite solar emitter irradiance'
    assert np.max(np.abs(actual[...,13,0]-1)) < 2e-5, 'solar rim samples carry finite irradiance'
    solar_radius = float(np.float32(.004675))
    solar_pdf = 1/(4*np.pi*np.sin(solar_radius/2)**2)
    assert np.max(np.abs(actual[...,10,3]-solar_pdf)/solar_pdf) < RELATIVE_LIMIT
    cases = []
    for row in range(height):
        r, cosine = float(roughness[row, 0]), float(v[row, 0, 2])
        # Very narrow analytic PDFs require a different quadrature coordinate.
        # Their full normal-incidence acceptance has a closed form instead.
        if r < .319 and cosine < .999:
            continue
        control = quadrature(cosine, r)
        coarse = quadrature(cosine, r, 256)
        regular = r >= .319
        if regular:
            assert abs(control['mass']-coarse['mass']) < .0025, ('quadrature mass stability', row)
            assert np.max(np.abs(control['energy']-coarse['energy'])) < .0025, ('quadrature energy stability', row)
        accepted = float(np.mean(actual[row, :, 4, 3]))
        mass = float(control['mass']) if regular else 1/(1+r**4)
        assert abs(accepted-mass) <= REJECTION_LIMIT, ('retained rejection mass', row, accepted, mass)
        sampled_energy = np.mean(actual[row, :, 4, :3], axis=0)
        assert np.max(np.abs(sampled_energy-control['energy'])) <= INTEGRAL_LIMIT, ('furnace', row)
        assert np.max(sampled_energy) <= 1.02, ('energy', row)
        sky = np.mean(actual[row, :, 7, :3], axis=0)
        sun = np.mean(actual[row, :, 8, :3], axis=0)
        if regular:
            assert np.max(np.abs(sky-control['energy']*[.2,.3,.4])) < INTEGRAL_LIMIT, ('mixture sky', row)
            assert np.max(np.abs(sun-control['sun'])) < INTEGRAL_LIMIT, ('mixture sun', row)
            assert np.max(np.abs(sky+sun-(control['energy']*[.2,.3,.4]+control['sun']))) < INTEGRAL_LIMIT, ('mixture combined', row)
        cases.append(dict(row=row, roughness=r, cosine=cosine,
                          accepted=accepted, integrated_mass=mass,
                          sampled_energy=sampled_energy.tolist(), integrated_energy=control['energy'].tolist(),
                          sky=sky.tolist(), sun=sun.tolist()))
        if regular:
            normalized_control = quadrature(cosine,r,regularized=False)
            normalized_sample = np.mean(actual[row,:,15,:3],axis=0)
            assert np.max(np.abs(normalized_sample-normalized_control['energy'])) <= INTEGRAL_LIMIT, ('normalized furnace',row)
            cases[-1].update(normalized_energy=normalized_sample.tolist(),
                             normalized_integral=normalized_control['energy'].tolist())
    # At normal incidence a near-delta, single-scatter conductor must approach
    # its F0. This catches applying the punctual D floor to a full integral.
    polished = np.mean(actual[56,:,15,:3],axis=0)
    assert np.max(np.abs(polished-F0)) < .005, ('polished furnace',polished,F0)
    return dict(passed=True, samples=width*height, direction_error=direction_error,
                maximum_arithmetic_tolerance_ratio=float(np.max(difference/allowance)),
                normalized_arithmetic_tolerance_ratio=float(np.max(sampled_ratio)),
                polished_furnace=polished.tolist(),
                arithmetic_absolute_limit=ABSOLUTE_LIMIT, arithmetic_relative_limit=RELATIVE_LIMIT,
                direction_limit=DIRECTION_LIMIT, integral_limit=INTEGRAL_LIMIT,
                rejection_limit=REJECTION_LIMIT, integral_cases=cases,
                stratified_blocks=height*(width//64),
                solar_irradiance_error=float(np.max(np.abs(actual[...,11,2]-1))),
                solar_rim_irradiance_error=float(np.max(np.abs(actual[...,13,0]-1))),
                solar_rim_membership_rounding=int(np.count_nonzero(actual[...,12,3]==0)),
                scope='Uncompensated GGX, analytic sky/cone controls; no full workshop/atmosphere qualification')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('build/dev/projects/raytracing/raytracing'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    binary, output = args.binary.resolve(), args.output.resolve()
    width, height = 2048, 64
    command = [str(binary), '--headless', '--frames', '1', '--width', str(width), '--height', str(height),
               '--require-validation', '--bsdf-contract', '--optics-output', str(output/'probe.bin'),
               '--output', str(output/'probe.png'), '--metrics', str(output/'metrics.json')]
    run = subprocess.run(command, capture_output=True, text=True, timeout=180,
                         env=dict(os.environ, VK_LAYER_VALIDATE_SYNC='1'))
    log = run.stdout+run.stderr
    (output/'native.log').write_text(log)
    assert run.returncode == 0 and not any(t in log.lower() for t in
                                         ['vuid-', 'hazard detected', 'vulkan validation error']), log
    result = score(output/'probe.bin', width, height)
    result.update(command=command, binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
    shader = binary.parent/'shaders/trace.rgen.spv'
    result['shader_sha256'] = hashlib.sha256(shader.read_bytes()).hexdigest()
    result['source_base'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    result['source_files_sha256'] = {
        str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
        [Path('projects/raytracing/shaders/material_bsdf.glsl'),
         Path('projects/raytracing/shaders/material_workshop.glsl'),
         Path('projects/raytracing/shaders/trace.rgen'), Path(__file__)]}
    (output/'receipt.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k: v for k, v in result.items() if k not in ['command', 'integral_cases']}, indent=2))


if __name__ == '__main__':
    main()
