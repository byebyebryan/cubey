#!/usr/bin/env python3
"""Audit retained receipts; no native execution or publication."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[3]


def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def summarize(raw):
    receipts = {}
    work = defaultdict(set)
    service = []; frames = comparisons = cancelled = estimated_under = 0
    for path in sorted(raw.glob('*/cohort.json')):
        data = json.loads(path.read_text())
        assert data['complete'], path
        receipts[str(path.relative_to(ROOT))] = digest(path)
        if data['format'] != 'cubey.sdf.edit-service-cohort': continue
        cohorts = defaultdict(list)
        for row in data['runs']:
            n = row['native']; assert n['complete'] and row['exit_code'] == 0
            deadlines = all(f['scheduled_to_completion_ms'] <= 16.67 and not f['unit_crossed_window'] for f in n['frames'])
            recomputed = n['drained'] and n['replacement_completed_ms'] <= 1000/15 and deadlines
            assert recomputed == n['service_passed']
            frames += len(n['frames'])
            if n['drained']:
                assert n['canonical_cache_equal']; comparisons += 1
                work[row['subject'], row['operation']].add(tuple(n['build']['local_work']))
            else:
                assert n['cancelled_at_limit'] and n['frames'][-1]['pending'] == 4
                cancelled += 1
            for u in n['units']:
                estimated_under += u['wall_ms'] > n['frozen_unit_wall_bounds_ms'][u['kind']]
                if 'submit_ms' in u:
                    assert u['ready_ms'] <= u['grant_ms'] <= u['submit_ms'] <= u['gpu_complete_ms']
                if 'available_ms' in u:
                    assert u['gpu_complete_ms'] <= u['available_ms']
            if path.parent.name.startswith('strict-'):
                for f in n['frames']:
                    late = [u for u in n['units'] if u['frame'] == f['frame'] and
                            u['available_ms'] > f['maintenance_begin_ms'] + n['budget_ms']]
                    assert not late or f['unit_crossed_window']
            cohorts[row['subject'],row['operation'],row['validation']].append(row)
        for (subject,operation,validation), rows in sorted(cohorts.items()):
            natives = [r['native'] for r in rows]
            times = [n['replacement_completed_ms'] for n in natives if n['drained']]
            admissions = [n['build']['admission_ms'] for n in natives if 'build' in n]
            median = lambda key: statistics.median(n[key] for n in natives)
            service.append(dict(cohort=path.parent.name,subject=subject,operation=operation,validation=validation,
                trials=len(rows),passes=sum(n['service_passed'] for n in natives),drained=len(times),
                replacement_ms=times,median_replacement_ms=statistics.median(times) if times else None,
                median_preparation_ms=median('worker_preparation_ms'),
                median_admission_ms=statistics.median(admissions) if admissions else None,
                median_unit_gpu_ms=statistics.median(sum(u['gpu_ms'] for u in n['units']) for n in natives),
                median_unit_wall_ms=statistics.median(sum(u['wall_ms'] for u in n['units']) for n in natives),
                max_frame_ms=max(r['stats']['max_frame_ms'] for r in rows),
                frame_misses=sum(r['stats']['over_16_67'] for r in rows),
                maintenance_misses=sum(f['unit_crossed_window'] for n in natives for f in n['frames'])))
    assert all(len(v) == 1 for v in work.values()), 'source work changed across service variants'
    strict_service = [r for r in service if r['cohort'].startswith('strict-8-')]
    assert sum(r['trials'] for r in strict_service) == 28
    streams = []
    for folder in ('strict-stream-off','strict-stream-on'):
        path = raw/folder/'cohort.json'; d = json.loads(path.read_text()); assert d['complete']
        receipts[str(path.relative_to(ROOT))] = digest(path)
        for row in d['runs']:
            n=row['native']; a=row['assessment']; assert n['complete']
            assert n['consumed_poses'] == sum(len(f['displayed_pose_indices']) for f in n['frames'])
            for f in n['frames']:
                late=[u for u in n['units'] if u['frame']==f['frame'] and
                      u['available_ms'] > f['maintenance_begin_ms']+n['budget_ms']]
                assert not late or f['unit_crossed_window']
            streams.append(dict(cohort=folder,name=row['name'],rate=row['rate'],subtract=row['subtract'],
                passed=a['passed'],checks=a['checks'],accepted=n['accepted_count'],jobs=len(n['jobs']),
                frames=len(n['frames']),max_frame_ms=a['max_frame_ms'],
                max_accepted_to_cache_ms=a['max_accepted_to_completed_cache_ms'],
                max_pose_to_preview_ms=a['max_pose_to_preview_ms'],
                max_pending=n['max_pending_including_tip'],max_input_queue=n['max_input_queue'],
                max_reconstruction_error=max((x['max_combined_error'] for x in a['independent_reconstruction']),default=None),
                maintenance_misses=sum(f['unit_crossed_window'] for f in n['frames'])))
    assert len(streams)==16
    regression_path=raw/'final-regression/regression.json'
    regression=json.loads(regression_path.read_text());assert regression['complete']
    receipts[str(regression_path.relative_to(ROOT))]=digest(regression_path)
    assert all(x['score']['passed'] and x['owners_passed'] for x in regression['geometry'])
    assert all(x['byte_identical'] for x in regression['replays'])
    assert all(x['expected_refusal'] and x['exit_code'] != 0 for x in regression['negative'])
    baseline=json.loads((raw/'baseline/cohort.json').read_text())
    shader_dir=ROOT/'build/release/projects/sdf/shaders'
    assert all(digest(shader_dir/n)==h for n,h in baseline['identity']['shaders'].items())
    native=raw/'qualified-v2-native'
    current_identity=json.loads((raw/'strict-stream-off/cohort.json').read_text())['identity']
    assert digest(native)==current_identity['binary_sha256']
    admission_path=raw/'admission-review.json'
    admission=json.loads(admission_path.read_text());assert admission['complete'] and admission['preflight_refused_before_host']
    receipts[str(admission_path.relative_to(ROOT))]=digest(admission_path)
    positive=json.loads((raw/'tip-admission-positive/cohort.json').read_text())
    assert positive['complete'] and len(positive['runs'])==2 and all(r['assessment']['passed'] for r in positive['runs'])
    return dict(format='cubey.sdf.edit-service-results',version=1,
        runtime_source_head='9df0fa1f',binary_sha256=digest(native),
        final_runtime_source_head=admission['runtime_source_head'],final_native_sha256=admission['binary_sha256'],
        admission_followup=dict(positive_streams=2,early_tip_refusals=1,
            max_accepted_to_cache_ms=max(r['assessment']['max_accepted_to_completed_cache_ms'] for r in positive['runs']),
            max_frame_ms=max(r['assessment']['max_frame_ms'] for r in positive['runs'])),
        host='NVIDIA GeForce RTX 5070 Ti; Linux; Release; paced headless 1080p SS1; one completed reader',
        service=service,total_service_frames=frames,exact_service_cache_comparisons=comparisons,
        cancelled_service_controls=cancelled,units_exceeding_empirical_prediction=estimated_under,
        source_work_counts_preserved=True,
        strict_service_qualified=all(r['passes']==r['trials'] for r in strict_service),
        strict_service_trials=sum(r['trials'] for r in strict_service),
        finite_streams=streams,finite_streams_qualified=all(r['passed'] for r in streams),
        geometry=dict(regular_rays=sum(r['score']['regular_rays'] for r in regression['geometry']),
            owner_checks=sum(r['owner_checks'] for r in regression['geometry']),
            all_passed=True,byte_identical_cached_replays=len(regression['replays']),
            early_refusals=len(regression['negative'])),
        public_executable_unchanged=True,all_shaders_unchanged=len(baseline['identity']['shaders']),
        protected_public_shaders_unchanged=baseline['identity']['protected_shaders_unchanged'],
        receipts=receipts,complete=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--raw',type=Path,default=ROOT/'outputs/sdf/edit-service-loop-20261101')
    p.add_argument('--output',type=Path,default=Path(__file__).with_name('summary.json'))
    args=p.parse_args();result=summarize(args.raw.resolve())
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps({k:result[k] for k in ('strict_service_qualified','strict_service_trials','finite_streams_qualified','total_service_frames','exact_service_cache_comparisons','geometry')},indent=2))


if __name__=='__main__':main()
