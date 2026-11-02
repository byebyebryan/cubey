#!/usr/bin/env python3
"""Research-only replay arithmetic over saved native receipts; no GPU execution."""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[3]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pack(units, budget, estimate):
    """Optimistic sequential windows: no CPU gaps or frame/submission overhead.

    Admission tests the estimate but charges the recorded completed unit wall
    cost. Even a perfect estimate here is a counterfactual, not a new timing.
    """
    windows, used, underestimate = 1, 0.0, 0
    for unit in units:
        predicted = estimate(unit)
        if predicted > budget or unit['wall_ms'] > budget:
            return dict(windows=None, blocked=True, underestimated_units=underestimate)
        if used + predicted > budget:
            windows += 1; used = 0.0
        if unit['wall_ms'] > predicted:
            underestimate += 1
        used += unit['wall_ms']
        if used > budget:
            return dict(windows=None, blocked=False, overrun=True, underestimated_units=underestimate)
    return dict(windows=windows, blocked=False, overrun=False, underestimated_units=underestimate)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).with_name('analysis.json'))
    args = parser.parse_args()
    raw = ROOT/'outputs/sdf/interactive-edit-loop-20261031/qualified-v1'
    paths = [raw/'service/service.json', raw/'post-controls/controls.json']
    service, post = [json.loads(p.read_text()) for p in paths]
    assert service['complete'] and post['complete']
    runs = service['runs']
    validation_enabled = all('--require-validation' in r['command'] and
                             '--no-validation' not in r['command'] for r in runs)
    assert validation_enabled
    records = []
    for row in runs:
        native = row['native']
        if not native['drained']:
            continue  # cancelled partial jobs are not complete-work lower bounds
        units, budget = native['units'], row['budget_ms']
        key = lambda u: (u['kind'], u['level'], u['groups'])
        training = defaultdict(list)
        for other in runs:
            if other is row or (other['subject'], other['operation'], other['budget_ms']) != (row['subject'], row['operation'], budget):
                continue
            if other['native']['drained']:
                for u in other['native']['units']:
                    training[key(u)].append(u['wall_ms'])
        bounds = native['frozen_unit_wall_bounds_ms']
        def detailed(u):
            return max(training[key(u)])*1.25+.05 if training[key(u)] else bounds[u['kind']]
        wall = sum(u['wall_ms'] for u in units)
        gpu = sum(u['gpu_ms'] for u in units)
        records.append(dict(subject=row['subject'], operation=row['operation'], budget_ms=budget,
            repeat=row['repeat'], complete_unit_wall_ms=wall, complete_unit_gpu_ms=gpu,
            preparation_ms=native['worker_preparation_ms'], admission_ms=native['build']['admission_ms'],
            replacement_completed_ms=native['replacement_completed_ms'],
            minimum_windows_from_total_wall=math.ceil(wall/budget),
            actual_work_windows=len({u['frame'] for u in units}),
            perfect_cost_next_fit=pack(units,budget,lambda u:u['wall_ms']),
            same_kind_estimator_no_gap_replay=pack(units,budget,lambda u:bounds[u['kind']]),
            held_out_detailed_estimator_no_gap_replay=pack(units,budget,detailed)))
    groups = defaultdict(list)
    for record in records:
        groups[(record['subject'],record['operation'],record['budget_ms'])].append(record)
    cohorts = []
    for (subject, operation, budget), items in sorted(groups.items()):
        cohorts.append(dict(subject=subject,operation=operation,budget_ms=budget,
            median_unit_wall_ms=statistics.median(r['complete_unit_wall_ms'] for r in items),
            median_unit_gpu_ms=statistics.median(r['complete_unit_gpu_ms'] for r in items),
            median_preparation_ms=statistics.median(r['preparation_ms'] for r in items),
            median_admission_ms=statistics.median(r['admission_ms'] for r in items),
            median_replacement_ms=statistics.median(r['replacement_completed_ms'] for r in items),
            all_complete_wall_exceeds_four_windows=all(r['complete_unit_wall_ms']>4*budget for r in items),
            minimum_windows=[r['minimum_windows_from_total_wall'] for r in items],
            perfect_next_fit_windows=[r['perfect_cost_next_fit']['windows'] for r in items],
            observed_work_windows=[r['actual_work_windows'] for r in items]))
    alternatives = []
    for control in post['controls']:
        if control['kind']!='1024-service-calibration':continue
        n=control['native'];budget=n['budget_ms'];wall=sum(u['wall_ms'] for u in n['units'])
        alternatives.append(dict(subject='guardian',operation=control['operation'],groups=1024,
            budget_ms=budget,unit_wall_ms=wall,unit_gpu_ms=sum(u['gpu_ms'] for u in n['units']),
            minimum_windows_from_total_wall=math.ceil(wall/budget),
            perfect_cost_next_fit=pack(n['units'],budget,lambda u:u['wall_ms']),
            replacement_completed_ms=n['replacement_completed_ms']))
    # Mathematical counterexample to retaining globally exact center scalars
    # merely because the old surface/topology did not change in a local mask.
    point = (230.5/256)-.5  # actual finest-level lattice center coordinate
    transverse = (128.5/256)-.5
    old_center, new_center = 0.0, point-.1
    old_distance=abs(point-old_center)-.1
    tool_distance=abs(point-new_center)-.01
    new_distance=min(old_distance,tool_distance)
    node_radius=math.sqrt(3)/(2*256)
    assert new_distance!=old_distance and min(old_distance,new_distance)>node_radius
    center_counterexample=dict(kind='ideal arithmetic, not an existing native bug',
        point=[point,transverse,transverse],
        old_sphere=dict(center=[old_center,transverse,transverse],radius=.1),
        added_sphere=dict(center=[new_center,transverse,transverse],radius=.01),
        old_scalar=old_distance,new_scalar=new_distance,node_radius=node_radius,
        old_and_new_classification='empty',
        consequence='Unchanged topology does not establish validity of an exact scalar at the new source version.')
    # Float centers on the four existing classification levels, plus one
    # validity bit per center; no allocator or driver overhead is claimed.
    centers=sum(2**(3*depth) for depth in (2,4,6,8))
    per_field=4*centers+math.ceil(centers/8)
    geometry=max(r['native']['geometry_payload_bytes'] for r in runs)
    diagnostic=19173961*4  # existing dense node readback; source-pinned Field layout
    memory=dict(classification_centers=centers,bytes_per_field_values_and_validity=per_field,
        two_field_increment_bytes=2*per_field,baseline_geometry_bytes=geometry,
        largest_existing_diagnostic_bytes=diagnostic,
        analytic_total_bytes=geometry+diagnostic+2*per_field,
        remaining_below_two_gib_bytes=2**31-(geometry+diagnostic+2*per_field),
        dense_u32_epoch_increment_bytes=2*4*centers,
        analytic_total_with_dense_epochs_bytes=geometry+diagnostic+2*per_field+2*4*centers,
        limitation='Arithmetic screen only; value-plus-validity estimate excludes epochs. Both estimates exclude work lists, extra copies, renderer and driver allocation. Not a native memory result.')
    coarse=sum(2**(3*depth) for depth in (2,4,6))
    compact=coarse+64**3
    memory['classification_only_candidate']=dict(coarse_dense_entries=coarse,
        finest_entries_indexed_by_existing_brick_slot=64**3,
        scalar_and_validity_bytes_per_field=4*compact+math.ceil(compact/8),
        limitation='Proposed SUBDIVIDED-only center state, not arbitrary exact scalars. Requires initialization, locality-dominance and exposed-child/F32 proofs. Not implemented or measured.')
    result=dict(format='cubey.sdf.interactive-edit-solution-research',version=1,
        source_head='6e9bce4e',analysis_sha256=sha(Path(__file__)),
        retained_service_requires_validation=validation_enabled,
        inputs=[dict(path=str(p.relative_to(ROOT)),sha256=sha(p)) for p in paths],
        assumptions=['No new GPU timing or implementation.',
            'Recorded unit wall costs belong to the sequential submit-and-wait protocol.',
            'Offline packing omits readiness gaps, callback/profiling costs, drawing, publication, and scheduling noise.',
            'Held-out prediction uses two repetitions of the same subject/operation; it is not a worst-case duration proof.'],
        records=records,cohorts=cohorts,alternate_granularity=alternatives,
        global_scalar_validity_counterexample=center_counterexample,center_cache_memory_screen=memory,
        structural_preparation_traversals=dict(timed_prepare_calls=2,
            full_document_parse_traversals=8,full_document_pretty_print_traversals=4,
            scope='Source audit of the measured preparation calls; startup parsing is additional. No substage CPU profile measured.'),
        complete=True)
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    for c in cohorts:
        if c['budget_ms']==4:
            print(c['subject'],c['operation'],'unit wall',round(c['median_unit_wall_ms'],3),
                  'minimum windows',c['minimum_windows'],'optimistic next-fit',c['perfect_next_fit_windows'],
                  'observed work windows',c['observed_work_windows'])
    print('Dense center-cache arithmetic margin:',memory['remaining_below_two_gib_bytes'],'bytes')


if __name__=='__main__':main()
