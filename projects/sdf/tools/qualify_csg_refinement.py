#!/usr/bin/env python3
"""Qualify the saved form-refinement candidates, including every native history state."""
import argparse
import datetime as dt
import json
import math
from pathlib import Path
import re
import subprocess

from check_robot_joints import add, sub, scale, length, rotate, compose
from refine_csg_showcase import ROOT, SCENES, PRESETS, SOURCES, family, sha, save, validate


def support_audit(document):
    """Read composed geometry, independently of the scene-authoring constants."""
    nodes = []
    def walk(node, p=(0, 0, 0), q=(0, 0, 0, 1), s=1):
        center = add(p, rotate(q, scale(node.get('position', (0, 0, 0)), s)))
        local_q = node.get('rotation_xyzw', (0, 0, 0, 1))
        q = compose(q, scale(local_q, 1/length(local_q)))
        s *= node.get('scale', 1)
        nodes.append(dict(node, center=center, q=q, size=s))
        for child in node.get('children', []):
            walk(child, center, q, s)
    walk(document['root'])
    checks = []
    def group(prefix): return [n for n in nodes if family(n) == prefix]
    def one(prefix):
        matches = group(prefix)
        assert len(matches) == 1, prefix
        return matches[0]
    def check(label, passed, **values):
        checks.append(dict(name=label, passed=bool(passed), **values))
    def top(node):
        # These support slabs are horizontal. Reject a changed orientation.
        assert length(sub(rotate(node['q'], (0, 1, 0)), (0, 1, 0))) < 1e-9
        h = node.get('half_length', node.get('half_extent', [0, 0, 0])[1])
        return node['center'][1] + (h + node.get('rounding', 0))*node['size']
    floor, rock, foot = [one(p) for p in ('lower-terrace', 'rock-core', 'display-foot')]
    for upper, lower in [(floor, rock), (rock, foot)]:
        bottom = 2*upper['center'][1]-top(upper)
        gap = bottom-top(lower)
        check(upper['name']+' supported below', gap <= 1e-9, signed_gap=gap)
    # Conservative rounded-box envelopes in XZ. This guarantees the entire
    # stair/landing stays inside the square terrace, rather than just centers.
    for node in group('stair-tread') + group('bottom-landing') + group('top-landing'):
        e = [v+node.get('rounding', 0) for v in node['half_extent']]
        for x in (-e[0], e[0]):
            for z in (-e[2], e[2]):
                p = add(node['center'], rotate(node['q'], scale((x, 0, z), node['size'])))
                margin = min(floor['half_extent'][0]-abs(p[0]-floor['center'][0]),
                             floor['half_extent'][2]-abs(p[2]-floor['center'][2]))
                check(node['name']+' footprint', margin >= .010, margin=margin)
    treads = sorted(group('stair-tread'), key=lambda n:n['center'][1])
    masses = sorted(group('stair-mass'), key=lambda n:n['center'][1])
    assert len(treads) == len(masses) == 10
    for tread, mass in zip(treads, masses):
        bottom = 2*tread['center'][1]-top(tread)
        check(tread['name']+' on stair mass', bottom <= top(mass)+1e-9)
    pad = one('survey-pad')
    feet = []
    for leg in group('tripod-leg'):
        offset = rotate(leg['q'], (0, leg['half_length']*leg['size'], 0))
        p = min([add(leg['center'], offset), sub(leg['center'], offset)], key=lambda p:p[1])
        radius = leg['radius']*leg['size']
        radial_margin = pad['radius']*pad['size']-math.hypot(p[0]-pad['center'][0], p[2]-pad['center'][2])-radius
        signed_gap = p[1]-radius-top(pad)
        # Feet are on the left observing apron.
        separation = min(n['center'][0]-n['half_extent'][0]-n.get('rounding', 0)-(p[0]+radius)
                         for n in treads)
        check(leg['name']+' seats on pad and clears stairs', radial_margin > .005 and
              signed_gap <= 1e-9 and signed_gap >= -.012 and separation > .010,
              radial_margin=radial_margin, signed_gap=signed_gap, stair_separation=separation)
        feet.append(p)
    assert len(feet) == 3
    return dict(passed=all(c['passed'] for c in checks), checks=checks, feet=feet,
                scope='Serialized square-base supports, conservative stair/landing footprint, tread seating, three tripod feet; not a general collision solver.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=Path('build/release/projects/sdf/sdf'))
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    binary = (ROOT/args.binary).resolve()
    shaders = {p.name:sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))}
    receipt = dict(format='cubey.sdf.form-refinement-qualification', version=1,
                   captured_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                   binary_sha256=sha(binary), shaders=shaders, source_head=subprocess.check_output(
                       ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                   qualifier_sha256=sha(__file__), subjects=[], complete=False)
    save(out/'qualification.json', receipt)
    for subject in SOURCES:
        scene = SCENES/(subject+'.json')
        document = json.loads(scene.read_text())
        structural = validate(document)
        row = dict(subject=subject, scene=str(scene.relative_to(ROOT)), scene_sha256=sha(scene), structural=structural)
        if subject.startswith('observatory'):
            row['support'] = support_audit(document)
            assert row['support']['passed'], row['support']
        config = json.loads((PRESETS/(subject+'.json')).read_text())
        # Native field oracles deliberately use the advanced sampled controls;
        # named presentation modes reject oracle combinations by contract.
        config.pop('render-mode')
        config_path = out/(subject+'-oracle-config.json')
        save(config_path, config)
        command = [str(binary), '--config', str(config_path),
                   '--headless', '--require-validation', '--frames', '1',
                   '--width', '480', '--height', '360', '--supersample', '1',
                   '--surface-trace', 'sampled', '--validate-history', '--validate-field',
                   '--output', str(out/(subject+'.png'))]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=600)
        log = result.stdout+result.stderr
        log_path = out/(subject+'.log')
        log_path.write_text(log)
        assert result.returncode == 0 and 'VUID-' not in log and 'Validation Error' not in log, log[-4000:]
        assert 'sdf: authored history passed' in log
        visited = [int(m) for m in re.findall(r'sdf: history validated step=(\d+)', log)]
        n = len(document['history'])
        assert visited == [*range(n+1), n//2, 0, n], visited
        row.update(command=command, config_sha256=sha(config_path), exit_code=result.returncode, validated_states=visited,
                   log_sha256=sha(log_path), image_sha256=sha(out/(subject+'.png')),
                   stats=[s for s in log.splitlines() if s.startswith('sdf:')])
        receipt['subjects'].append(row)
        save(out/'qualification.json', receipt)
        print(subject, len(visited), 'native history/seek states PASS', flush=True)
    assert sha(binary) == receipt['binary_sha256']
    assert all(sha(binary.parent/'shaders'/n) == h for n,h in shaders.items())
    receipt.update(complete=True, runtime_unchanged=True)
    save(out/'qualification.json', receipt)


if __name__ == '__main__':
    main()
