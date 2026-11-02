#!/usr/bin/env python3
"""Finite raw-pose integration; independent reconstruction and deadline checks."""
import argparse
import json
import math
from pathlib import Path
import subprocess

import cached_edit_loop as edit
from edit_service_loop import ROOT, digest, identity, save
import stroke_sampling
from tracing_comparison import document, sphere


def poses(rate):
    result = []
    for stroke, start, duration in [(0, 0., .1), (1, .14, 8/120), (2, .25, .1)]:
        steps = round(duration*rate)
        for i in range(steps+1):
            u = i/steps
            if stroke == 0:
                v = u if u < .3 else .3 if u < .5 else .3+(u-.5)*1.4
                p, radius = [-.035+.07*v, -.035, .19], .012
            elif stroke == 1:
                p = [-.035+.07*min(2*u, 1), -.01+.04*max(2*u-1, 0), .19]
                radius = .008+.016*u if u <= .5 else .016-.012*(u-.5)
            else:
                p, radius = [-.035+.07*u, .045+.003*math.sin(math.pi*u), .185], .01+.0015*math.sin(2*math.pi*u)
            result.append(dict(t=start+duration*u, p=p, r=radius,
                               begin=i == 0, end=i == steps, corner=stroke == 1 and i*2 == steps))
    return result


def assess(native, recorded):
    accepted = native['accepted']
    published = [i for job in native['jobs'] for i in job['ids']]
    wanted = [row['id'] for row in accepted]
    identity_ok = published == wanted and not native['uncommitted_ids']
    ages = [job['first_completed_draw_ms']-accepted[i-1]['source_accept_ms']
            for job in native['jobs'] for i in job['ids']]
    previews = [f['completed_ms']-recorded[i]['t']*1000
                for f in native['frames'] for i in f['displayed_pose_indices']]
    independent = []
    first = 0
    for last, pose in enumerate(recorded):
        if not pose['end']: continue
        spans = [(a['first']-first, a['last']-first) for a in accepted
                 if first <= a['first'] <= a['last'] <= last]
        independent.append(stroke_sampling.validate(recorded[first:last+1], spans, native['tolerance']))
        first = last+1
    frames = [f['scheduled_to_completion_ms'] for f in native['frames']]
    checks = dict(drained=native['drained'], canonical_cache=native.get('canonical_cache_equal', False),
                  faithful_source=native.get('canonical_source_equal', False), all_ids=identity_ok,
                  all_poses=native['consumed_poses'] == len(recorded) == len(previews),
                  bounded=native['max_pending_including_tip'] <= 8 and native['max_input_queue'] <= 64,
                  frames=max(frames) <= 16.67 and not any(f['unit_crossed_window'] for f in native['frames']),
                  accepted_to_cache=bool(ages) and max(ages) <= 1000/15,
                  input_to_preview=bool(previews) and max(previews) <= 1000/30)
    return dict(checks=checks, passed=all(checks.values()), max_frame_ms=max(frames),
                max_accepted_to_completed_cache_ms=max(ages, default=None),
                max_pose_to_preview_ms=max(previews, default=None), independent_reconstruction=independent)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--repeats', type=int, default=3)
    p.add_argument('--validation', choices=['on', 'off'], default='off')
    p.add_argument('--rates', type=int, nargs='+', default=[120, 240])
    args = p.parse_args(); args.binary = args.binary.resolve(); args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    receipt = dict(format='cubey.sdf.finite-stroke-cohort', version=1, identity=identity(args.binary),
                   runs=[], complete=False)
    for repeat in range(args.repeats):
        for rate in args.rates:
            recorded = poses(rate)
            for cut in (False, True):
                name = f'{rate}-'+('cut' if cut else 'add')+f'-{repeat}'
                base = document(sphere('body', .2))
                # Keep named history and material ownership in the retained source.
                base['history'] = [dict(node='body', label='Initial body', seconds=.2)]
                data = edit.manifest([dict(name=name, base=base, scene=base)])
                data['stream'] = dict(poses=recorded, subtract=cut, material='blue', groups=2048, budget_ms=4)
                inp, out = args.output/(name+'-input.json'), args.output/(name+'.json')
                save(inp, data)
                command = edit.native(args, inp, out, 'service', ['--edit-commit-builder', 'local-dom', '--edit-rounded-segments'], size=(1920, 1080))
                if args.validation == 'off': command[command.index('--require-validation')] = '--no-validation'
                with out.with_suffix('.log').open('w') as log:
                    result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=180)
                log = out.with_suffix('.log').read_text()
                if result.returncode or 'VUID-' in log or 'Validation Error' in log:
                    raise RuntimeError(log[-4000:])
                native = json.loads(out.read_text()); assert native['complete']
                assessment = assess(native, recorded)
                receipt['runs'].append(dict(name=name, rate=rate, subtract=cut, repeat=repeat,
                                            validation=args.validation, command=command, input_sha256=digest(inp),
                                            log_sha256=digest(out.with_suffix('.log')), native=native, assessment=assessment))
                save(args.output/'cohort.json', receipt)
                print(name, 'pass', assessment['passed'], 'accepted/cache age', assessment['max_accepted_to_completed_cache_ms'],
                      'frame max', assessment['max_frame_ms'], 'slots', native['max_pending_including_tip'], flush=True)
    receipt['complete'] = True
    save(args.output/'cohort.json', receipt)


if __name__ == '__main__': main()
