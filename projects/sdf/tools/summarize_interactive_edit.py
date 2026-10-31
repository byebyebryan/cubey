#!/usr/bin/env python3
"""Audit retained native receipts and summarize the bounded editing followup."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import statistics


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(raw):
    q = raw/'qualified-v1'
    execution = json.loads((q/'execution.json').read_text())
    assert execution['complete'] and all(r['exit_code'] == 0 for r in execution['steps'])
    receipts = {}
    def read(folder, name):
        path = q/folder/name
        result = json.loads(path.read_text())
        assert result['complete'], path
        receipts[str(path)] = digest(path)
        return result
    geometry = read('geometry', 'geometry.json')['score']
    assert geometry['passed'] and geometry['owners_passed']
    admission = read('admission', 'admission.json')
    assert all((r['exit_code'] == 0) == r['expected'] for r in admission['cases'])
    cache_comparisons = words = peak_geometry = 0
    phases = {}
    for name in ('cache-fixtures', 'cache-dominance', 'cache-local', 'cache-sliced', 'sampling-work'):
        data = read(name, 'sampling-work.json' if name == 'sampling-work' else 'caches.json')
        comparisons = count = 0
        for run in data['runs']:
            assert run['exit_code'] == 0 and run['native']['cache_passed']
            for case in run['native']['cache_runs']:
                assert case['comparison']['passed']
                comparisons += 1; count += case['candidate']['texels']
                peak_geometry = max(peak_geometry, max(t['geometry_payload_bytes'] for t in case['trials']))
        phases[name] = dict(comparisons=comparisons, raw_r16_words=count)
        cache_comparisons += comparisons; words += count
    service = read('service', 'service.json')
    cohorts = defaultdict(list)
    for run in service['runs']:
        n = run['native']
        deadlines = all(f['scheduled_to_completion_ms'] <= 16.67 and not f['unit_crossed_window'] for f in n['frames'])
        recomputed = n['drained'] and n['replacement_completed_ms'] <= 4*1000/60 and deadlines
        assert recomputed == n['service_passed']
        if n['drained']: assert n['canonical_cache_equal']
        cohorts[(run['subject'], run['operation'], run['budget_ms'])].append(run)
    service_summary = []
    for (subject, op, budget), runs in sorted(cohorts.items()):
        times = [r['native']['replacement_completed_ms'] for r in runs if r['native']['drained']]
        n = runs[0]['native']
        service_summary.append(dict(subject=subject, operation=op, budget_ms=budget,
            completed_replacement_ms=times, passes=sum(r['native']['service_passed'] for r in runs), trials=len(runs),
            median_preparation_ms=statistics.median(r['native']['worker_preparation_ms'] for r in runs),
            median_admission_ms=statistics.median(r['native']['build']['admission_ms'] for r in runs) if times else None,
            median_gpu_ms=statistics.median(sum(u['gpu_ms'] for u in r['native']['units']) for r in runs),
            median_unit_wall_ms=statistics.median(sum(u['wall_ms'] for u in r['native']['units']) for r in runs),
            max_complete_frame_ms=max(r['stats']['max_ms'] for r in runs),
            missed_60hz=sum(r['stats']['over_16_67'] for r in runs)))
    captures = read('captures', 'captures.json')
    assert all(r.get('original_cached_byte_identical', False) for r in captures['runs'] if r['phase'] == 'committed' or r['case']['count'] == 0)
    replay = read('replay', 'replay.json')
    assert all(r['exhausted'] == 0 and r['changed_cached_rays'] >= 10 for r in replay['runs'])
    prefixes = read('prefixes', 'prefixes.json')
    assert all(r['intermediate_byte_identical'] and r['settled_byte_identical'] and r['retained_source_matches'] for r in prefixes['runs'])
    timing = read('timing', 'timing.json')
    render_cohorts = defaultdict(list)
    for run in timing['runs']:
        row = run['case']
        render_cohorts[(row['subject'], run['view'], row['operation'], row['count'], run['route'])].extend(run['passes']['sdf.whole_scene']['values_ms'])
    render_summary = [dict(subject=s, view=v, operation=op, count=n, route=route,
        frames=len(values), median_ms=statistics.median(values), max_ms=max(values),
        over_16_67=sum(x > 16.67 for x in values), over_11_11=sum(x > 11.11 for x in values))
        for (s, v, op, n, route), values in sorted(render_cohorts.items())]
    sampling = json.loads((raw/'sampling-inputs-v1/sampling.json').read_text())
    assert sampling['complete'] and sampling['lift_restart_no_bridge']
    work = read('sampling-work', 'sampling-work.json')
    work_cohorts = defaultdict(list)
    for run in work['runs']:
        row = run['case']; trials = [case['trials'][0] for case in run['native']['cache_runs']]
        work_cohorts[(row['subject'], row['path'], row['representation'])].append(dict(
            wall_ms=sum(t['builder_wall_ms'] for t in trials),
            gpu_ms=sum(u['gpu_ms'] for t in trials for u in t['units']),
            dirty_bricks=sum(t['local_work'][3] for t in trials),
            tools=row['tools'], batches=row['batches']))
    work_summary = [dict(subject=s, path=p, representation=r, trials=len(values),
        median_wall_ms=statistics.median(v['wall_ms'] for v in values),
        median_gpu_ms=statistics.median(v['gpu_ms'] for v in values),
        median_dirty_bricks=statistics.median(v['dirty_bricks'] for v in values),
        tools=values[0]['tools'], batches=values[0]['batches'])
        for (s, p, r), values in sorted(work_cohorts.items())]
    return dict(source_head=execution['source_head'], binary_sha256=execution['binary_sha256'],
        admission_checks=len(admission['cases']), geometry=geometry,
        cache_comparisons=cache_comparisons, raw_r16_words=words, cache_phases=phases,
        peak_geometry_payload_bytes=peak_geometry, service=service_summary,
        complete_service_frames=sum(len(r['native']['frames']) for r in service['runs']),
        captures=len(captures['runs']), original_cached_image_matches=sum(r.get('original_cached_byte_identical', False) for r in captures['runs']),
        prefixes=len(prefixes['runs']), prefix_frames=sum(len(r['native']['frames']) for r in prefixes['runs']),
        camera_rays=sum(r['camera_rays'] for r in replay['runs']), shadow_rays=sum(r['shadow_rays'] for r in replay['runs']),
        preview_render=render_summary, sampling=sampling['rows'], sampling_work=work_summary,
        receipts=receipts, complete=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--raw', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    value = summarize(args.raw.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(value, indent=2, allow_nan=False)+'\n')
    print(value['cache_comparisons'], 'exact caches;', value['raw_r16_words'], 'raw R16 words; summary:', args.output)


if __name__ == '__main__':
    main()
