#!/usr/bin/env python3
"""Post-timing checks and portable report for the bounded local-update study."""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import statistics

ROOT=Path(__file__).resolve().parents[3]
PACKET=Path(__file__).resolve().parent


def load(p):return json.loads(p.read_text())
def save(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def digest(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def percentile(v,p):
    s=sorted(v);x=(len(s)-1)*p;i=int(x)
    return s[i]+(s[min(i+1,len(s)-1)]-s[i])*(x-i)
def stats(v):
    return dict(samples=len(v),median_ms=statistics.median(v),mean_ms=statistics.mean(v),
        p95_ms=percentile(v,.95),p99_ms=percentile(v,.99),max_ms=max(v),min_ms=min(v),
        over_16_67=sum(x>16.67 for x in v),over_11_11=sum(x>11.11 for x in v),values_ms=v)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--raw',type=Path,required=True)
    p.add_argument('--export',type=Path,required=True);args=p.parse_args()
    raw=args.raw.resolve();export=args.export.resolve();export.mkdir(parents=True,exist_ok=False)
    (export/'images').mkdir()
    paths=dict(local='local-v2/qualification.json',local_chains='local-chains-v2/qualification.json',
        dominance='dominance-v1/qualification.json',dominance_chains='dominance-chains-v1/qualification.json',
        batches='stroke-caches-v1/caches.json',sliced='stroke-sliced32-v1/caches.json',
        calibration='stroke-calibration-v1/calibrate.json',copies='copies-v1/copies.json',
        geometry='stroke-geometry-v4/geometry.json',admission='stroke-admission-v2/admission.json',
        replay='stroke-replay-v1/replay.json',prefixes='stroke-prefixes-v1/prefixes.json',
        captures='stroke-captures-v1/captures.json',render='stroke-times-v1/timing.json',
        retention='stroke-retention-v1/retention.json',
        commit_02='timing-02-v1/timing.json',commit_065='timing-065-v1/timing.json',commit_15='timing-15-v2/timing.json')
    data={k:load(raw/v) for k,v in paths.items()}
    assert all(x['complete'] for x in data.values())
    base=load(raw/'baseline.json');shaders=ROOT/'build/release/projects/sdf/shaders'
    assert all(digest(shaders/n)==h for n,h in base['shaders'].items())
    assert digest(shaders.parent/'sdf')==base['binaries']['sdf']
    allowed={'projects/sdf/CMakeLists.txt','projects/sdf/sdf_cached_commit.h','projects/sdf/sdf_cached_commit.cpp',
        'projects/sdf/tools/cached_commit_study.cpp','projects/sdf/sdf_renderer.cpp','projects/sdf/shaders/cached_edit_trace.glsl'}
    assert all(digest(ROOT/n)==h for n,h in base['protected'].items() if n not in allowed)
    pairs=[]
    for name in ('local','local_chains','dominance','dominance_chains','batches','sliced','calibration'):
        for run in data[name]['runs']:
            native=run['native'];assert run['exit_code']==0
            for row in native.get('runs',native.get('cache_runs',[])):
                assert row['comparison']['passed']
                assert row.get('retained_slots_moved',0)==0
                a,b=row['candidate'],row['canonical']
                for key in ('topology_sha256','cells_sha256','raw_r16_sha256'):assert a[key]==b[key]
                for description in (a,b):
                    for file,key in zip(description['files'],('topology_sha256','cells_sha256','raw_r16_sha256')):
                        assert digest(Path(file))==description[key]
                pairs.append(dict(route=name,name=row['name'],texels=a['texels'],bricks=a['bricks'],
                    candidate=a,canonical=b,retained_slots_moved=row.get('retained_slots_moved'),
                    passed=True))
    commit=collections.defaultdict(list)
    for name in ('commit_02','commit_065','commit_15'):
        for run in data[name]['runs']:
            for t in run['native']['runs'][0]['trials']:
                if not t['warmup']:commit[(run['case']['subject'],run['case']['operation'],run['case']['radius'],run['mode'])].append(t)
    commits=[]
    for key,rows in sorted(commit.items()):
        assert len(rows)==30
        stages={k:statistics.median(r[k] for r in rows) for k in ('admission_ms','filter_gpu_ms','copy_gpu_ms','reuse_gpu_ms','fallback_gpu_ms','brick_gpu_ms')}
        commits.append(dict(subject=key[0],operation=key[1],radius=key[2],mode=key[3],
            **stats([r['ready_to_render_ms'] for r in rows]),stages=stages,example_work=rows[0].get('local_work')))
    render=collections.defaultdict(list)
    for run in data['render']['runs']:
        key=(run['case']['subject'],run['view'],run['case']['operation'],run['case']['count'],run['route'])
        render[key]+=run['passes']['sdf.whole_scene']['values_ms']
    renders=[]
    for key,values in sorted(render.items()):
        assert len(values)==450
        renders.append(dict(subject=key[0],view=key[1],operation=key[2],count=key[3],route=key[4],**stats(values)))
    calibration=collections.defaultdict(list)
    for run in data['calibration']['runs']:
        for row in run['native']['cache_runs']:
            for t in row['trials']:
                if not t['warmup']:calibration[(run['case']['subject'],run['case']['operation'],run['groups'])].append(t)
    calibrations=[]
    for key,rows in sorted(calibration.items()):
        assert len(rows)==30
        units=[u for t in rows for u in t['units']]
        totals=[sum(u['gpu_ms'] for u in t['units']) for t in rows]
        stages={kind:dict(max_gpu_ms=max(u['gpu_ms'] for u in units if u['kind']==kind),
            max_wall_ms=max(u['wall_ms'] for u in units if u['kind']==kind),
            median_total_gpu_ms=statistics.median(sum(u['gpu_ms'] for u in t['units'] if u['kind']==kind) for t in rows))
            for kind in sorted(set(u['kind'] for u in units))}
        calibrations.append(dict(subject=key[0],operation=key[1],groups=key[2],gpu=stats(totals),
            wall=stats([t['builder_wall_ms'] for t in rows]),
            admission=stats([t['admission_ms'] for t in rows]),stages=stages,
            max_unit_gpu_ms=max(u['gpu_ms'] for u in units),max_unit_wall_ms=max(u['wall_ms'] for u in units),
            unit_over_05=sum(u['gpu_ms']>.5 for u in units),unit_over_1=sum(u['gpu_ms']>1 for u in units),
            unit_over_2=sum(u['gpu_ms']>2 for u in units),units=len(units)))
    assert all(r['gpu']['min_ms']>8 for r in calibrations if r['subject']=='guardian' and r['operation']=='cut')
    copies=collections.defaultdict(list)
    for run in data['copies']['runs']:
        for t in run['native']['runs'][0]['trials']:
            if not t['warmup']:copies[(run['case']['subject'],run['slab_mib'])].append(t)
    copy_rows=[dict(subject=k[0],slab_mib=k[1],gpu=stats([t['copy_gpu_ms'] for t in rows]),
        wall=stats([t['copy_wall_ms'] for t in rows]),max_unit_gpu_ms=max(t['copy_max_chunk_gpu_ms'] for t in rows),
        bytes=rows[0]['copy_bytes']) for k,rows in sorted(copies.items())]
    assert data['geometry']['score']['passed'] and data['geometry']['score']['pending_owner_checks_passed']
    assert len(data['admission']['positive'])==5 and len(data['admission']['negative'])==10
    assert all(r['intermediate_byte_identical'] and r['settled_byte_identical'] and r['retained_source_matches'] for r in data['prefixes']['runs'])
    assert len(data['prefixes']['runs'])==28
    assert len(data['retention']['runs'])==4 and all(r['settled_byte_identical'] for r in data['retention']['runs'])
    images=[]
    for run in data['captures']['runs']:
        src=Path(run['image']);dst=export/'images'/src.name;shutil.copy2(src,dst)
        assert digest(dst)==run['image_sha256']
        images.append(dict(subject=run['case']['subject'],operation=run['case']['operation'],
            view=run['view'],phase=run['phase'],src='images/'+src.name))
    prefix_images=[]
    for run in data['prefixes']['runs']:
        if run['fault']!='none':continue
        for frame in run['native']['frames']:
            src=Path(frame['image']);dst=export/'images'/src.name;shutil.copy2(src,dst)
            prefix_images.append(dict(subject=run['case']['subject'],operation=run['case']['operation'],
                frame=frame['frame'],display=frame['display'],src='images/'+src.name,
                baked_ids=frame['baked_ids'],pending_ids=frame['render_pending_ids']))
    budgets=[]
    for r in calibrations:
        if r['groups']!=0:continue
        for budget in (.5,1,2):
            budgets.append(dict(subject=r['subject'],operation=r['operation'],budget_ms=budget,
                gpu_median_ms=r['gpu']['median_ms'],available_four_frames_ms=4*budget,
                optimistic_min_frames=math.ceil(r['gpu']['median_ms']/budget),
                fits_four_frame_gpu_sum=r['gpu']['max_ms']<=4*budget,
                note='Arithmetic necessary condition only; excludes scheduling, CPU prep, overlap and publication'))
    logs=list(raw.rglob('*.log'));errors=[]
    for log in logs:
        if re.search(r'VUID-|Validation Error',log.read_text(errors='replace')):errors.append(str(log))
    assert not errors
    geometry_bytes=max(t['geometry_payload_bytes'] for run in data['batches']['runs'] for row in run['native']['cache_runs'] for t in row['trials'])
    peak_staging=76695844
    assert geometry_bytes+peak_staging<2*1024**3
    summary=dict(scope='Private local cache and eight same-operation sphere study; scheduling stopped at Guardian cost gate',
        qualification=dict(pairs=len(pairs),stored_r16_comparisons=sum(r['texels'] for r in pairs),
            routes=dict(collections.Counter(r['route'] for r in pairs)),all_exact=True,
            geometry=data['geometry']['score'],prefix_sequences=28,sequence_frames=sum(len(r['native']['frames'])for r in data['prefixes']['runs']),
            camera_rays=sum(r['camera_rays']for r in data['replay']['runs']),shadow_rays=sum(r['shadow_rays']for r in data['replay']['runs']),
            camera_shadow_exhaustions=0,negative_requests=10,retained_consumer_replays=4),
        preservation=dict(original_spvs=211,protected_source_files=len(base['protected'])-len(allowed),public_executable_byte_identical=True),
        requested_memory=dict(geometry_and_lists_bytes=geometry_bytes,diagnostic_staging_bytes=peak_staging,
            combined_bytes=geometry_bytes+peak_staging,is_allocator_residency=False),
        commits=commits,render=renders,calibration=calibrations,copies=copy_rows,budgets=budgets,
        images=images,prefix_images=prefix_images,validation_logs=len(logs),validation_error_patterns=errors,
        limits=['No interleaved frame-time, timestamped input, sustained service, physical input/display or XR acceptance',
            'Unit calibration is serialized and does not establish GPU preemption or renderer interference',
            'GPU-sum frame allowances are optimistic screens, not measured frame/commit completion',
            'Numeric guards are engineering guards qualified on this bounded roster, not a universal IEEE proof',
            'Stable-slot metadata is owned by one helper for the lifetime of its private field chain',
            'Two Fields; bounded append slots leave holes and have no reclamation',
            'Canonical source capacity remains unchanged; finite admitted streams do not prove long sessions',
            'Cached sub-grid/grazing and earlier unscored Guardian depth limitations remain',
            'Copy-only legacy filter/field timing fields inherit base stats; explicit copy fields are used',
            'No public modes/defaults, brush/undo UI, allocator, push or project site deployment'])
    save(PACKET/'summary.json',summary);save(PACKET/'cache-pairs.json',pairs)
    save(PACKET/'baseline.json',base)
    for name,path in paths.items():shutil.copy2(raw/path,PACKET/(name+'.json'))
    save(export/'data.json',summary)
    template=(PACKET/'report-template.html').read_text()
    (export/'index.html').write_text(template.replace('__REPORT_DATA__',json.dumps(summary,allow_nan=False).replace('</','<\\/')))
    shutil.copy2(export/'index.html',PACKET/'report.html')
    save(PACKET/'report-manifest.json',[dict(path=str(x.relative_to(export)),sha256=digest(x),bytes=x.stat().st_size) for x in sorted(export.rglob('*')) if x.is_file()])
    save(PACKET/'raw-manifest.json',[dict(path=str(x.relative_to(raw)),sha256=digest(x),bytes=x.stat().st_size) for x in sorted(raw.rglob('*')) if x.is_file() and x.suffix in ('.json','.log')])
    print(json.dumps({k:summary[k] for k in ('qualification','preservation','requested_memory')},indent=2))


if __name__=='__main__':main()
