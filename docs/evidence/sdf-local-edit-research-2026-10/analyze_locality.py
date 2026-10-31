#!/usr/bin/env python3
"""CPU-only reanalysis of saved canonical R16 fields; no native GPU work."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
PREVIOUS = ROOT / 'docs/evidence/sdf-cached-commit-2026-10'
R = math.sqrt(3) / (2 * 256)
TAU = 2e-6
IDEAL_BOUND = 2 * R + TAU
# Diagnostic margin, deliberately NOT asserted to be a universal F32 error proof.
DIAGNOSTIC_PAD = IDEAL_BOUND + 1e-4


def load(p):
    return json.loads(Path(p).read_text())


def sha(p):
    with Path(p).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def unmorton(codes):
    v = np.asarray(codes, dtype=np.uint32)
    result = np.zeros((len(v), 3), dtype=np.float64)
    for axis in range(3):
        for bit in range(8):
            result[:, axis] += ((v >> (3 * bit + axis)) & 1) * (1 << bit)
    return result


def center(codes, level=8):
    return (unmorton(codes) + .5) / (1 << level) - .5


def sphere_band(points, sphere, radius, padding):
    return np.linalg.norm(points - sphere['position'], axis=1) - sphere['radius'] <= radius + padding


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    pairs = load(PREVIOUS / 'cache-pairs.json')
    scans = []
    seen = set()
    for pair in pairs:
        row = pair['canonical']
        path = Path(row['files'][2])
        assert sha(path) == row['raw_r16_sha256'] and path.stat().st_size == row['texels'] * 2
        if row['raw_r16_sha256'] in seen:
            continue
        seen.add(row['raw_r16_sha256'])
        values = np.memmap(path, dtype='<f2', mode='r') if path.stat().st_size else np.empty(0,dtype='<f2')
        maximum = 0.
        nonfinite = 0
        for start in range(0, len(values), 1048576):
            block = values[start:start + 1048576].astype(np.float32)
            nonfinite += int(np.count_nonzero(~np.isfinite(block)))
            if len(block):
                maximum = max(maximum, float(np.max(np.abs(block))))
        scans.append(dict(case=pair['case'],file=str(path),sha256=row['raw_r16_sha256'],
            samples=len(values),maximum_abs_stored=maximum,nonfinite=nonfinite,
            below_ideal_plus_diagnostic_margin=maximum <= DIAGNOSTIC_PAD))
    assert all(r['nonfinite'] == 0 and r['below_ideal_plus_diagnostic_margin'] for r in scans)
    subject_masks=[]
    for run in load(PREVIOUS / 'qualification.json')['runs']:
        case=run['case']
        if 'radius' not in case:
            continue
        tool=load(case['input'])['cases'][0]['scene']['root']['children'][-1]
        row=run['native']['runs'][0]['canonical']
        cells=np.fromfile(row['files'][1],dtype='<u4')
        assert sha(row['files'][1])==row['cells_sha256']
        count=int(np.count_nonzero(sphere_band(center(cells),tool,R,DIAGNOSTIC_PAD)))
        subject_masks.append(dict(subject=case['subject'],operation=case['operation'],radius=case['radius'],
            total_bricks=len(cells),bricks_in_diagnostic_band=count,fraction=count/len(cells)))
    transitions = []
    for run in load(PREVIOUS / 'chains.json')['runs']:
        cases = load(run['case']['input'])['cases']
        rows = run['native']['runs']
        for i in range(1, len(rows)):
            old, new = rows[i - 1]['canonical'], rows[i]['canonical']
            tool = cases[i]['scene']['root']['children'][-1]
            assert tool['type'] == 'sphere'
            old_cells = np.fromfile(old['files'][1], dtype='<u4')
            new_cells = np.fromfile(new['files'][1], dtype='<u4')
            assert sha(old['files'][1]) == old['cells_sha256'] and sha(new['files'][1]) == new['cells_sha256']
            common, ai, bi = np.intersect1d(old_cells, new_cells, return_indices=True)
            removed = np.setdiff1d(old_cells, new_cells)
            added = np.setdiff1d(new_cells, old_cells)
            a = np.memmap(old['files'][2], dtype='<u2', mode='r', shape=(len(old_cells),512))
            b = np.memmap(new['files'][2], dtype='<u2', mode='r', shape=(len(new_cells),512))
            changed = np.zeros(len(common), dtype=bool)
            changed_samples = 0
            for start in range(0, len(common), 2048):
                delta = a[ai[start:start + 2048]] != b[bi[start:start + 2048]]
                changed[start:start + 2048] = np.any(delta, axis=1)
                changed_samples += int(np.count_nonzero(delta))
            points = center(common)
            raw_box = np.all(np.abs(points - tool['position']) <= tool['radius'] + 1/512, axis=1)
            raw_sphere = sphere_band(points, tool, R, 0.)
            band = sphere_band(points, tool, R, DIAGNOSTIC_PAD)
            old_band = sphere_band(center(old_cells), tool, R, DIAGNOSTIC_PAD)
            new_band = sphere_band(center(new_cells), tool, R, DIAGNOSTIC_PAD)
            # Independently inspect normalized reachable topology, including entries
            # appearing/disappearing when uniform cells become subdivided or reverse.
            ta = np.fromfile(old['files'][0], dtype='<u4').reshape(-1,2)
            tb = np.fromfile(new['files'][0], dtype='<u4').reshape(-1,2)
            assert sha(old['files'][0]) == old['topology_sha256'] and sha(new['files'][0]) == new['topology_sha256']
            keys, ix, iy = np.intersect1d(ta[:,0],tb[:,0],return_indices=True)
            changed_flags = keys[ta[ix,1] != tb[iy,1]]
            added_keys, removed_keys = np.setdiff1d(tb[:,0],ta[:,0]), np.setdiff1d(ta[:,0],tb[:,0])
            changed_keys = np.concatenate((changed_flags,added_keys,removed_keys))
            structural_parents = set(int(k) for k in keys[((ta[ix,1] ^ tb[iy,1]) & 0x20000000) != 0])
            changed_flag_set=set(int(k) for k in changed_flags)
            topology_outside_band = 0
            flag_changes_outside_band = 0
            unexplained_structural_changes = 0
            for level in (2,4,6,8):
                selected = changed_keys[(changed_keys >> 24) == level]
                codes = selected & 0xffffff
                mask = sphere_band(center(codes,level),tool,math.sqrt(3)/(2*(1<<level)),DIAGNOSTIC_PAD)
                topology_outside_band += int(np.count_nonzero(~mask))
                for key in selected[~mask]:
                    code = int(key) & 0xffffff
                    if int(key) in changed_flag_set:
                        flag_changes_outside_band += 1
                    explained = False
                    for parent_level in range(level-2,1,-2):
                        parent_code = code >> (3*(level-parent_level))
                        parent_key = (parent_level << 24) | parent_code
                        if parent_key in structural_parents:
                            intersects = sphere_band(center([parent_code],parent_level),tool,
                                math.sqrt(3)/(2*(1<<parent_level)),DIAGNOSTIC_PAD)[0]
                            if intersects:
                                explained = True
                                break
                    if not explained:
                        unexplained_structural_changes += 1
            result = dict(subject=run['case']['subject'],step=i,name=rows[i]['name'],
                operation=cases[i]['scene']['root']['type'],tool=tool,
                old_bricks=len(old_cells),new_bricks=len(new_cells),common_bricks=len(common),
                added_bricks=len(added),removed_bricks=len(removed),
                changed_common_bricks=int(np.count_nonzero(changed)),changed_common_samples=changed_samples,
                changed_common_outside_raw_brush_aabb=int(np.count_nonzero(changed & ~raw_box)),
                changed_common_outside_brush_cell_sphere=int(np.count_nonzero(changed & ~raw_sphere)),
                old_bricks_in_diagnostic_band=int(np.count_nonzero(old_band)),
                new_bricks_in_diagnostic_band=int(np.count_nonzero(new_band)),
                changed_common_outside_diagnostic_band=int(np.count_nonzero(changed & ~band)),
                added_outside_diagnostic_band=int(np.count_nonzero(~sphere_band(center(added),tool,R,DIAGNOSTIC_PAD))),
                removed_outside_diagnostic_band=int(np.count_nonzero(~sphere_band(center(removed),tool,R,DIAGNOSTIC_PAD))),
                changed_topology_entries=len(changed_keys),changed_topology_outside_diagnostic_band=topology_outside_band,
                changed_existing_topology_flags_outside_diagnostic_band=flag_changes_outside_band,
                outside_topology_not_explained_by_changed_ancestor=unexplained_structural_changes,
                source_cache_hashes=[old['raw_r16_sha256'],new['raw_r16_sha256']])
            result['diagnostic_band_plus_structural_closure_covers_changes'] = all(result[k] == 0 for k in (
                'changed_common_outside_diagnostic_band','added_outside_diagnostic_band',
                'removed_outside_diagnostic_band','changed_existing_topology_flags_outside_diagnostic_band',
                'outside_topology_not_explained_by_changed_ancestor'))
            assert result['diagnostic_band_plus_structural_closure_covers_changes']
            transitions.append(result)
    assert len(transitions) == 22
    # An exact-arithmetic counterexample to treating the brush solid AABB as
    # the complete scalar support: outside a unit cutter, a deep solid's
    # distance changes from -10 to -.5 although occupancy stays solid.
    counterexample = dict(base=-10.,tool=.5,cut_result=max(-10.,-.5),
        outside_tool=True,occupancy_changed=False,scalar_changed=True)
    source_paths = [ROOT / p for p in (
        'projects/sdf/sdf_field.cpp','projects/sdf/sdf_cached_commit.cpp',
        'projects/sdf/shaders/authored_filter.comp','projects/sdf/shaders/authored_bricks.comp',
        'projects/sdf/shaders/commit_reuse_bricks.comp','projects/sdf/shaders/trace.glsl',
        'projects/sdf/shaders/cached_edit_trace.glsl','projects/sdf/sdf_regions.cpp',
        'projects/sdf/sdf_renderer.cpp','projects/sdf/sdf_cached_edit.cpp',
        'src/cubey/vulkan/image.cpp','src/cubey/render/texture.cpp',
        'include/cubey/engine/project_gpu_services.h','include/cubey/vulkan/gpu_runtime.h')]
    donor = Path('/home/bryan/code/projectR')
    donor_paths = [donor / p for p in ('Assets/Shaders/BrickEngineBuilder.compute',
        'Assets/Shaders/BrickEngineCompute.compute','Assets/Scripts/BrickEngine/BrickEngine.cs')]
    result = dict(scope='CPU reanalysis of saved native fields; no new native GPU timing or local-updater qualification',
        source_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        script_sha256=sha(__file__),numpy_version=np.__version__,
        leaf_cell_radius=R,classifier_epsilon=TAU,ideal_real_arithmetic_sample_bound=IDEAL_BOUND,
        diagnostic_padding=DIAGNOSTIC_PAD,
        padding_status='Illustrative real bound plus 1e-4 diagnostic margin; not a proved universal F32 guard',
        scans=scans,unique_canonical_fields=len(scans),samples_scanned=sum(r['samples'] for r in scans),
        maximum_abs_stored=max(r['maximum_abs_stored'] for r in scans),
        transitions=transitions,subject_masks=subject_masks,counterexample=counterexample,
        source_sha256={str(p.relative_to(ROOT)):sha(p) for p in source_paths},
        previous_packet_sha256={name:sha(PREVIOUS/name) for name in ('cache-pairs.json','chains.json','summary.json','baseline.json')},
        projectr_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=donor,text=True).strip(),
        projectr_sha256={str(p.relative_to(donor)):sha(p) for p in donor_paths})
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps({k:result[k] for k in ('unique_canonical_fields','samples_scanned',
        'maximum_abs_stored','ideal_real_arithmetic_sample_bound','diagnostic_padding')},indent=2))
    for subject in ('guardian','robot'):
        rows = [r for r in transitions if r['subject']==subject]
        print(subject,json.dumps(dict(transitions=len(rows),
            candidate_fraction_range=[min(r['new_bricks_in_diagnostic_band']/r['new_bricks'] for r in rows),
                max(r['new_bricks_in_diagnostic_band']/r['new_bricks'] for r in rows)],
            raw_aabb_missed_changed_bricks=sum(r['changed_common_outside_raw_brush_aabb'] for r in rows))))


if __name__ == '__main__':
    main()
