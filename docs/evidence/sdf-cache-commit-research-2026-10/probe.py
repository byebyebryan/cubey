#!/usr/bin/env python3
"""CPU models and pinned-receipt analysis for the cache-commit research pass.

No native executable, GPU workload, candidate builder or editor is run. Model
success is not native R16F, cache-equivalence or latency qualification.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import statistics
import struct
import subprocess


def half(x):
    return struct.unpack("<e", struct.pack("<e", x))[0]


def half_bits(x):
    return struct.pack("<e", x).hex()


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    rng = random.Random(20261030)
    cases = {}

    # Selection commutes with an idempotent monotone quantizer at identical
    # sample knots. Python half uses round-to-nearest-even; Vulkan image-store
    # conversion, shader evaluation and signed-zero ties need native checks.
    pairs = [(f32(rng.uniform(-2, 2)), f32(rng.uniform(-2, 2))) for _ in range(100000)]
    edges = [0., -0., 2**-25, -2**-25, 2**-24, -2**-24,
             2**-14, -2**-14, 1 + 2**-11, -(1 + 2**-11), 65504., -65504.]
    pairs.extend((f32(a), f32(b)) for a in edges for b in edges)
    numeric_differences = bit_differences = 0
    bit_examples = []
    for a, b in pairs:
        for op in (min, max):
            expected, reused = half(op(a, b)), half(op(half(a), b))
            numeric_differences += expected != reused
            if half_bits(expected) != half_bits(reused):
                bit_differences += 1
                assert expected == 0 and reused == 0
                bit_examples.append(dict(a=a,b=b,operation=op.__name__,
                    expected_bits=half_bits(expected),reused_bits=half_bits(reused)))
    assert numeric_differences == 0
    chain_differences = 0
    for _ in range(2000):
        exact = f32(rng.uniform(-.5, .5))
        reused = half(exact)
        for i in range(64):
            tool = f32(rng.uniform(-.5, .5))
            op = min if i % 2 == 0 else max
            operand = tool if i % 2 == 0 else -tool
            exact, reused = op(exact, operand), half(op(reused, operand))
        chain_differences += half(exact) != reused
    assert chain_differences == 0
    cases["fixed_knot_hard_selection"] = dict(
        pair_evaluations=len(pairs)*2, numeric_differences=numeric_differences,
        bit_differences_including_zero_ties=bit_differences,
        bit_difference_examples=bit_examples,
        chains=2000, operations_per_chain=64, chain_numeric_differences=chain_differences,
        assumptions="Same lattice positions, finite values, same operand evaluation, deterministic monotone idempotent quantizer; hard min/max only",
        limit="Scalar model only. Signed zero, subnormals, format conversion and GPU compilation must be qualified natively; no smooth-edit or interpolated-base claim")

    # Clipped base can be reused for union only if the tool receives the same
    # workspace clip. A cutter must remain raw for hard subtraction.
    clip_differences = 0
    for _ in range(20000):
        a, b, c = (f32(rng.uniform(-1, 1)) for _ in range(3))
        base = half(max(a, c))
        clip_differences += half(max(min(a, b), c)) != half(min(base, max(b, c)))
        clip_differences += half(max(a, -b, c)) != half(max(base, -b))
    assert clip_differences == 0
    a, b, c = -.2, -.1, .0005
    correct, wrong = max(a, -b, c), max(max(a, c), -max(b, c))
    assert correct != wrong
    cases["workspace_composition"] = dict(
        model_evaluations=40000, numeric_differences=clip_differences,
        union="min(cached_clipped_base, max(raw_tool, workspace_clip))",
        subtraction="max(cached_clipped_base, -raw_tool)",
        clipped_cutter_counterexample=dict(base=a, tool=b, clip=c, correct=correct, wrong=wrong))

    # Reusing interpolated base is a different function from combining knots
    # and interpolating afterward. This can alter sign, without any rounding.
    t, a0, a1, b = .75, -1., 1., .1
    live = min((1-t)*a0+t*a1, b)
    baked = (1-t)*min(a0, b)+t*min(a1, b)
    assert live > 0 and baked < 0
    cases["interpolation_does_not_commute"] = dict(
        position=t, base_knots=[a0, a1], tool=b, live=live, baked=baked,
        meaning="A texel-equivalent commit does not establish analytical-preview/bake equivalence")

    # Sign-only old cells cannot provide the exact operand for new bricks.
    a1, a2, b = -.01, -.2, .05
    r1, r2 = max(a1, -b), max(a2, -b)
    assert a1 < 0 and a2 < 0 and r1 != r2
    cases["uniform_label_is_not_a_scalar"] = dict(
        two_filled_values=[a1,a2], cutter=b, canonical_results=[r1,r2],
        sentinel_result=max(-1000., -b),
        meaning="Newly allocated bricks need canonical samples or a valid dominance certificate; filled/empty sentinels are insufficient")

    add = dict(point=.2, base=abs(.2)-.1, tool=abs(.2-.3)-.05, brush_bounds=[.25,.35])
    cut = dict(point=.1, base=abs(.1)-.3, tool=abs(.1)-.05, brush_bounds=[-.05,.05])
    add["edited"], cut["edited"] = min(add["base"],add["tool"]), max(cut["base"],-cut["tool"])
    assert add['edited'] != add['base'] and cut['edited'] != cut['base']
    cases["primitive_bounds_do_not_bound_scalar_changes"] = dict(add=add,cut=cut)

    # A full regenerated topology can safely assign new physical indices only
    # when old data is fetched by logical cell and copied to the new slot.
    old_slots, old_values, new_slots = {"A":0,"B":1}, [.1,-.2], {"A":1,"B":0}
    copied = [None,None]
    for cell, slot in new_slots.items(): copied[slot] = old_values[old_slots[cell]]
    assert copied == [-.2,.1] and old_values[new_slots['A']] != old_values[old_slots['A']]
    cases["compact_atlas_remapping"] = dict(old_slots=old_slots,new_slots=new_slots,
        old_values=old_values,correct_new_values=copied,
        meaning="Full hierarchy plus explicit coordinate-based copying needs no persistent allocator; skipping writes at changed atlas indices is invalid")

    evidence = root/'docs/evidence/sdf-cached-one-edit-2026-10'
    sequence = json.loads((evidence/'sequences.json').read_text())
    costs = []
    for subject in ('guardian','robot'):
        runs = [r for r in sequence['runs'] if r['subject']==subject]
        commits = [c for r in runs for c in r['native']['commits'] if c['published']]
        keys = ('allocation_ms','filter_gpu_ms','brick_gpu_ms','field_build_ms',
                'publication_prepare_ms','ready_to_render_ms','to_completed_replacement_frame_ms')
        costs.append(dict(subject=subject,samples=len(commits),
            median_components_ms={k:statistics.median(c[k] for c in commits) for k in keys},
            program=runs[0]['native']['programs'][0],
            memory_payload_bytes=commits[0]['peak_field_payload_bytes']))
    old = json.loads((root/'docs/evidence/sdf-tracing-comparison-2026-10/summary.json').read_text())
    prune_costs = [dict(scene=r['scene'],axis=r['axis'],resources=r['resources'])
                   for r in old['frame_summary'] if r['scene']=='guardian' and r['mode']=='analytic']

    protected = {}
    for pattern in ('*.cpp','*.h','*.glsl','*.comp','*.frag','*.vert','CMakeLists.txt'):
        for p in (root/'projects/sdf').rglob(pattern): protected[str(p.relative_to(root))]=sha(p)
    for rel in ('include/cubey/engine/project_gpu_services.h','include/cubey/vulkan/submission_tickets.h'):
        protected[rel]=sha(root/rel)
    donor = Path('/home/bryan/code/projectR')
    donor_files = {rel:sha(donor/rel) for rel in ('Assets/Shaders/BrickEngineBuilder.compute',
                    'Assets/Shaders/BrickEngineCompute.compute')}
    result=dict(passed=True,scope=__doc__.strip(),seed=20261030,
        intake_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        cases=cases,costs=dict(source_head=sequence['source_head'],device=sequence['device'],
            receipt_sha256=sha(evidence/'sequences.json'),subjects=costs,
            limit='Reanalysis of previous native runs; no new GPU timing'),
        historical_exact_region_preparation=dict(source='docs/evidence/sdf-tracing-comparison-2026-10/summary.json',
            receipt_sha256=sha(root/'docs/evidence/sdf-tracing-comparison-2026-10/summary.json'),
            rows=prune_costs,limit='Older analytical-renderer setup, not a new field-builder measurement or speed prediction'),
        sources=protected,projectr_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=donor,text=True).strip(),
        projectr_sources=donor_files,probe_sha256=sha(Path(__file__)))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(passed=True,cases=len(cases),fixed_knot=cases['fixed_knot_hard_selection'],
        protected_sources=len(protected),scope=result['scope']),indent=2))


if __name__ == '__main__':
    main()
