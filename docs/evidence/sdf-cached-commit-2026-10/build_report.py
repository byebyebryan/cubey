#!/usr/bin/env python3
"""Post-timing evidence checks, lossless native images and static report export."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import shutil
import statistics

from PIL import Image

ROOT=Path(__file__).resolve().parents[3]
PACKET=Path(__file__).resolve().parent


def load(path):return json.loads(path.read_text())
def save(path,data):path.write_text(json.dumps(data,indent=2,allow_nan=False)+'\n')
def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def percentile(values,p):
    s=sorted(values);i=(len(s)-1)*p;a=int(i)
    return s[a]+(s[min(a+1,len(s)-1)]-s[a])*(i-a)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--raw',type=Path,required=True);p.add_argument('--export',type=Path,required=True)
    args=p.parse_args();raw=args.raw.resolve();q=raw/'qualified-v2';export=args.export.resolve()
    export.mkdir(parents=True,exist_ok=False);(export/'images').mkdir()
    files={'prepare':q/'prepare/prepare.json','qualification':q/'reuse/qualification.json',
        'chains':q/'chains/qualification.json','faults':q/'faults/faults.json',
        'sequences':q/'sequences/sequences.json','retention':q/'retention-final/retention.json',
        'timing':q/'timing/timing.json','pruned4':q/'pruned4/qualification.json',
        'pruned16':q/'pruned16/qualification.json','pruned_cost':q/'pruned-cost-diagnostic/timing.json',
        'prune_probe':q/'prune-probe-raw/prune-probe.json'}
    data={name:load(path) for name,path in files.items()}
    assert all(d['complete'] for d in data.values())
    for name in ('qualification','chains'):assert data[name]['passed']
    base=load(raw/'baseline.json');shaders=ROOT/'build/release/projects/sdf/shaders'
    assert all(digest(shaders/name)==h for name,h in base['shader_hashes'].items())
    allowed={'projects/sdf/CMakeLists.txt','projects/sdf/sdf_field.h','projects/sdf/tools/cached_edit_study.cpp'}
    assert all(digest(ROOT/name)==h for name,h in base['protected'].items() if name not in allowed)
    required={name:h for name,h in data['timing']['shaders'].items() if name!='commit_prune_probe.comp.spv'}
    for name in ('qualification','chains','faults','pruned4','pruned16','pruned_cost'):
        assert data[name]['binary_sha256']==data['timing']['binary_sha256']
        assert all(data[name]['shaders'][shader]==h for shader,h in required.items())
    assert len(data['timing']['runs'])==48
    groups=collections.defaultdict(list);warmup=0
    for run in data['timing']['runs']:
        assert run['exit_code']==0 and run['native']['complete']
        rows=run['native']['runs'][0]['trials'];assert len(rows)==13
        for row in rows:
            if row['warmup']:warmup+=1;continue
            assert not row['full_fallback']
            key=(run['case']['subject'],run['case']['operation'],'new' if run['cold'] else 'pooled',run['mode'])
            groups[key].append(row)
    timing=[]
    for key,rows in sorted(groups.items()):
        assert len(rows)==30
        values=[r['ready_to_render_ms'] for r in rows]
        means=collections.defaultdict(list)
        for r in rows:
            stages=dict(admission=r['admission_ms'],allocation=r['allocation_ms'],classification=r['filter_gpu_ms'],
                reuse=r['reuse_gpu_ms'] if key[-1]=='reuse' else 0.,
                canonical=r['fallback_gpu_ms'] if key[-1]=='reuse' else r['brick_gpu_ms'])
            stages['other']=r['ready_to_render_ms']-sum(stages.values())
            assert stages['other']>=0
            for name,value in stages.items():means[name].append(value)
        timing.append(dict(subject=key[0],operation=key[1],allocation=key[2],mode=key[3],samples=30,
            median_ms=statistics.median(values),p95_ms=percentile(values,.95),p99_ms=percentile(values,.99),
            min_ms=min(values),max_ms=max(values),under_16_67=sum(x<=16.67 for x in values),
            under_11_11=sum(x<=11.11 for x in values),
            means_ms={name:statistics.mean(v) for name,v in means.items()},
            median_stages_ms={name:statistics.median(v) for name,v in means.items()}))
    speedups=[]
    for subject in ('guardian','robot'):
        for operation in ('add','cut'):
            for allocation in ('pooled','new'):
                rows={r['mode']:r for r in timing if (r['subject'],r['operation'],r['allocation'])==(subject,operation,allocation)}
                ratio=rows['full']['median_ms']/rows['reuse']['median_ms'];target=5 if subject=='guardian' else 2
                speedups.append(dict(subject=subject,operation=operation,allocation=allocation,ratio=ratio,
                    proposed_target=target,passed=ratio>=target))
    assert all(r['passed'] for r in speedups)

    pairs=[]
    for phase in ('qualification','chains'):
        for run in data[phase]['runs']:
            for row in run['native']['runs']:
                assert row['comparison']['passed'] and row['comparison']['r16_differences']==0
                pairs.append(dict(phase=phase,case=row['name'],candidate=row['candidate'],canonical=row['canonical']))
    for run in data['faults']['overflow']:
        row=run['native']['runs'][0]
        assert row['comparison']['passed'] and row['trials'][0]['full_fallback']
        pairs.append(dict(phase='overflow',case=row['name'],candidate=row['candidate'],canonical=row['canonical']))
    assert len(pairs)==61
    print('Hashing preserved raw artifacts after all native timing...',flush=True)
    manifest={}
    for path in sorted(raw.rglob('*')):
        if path.is_file() and not any(part.startswith('report') for part in path.relative_to(raw).parts):
            manifest[str(path.relative_to(raw))]=dict(bytes=path.stat().st_size,sha256=digest(path))
    for pair in pairs:
        for side in ('candidate','canonical'):
            row=pair[side]
            assert len(row['files'])==3
            for filename,key,size in zip(row['files'],('topology_sha256','cells_sha256','raw_r16_sha256'),
                    (row['topology_entries']*8,row['bricks']*4,row['texels']*2)):
                actual=manifest[str(Path(filename).relative_to(raw))]
                assert actual['sha256']==row[key] and actual['bytes']==size
    raw_manifest=dict(root=str(raw),files=manifest,scope='Actual saved bytes; large raw fields remain local and are not bundled into HTML')
    save(PACKET/'raw-manifest.json',raw_manifest)

    cache={};image_checks=[]
    def image(source):
        source=Path(source);sha=digest(source)
        if sha not in cache:
            name='images/native-'+sha[:18]+'.webp'
            original=Image.open(source).convert('RGBA')
            original.save(export/name,'WEBP',lossless=True,method=6,exact=True)
            decoded=Image.open(export/name).convert('RGBA')
            assert original.size==decoded.size and original.tobytes()==decoded.tobytes()
            cache[sha]=name
        image_checks.append(dict(source=str(source),source_sha256=sha,export=cache[sha],rgba_equal=True))
        return cache[sha]
    sequences=[]
    for run in data['sequences']['runs']:
        assert run['settled_byte_identical']
        native=run['native'];case=run['case'];original=load(Path(case['input']))['cases'][0]['scene']
        retained=load(Path(native['retained_source_file']))
        assert retained['root']==original['root'] and retained.get('history')==original.get('history')
        assert int(retained['geometry_hash'],16)==native['programs'][0]['edited_geometry_hash']
        control=q/'sequences'/(case['name']+'-original-cached.png')
        assert digest(control)==digest(Path(native['frames'][-1]['image']))
        if run['failure']!='none':
            assert digest(Path(native['frames'][1]['image']))==digest(Path(native['frames'][2]['image']))
            assert native['frames'][2]['pending_after_completed_frame']==1
        row=dict(subject=case['subject'],operation=case['operation'],failure=run['failure'],
            commits=native['commits'],retired_after=native['pending_retired_after_completed_frame'],
            control=image(control),frames=[])
        for f in native['frames']:row['frames'].append(dict(f,image=image(f['image'])))
        assert row['frames'][row['retired_after']]['pending_after_completed_frame']==0
        sequences.append(row)
    assert len(sequences)==14 and sum(len(r['frames']) for r in sequences)==66
    assert len(data['retention']['runs'])==4 and data['retention']['all_sequence_source_trees_and_histories_equal']
    for run in data['retention']['runs']:assert run['byte_identical']

    work=[]
    for run in data['qualification']['runs']:
        c=run['case']
        if c.get('radius')!=.065:continue
        row=run['native']['runs'][0];trial=row['trials'][0];counts=trial['work_counts']
        output=Path(run['command'][run['command'].index('--commit-output')+1]);log=output.with_suffix('.log').read_text()
        old=int(re.search(r'bricks=(\d+)/',log)[1])
        assert counts[0]+counts[1]==trial['bricks'] and counts[2]+counts[3]==counts[0]*512
        work.append(dict(subject=c['subject'],operation=c['operation'],old_bricks=old,common_bricks=counts[0],
            new_bricks=counts[1],removed_bricks=old-counts[0],reused_texels=counts[2],
            numeric_fallback_texels=counts[3],new_canonical_texels=counts[4]))
    rejected=[]
    for phase in ('pruned4','pruned16'):
        for run in data[phase]['runs']:
            if run['case'].get('radius')!=.065:continue
            row=run['native']['runs'][0];cmp=row['comparison']
            rejected.append(dict(subject=run['case']['subject'],operation=run['case']['operation'],
                axis=data[phase]['regions'],passed=cmp['passed'],topology_equal=cmp['topology_equal'],
                candidate_bricks=row['candidate']['bricks'],canonical_bricks=row['canonical']['bricks'],
                differing_r16=cmp['r16_differences'] if cmp['logical_leaves_equal'] else None,
                note='Unaligned samples are unscored when logical leaves differ; the native legacy count is not a count of differing matched values'))
    diagnostics=[]
    for run in data['pruned_cost']['runs']:
        rows=[r for r in run['native']['runs'][0]['trials'] if not r['warmup']]
        assert len(rows)==3
        diagnostics.append(dict(subject=run['case']['subject'],operation=run['case']['operation'],axis=run['regions'],
            median_ms=statistics.median(r['ready_to_render_ms'] for r in rows),
            region_prepare_ms=statistics.median(r['region_prepare_ms'] for r in rows),qualified=False))
    probes=[]
    for run in data['prune_probe']['runs']:
        rows=run['native']['samples']
        probes.append(dict(subject=run['name'],axis=run['axis'],samples=len(rows),
            float_differences=sum(r['full_bits']!=r['pruned_bits'] for r in rows),
            raw_r16_differences=sum(r['full_raw_half']!=r['pruned_raw_half'] for r in rows),
            packed_raw_differences=sum(r['pruned_packed_half']!=r['pruned_raw_half'] for r in rows),
            max_float_difference=max(abs(r['full']-r['pruned']) for r in rows),example=rows[0]))
    summary=dict(runtime_field_source='207d2517',timing_source=data['timing']['source_head'],
        field_binary_sha256=data['timing']['binary_sha256'],field_kernels=required,
        original_public_binary_unchanged=digest(ROOT/'build/release/projects/sdf/sdf')==base['public_binary_sha256'],
        original_shaders=len(base['shader_hashes']),protected_unchanged=len(base['protected'])-len(allowed),
        timing=timing,speedups=speedups,timing_runs=48,measured_commits=480,warmup_commits=warmup,
        qualified_pairs=len(pairs),stored_texel_comparisons=sum(p['candidate']['texels'] for p in pairs),
        ordinary_cases=35,chain_lengths=[8,16],overflow_cases=2,negative_requests=len(data['faults']['negative']),
        sequence_frames=66,source_replays=4,sequences=sequences,work=work,rejected_pruning=rejected,
        pruning_cost_diagnostic=diagnostics,prune_probes=probes,
        field_and_list_peak_bytes=max(r['geometry_payload_bytes'] for rows in groups.values() for r in rows),
        snapshot_staging_peak_bytes=data['qualification']['runs'][0]['native']['snapshot_staging_peak_bytes'],
        image_exports=len(cache),image_source_checks=len(image_checks),report_builder_sha256=digest(Path(__file__)))
    save(PACKET/'summary.json',summary);save(export/'summary.json',summary)
    save(PACKET/'image-checks.json',dict(passed=True,unique_images=len(cache),sources=image_checks))
    save(PACKET/'cache-pairs.json',pairs)
    for name,path in files.items():
        shutil.copy2(path,PACKET/(name+'.json'));shutil.copy2(path,export/(name+'.json'))
    for name in ('raw-manifest.json','cache-pairs.json','image-checks.json'):
        shutil.copy2(PACKET/name,export/name)
    template=(PACKET/'report.html').read_text()
    html=template.replace('@@DATA',json.dumps(summary,separators=(',',':')).replace('</','<\\/'))
    assert '@@' not in html
    (export/'index.html').write_text(html)
    save(PACKET/'report-manifest.json',dict(export=str(export),assets={str(p.relative_to(export)):
        dict(bytes=p.stat().st_size,sha256=digest(p)) for p in sorted(export.rglob('*')) if p.is_file()}))
    print(json.dumps({k:summary[k] for k in ('qualified_pairs','stored_texel_comparisons','measured_commits',
        'image_exports','original_public_binary_unchanged','original_shaders','protected_unchanged')},indent=2))


if __name__=='__main__':main()
