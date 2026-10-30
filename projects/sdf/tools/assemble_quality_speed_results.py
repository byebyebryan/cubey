#!/usr/bin/env python3
"""Archive the completed quality/speed study and build its static report.

Run after native timing/capture work has stopped. Media work uses CPU only.
Analysis dependencies: numpy, scipy, Pillow; ffmpeg is used for review clips.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text())


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n')


def p95(values):
    values = sorted(values)
    k = .95 * (len(values) - 1)
    lo = int(k)
    return values[lo] + (values[min(lo + 1, len(values) - 1)] - values[lo]) * (k - lo)


def assemble(root, evidence, report):
    evidence.mkdir(parents=True, exist_ok=False)
    report.mkdir(parents=True, exist_ok=False)
    packets = {
        'timing-core.json': 'timing-final-core/timing.json',
        'timing-extended.json': 'timing-final-extended/timing.json',
        'quality.json': 'quality-qualified/quality.json',
        'captures.json': 'captures-qualified/captures.json',
        'poses.json': 'sequence-geometry-final/sequences.json',
        'temporal.json': 'native-review/native-temporal.json',
        'history.json': 'history/history.json',
        'publication.json': 'publication-final/publication.json',
        'contracts.json': 'contracts-final/contracts.json',
        'cell-oracle.json': 'cell-oracle/receipt.json',
        'compiler.json': 'compiler/statistics.json',
        'negative.json': 'negative-qualified/negative.json',
        'preservation.json': 'preservation-final.json',
        'baseline.json': 'initial-preservation.json',
        'roi.json': 'roi/roi.json',
        'fixture-aa.json': 'native-review/fixture-aa.json',
        'hybrid-context.json': 'hybrid-context/hybrid.json',
        'shadow-ablation.json': 'shadow-ablation/captures.json',
    }
    for name, source in packets.items():
        shutil.copy2(root / source, evidence / name)
    for route in ('sampled','source','refine03125','normal','combined'):
        shutil.copy2(root/'contracts-final'/(route+'.json'),evidence/('queries-'+route+'.json'))
    shutil.copy2(root/'contracts-final/fixtures.json',evidence/'selected-fixtures.json')
    shutil.copy2(Path('projects/sdf/comparison/fixtures.json'),evidence/'source-fixtures.json')
    timings = []
    grouped = defaultdict(list)
    for filename in ('timing-core.json', 'timing-extended.json'):
        packet = read(evidence / filename)
        assert packet['complete']
        for run in packet['runs']:
            for data in run['passes'].values():
                assert sorted(data['frame_indices']) == list(range(30, 180))
                assert len(data['values_ms']) == 150
                assert abs(p95(data['values_ms']) - data['p95_ms']) < 1e-6
            grouped[run['scene'], run['view'], run['route']].append(run)
    for (scene, view, route), runs in grouped.items():
        assert sorted(r['repeat'] for r in runs) == [1, 2, 3]
        values = [r['passes']['sdf.whole_scene']['p95_ms'] for r in runs]
        timings.append(dict(scene=scene, view=view, route=route,
            p95_min_ms=min(values), p95_max_ms=max(values), repeats=values,
            resources=runs[0]['metrics']['sdf.resources']))
    quality = read(evidence / 'quality.json')['comparisons']
    captures = read(evidence / 'captures.json')
    assert captures['complete'] and len(captures['runs']) == 50
    for row in captures['runs']:
        prefix = root / 'captures-qualified' / (row['scene']+'-'+row['view']+'-'+row['route'])
        for suffix, key in [('.png','image_sha256'),('.bin','geometry_sha256'),('.valid.png','validity_sha256')]:
            assert sha(prefix.with_suffix(suffix)) == row[key]
    poses = read(evidence / 'poses.json')
    assert poses['complete'] and len(poses['runs']) == 16
    for run in poses['runs']:
        assert len(run['frames']) == 24
        for frame in run['frames']:
            assert not any(frame[k] for k in ('failed','field_builds','region_builds','cone_build_delta'))
            for path, key in [('frame_png','image_sha256'),('validated_png','validated_image_sha256'),('quality_file','geometry_sha256')]:
                assert sha(frame[path]) == frame[key]
    temporal = read(evidence / 'temporal.json')['comparisons']
    pub = read(evidence / 'publication.json')
    assert pub['complete'] and len(pub['runs']) == 24
    pub_groups = defaultdict(list)
    for run in pub['runs']:
        pub_groups[run['scene'],run['route']].extend(run['actions'])
    publication = []
    for (scene,route), rows in pub_groups.items():
        edit = [r['request_to_waited_gpu_completion_ms'] for r in rows if r['label']=='edit']
        geometry = [r['request_to_waited_gpu_completion_ms'] for r in rows if r['label'] in ('edit','undo','redo','retry')]
        publication.append(dict(scene=scene,route=route,edit_ms=edit,edit_p95_ms=p95(edit),
            geometry_action_p95_ms=p95(geometry),geometry_action_samples=geometry))
    stop_results = []
    for candidate in publication:
        if candidate['route'] not in ('combined','refine03125'): continue
        control = next(p for p in publication if p['scene']==candidate['scene'] and p['route']=='sampled')
        a,b = candidate['geometry_action_p95_ms'],control['geometry_action_p95_ms']
        stop_results.append(dict(scene=candidate['scene'],route=candidate['route'],
            increase_ms=a-b,increase_fraction=a/b-1,stop=(a>b*1.25 and a>b+10)))
    contracts = read(evidence/'contracts.json')
    assert contracts['complete']
    assert next(r for r in contracts['runs'] if r['route']=='source')['source_geometry_score']['passed']
    assert read(evidence/'cell-oracle.json')['passed']
    preserved = read(evidence/'preservation.json')
    assert not preserved['changed_original_spvs']
    candidates = [r for r in timings if r['route'] in ('refine03125','combined')]
    resources = [r for r in timings if r['route']=='combined']
    shadow = shadow_comparison(root)
    encoder = {filename: [run[k].split(',')[1].strip() for run in read(evidence/filename)['runs']
        for k in ('gpu_before','gpu_after')] for filename in ('timing-core.json','timing-extended.json','publication.json')}
    summary = dict(date='2026-10-29', implementation_commit='1b4faefb',
        binary_sha256=preserved['binary_sha256'], device='RTX 5070 Ti / NVIDIA 610.57.04',
        protocol='1920x1080 SS1, 180 frames, 30 warmup, 150 samples/pass, 3 serial interleaved repeats; encoder 0 percent at all 348 final timing boundaries, uncontrolled shared host',
        encoder_boundary_observations=encoder,
        timing_runs=sum(len(read(evidence/f)['runs']) for f in ('timing-core.json','timing-extended.json')),
        timings=timings, quality=quality, temporal=temporal, publication=publication,
        publication_stop_results=stop_results, shadow_ablation=shadow,
        resource_bounds=dict(diagnostic_logical_bytes=16+1920*1080*16,
            candidate_metadata_max_bytes=max(r['resources']['region_bytes'] for r in resources),
            overlapping_field_region_requested_bytes=max(2*(r['resources']['region_bytes']+r['resources']['field_bytes']) for r in resources),
            scope='Requested payload, not Vulkan allocator residency or total VRAM; CPU region scratch reported separately in timing packets'),
        gates=dict(all_ten_view_candidate_gpu_p95_below_16_7_ms=all(r['p95_max_ms']<16.7 for r in candidates),
            candidate_publication_stop_triggered=any(r['stop'] for r in stop_results),
            source_frozen_contracts_pass=True, controlled_cell_contracts_pass=True,
            cached_detail_preserving_profile_pass=False, native_pose_frames=384,
            human_visual_acceptance=False, windowed_candidate_qualification=False,
            idle_gpu_timing=False, default_changed=False),
        decision='Retain C and C+N as explicit private headless preview candidates. Reject detail-preserving promotion; keep sampled default and source quality reference.',
        evidence_identities={name:{k:v for k,v in read(evidence/name).items() if k in ('binary_sha256','source_head','source_diff_sha256','tool_sha256')} for name in packets},
        exclusions=['RGB silhouette inference failed calibration at 32 pixels; replaced with direct native geometry.',
            'Per-view native diagnostic/image discrepancies are excluded, never scored as improvements; silhouette masks additionally exclude two-pixel neighborhoods.',
            'Earlier failed CSV, descriptor and layout attempts are retained in raw outputs and excluded.',
            'The hybrid ladder is a short context pilot, not the repeated final matrix.',
            'Compiler local-memory statistics are unusable on this driver and excluded.',
            'Source-compatible normal angles exclude depth disagreements over two voxel spacings; tails and incompatible counts remain visible.'])
    save(evidence/'summary.json',summary)
    media(root, report, quality)
    shutil.copytree(evidence, report/'evidence')
    template=Path(__file__).with_name('quality_speed_report.html').read_text()
    (report/'index.html').write_text(template.replace('__STUDY_DATA__',json.dumps(summary).replace('</','<\\/')))
    manifest={str(p.relative_to(evidence)):sha(p) for p in sorted(evidence.rglob('*')) if p.is_file()}
    save(evidence/'manifest.json',manifest)
    shutil.copy2(evidence/'manifest.json',report/'evidence/manifest.json')
    print(json.dumps(dict(timing_runs=summary['timing_runs'],gates=summary['gates'],publication=stop_results,
        evidence=str(evidence),report=str(report)),indent=2))


def shadow_comparison(root):
    import numpy as np
    from PIL import Image
    result=[]
    for scene in ('guardian','robot'):
        for view in ('preset','close'):
            for shadows,folder in [(1,'captures-qualified'),(0,'shadow-ablation')]:
                prefix=root/folder/(scene+'-'+view)
                a=np.asarray(Image.open(str(prefix)+'-source.png').convert('RGB')).astype(np.int16)
                for route in ('sampled','combined'):
                    b=np.asarray(Image.open(str(prefix)+'-'+route+'.png').convert('RGB')).astype(np.int16)
                    result.append(dict(scene=scene,view=view,shadows=shadows,route=route,
                        mean_abs_channel=float(abs(a-b).mean()),pixels_over_8=int((abs(a-b).max(axis=-1)>8).sum())))
    return result


def media(root, report, quality):
    import numpy as np
    from PIL import Image, ImageDraw
    from scipy import ndimage
    from quality_speed_analysis import geometry
    images=report/'images';images.mkdir()
    for scene in ('guardian','robot'):
        for view in ('preset','side','rear','close','distant'):
            for route in ('sampled','source','refine03125','normal','combined'):
                name=scene+'-'+view+'-'+route
                shutil.copy2(root/'captures-qualified'/(name+'.png'),images/(name+'.png'))
                if route!='source':
                    for suffix in ('.normal-error.png','.mask.png'):
                        shutil.copy2(root/'quality-qualified'/(name+suffix),images/(name+suffix))
    # Select an interior shading crop where C+N reduces source-reference error.
    crop_records=[]
    for scene in ('guardian','robot'):
        prefix=root/'captures-qualified'/(scene+'-preset')
        arrays={r:np.asarray(Image.open(str(prefix)+'-'+r+'.png').convert('RGB')).astype(np.int16)
                for r in ('sampled','source','refine03125','combined')}
        d,_=geometry(Path(str(prefix)+'-source.bin'))
        foreground=ndimage.binary_erosion(d>=0,iterations=12)
        benefit=np.maximum(abs(arrays['sampled']-arrays['source']).mean(axis=-1)-
                           abs(arrays['combined']-arrays['source']).mean(axis=-1),0)*foreground
        score=ndimage.uniform_filter(benefit,size=64)
        y,x=np.unravel_index(np.argmax(score),score.shape)
        box=(max(int(x)-64,0),max(int(y)-64,0),min(int(x)+64,1920),min(int(y)+64,1080))
        montage(images/(scene+'-shading-crop.png'),arrays,box)
        crop_records.append(dict(scene=scene,kind='interior-shading',box=list(box),
            selection='Maximum 64-pixel mean reduction in RGB8 error, source foreground eroded 12 pixels; crop shared across all policies'))
    # Worst Robot close motion silhouette, from actual native geometry.
    poses=read(root/'sequence-geometry-final/sequences.json')
    runs={r['route']:r for r in poses['runs'] if r['scene']=='robot' and r['movement']=='dolly'}
    a,_=geometry(Path(runs['source']['frames'][0]['quality_file']))
    b,_=geometry(Path(runs['combined']['frames'][0]['quality_file']))
    am,bm=a>=0,b>=0
    ae=am&~ndimage.binary_erosion(am);be=bm&~ndimage.binary_erosion(bm)
    distances=ndimage.distance_transform_edt(~be)*ae
    y,x=np.unravel_index(np.argmax(distances),distances.shape)
    box=(max(int(x)-48,0),max(int(y)-48,0),min(int(x)+48,1920),min(int(y)+48,1080))
    arrays={r:np.asarray(Image.open(runs[r]['frames'][0]['frame_png']).convert('RGB')) for r in runs}
    montage(images/'robot-missing-feature.png',arrays,box)
    crop_records.append(dict(scene='robot',kind='missing-feature',movement='dolly',frame=0,box=list(box),
        selection='Source boundary point farthest from C+N boundary; direct per-frame geometry, no RGB mask inference'))
    save(report/'crops.json',crop_records)
    # Four independent native frames per pose; resizing is presentation only.
    clips=report/'clips';clips.mkdir()
    for scene in ('guardian','robot'):
        for movement in ('arc','dolly'):
            inputs=[]
            for route in ('sampled','refine03125','combined','source'):
                inputs+=['-framerate','12','-i',str(root/'sequences'/f'{scene}-{movement}-{route}.frame-%d.png')]
            graph=';'.join(f'[{i}:v]scale=640:360,setsar=1[v{i}]' for i in range(4))
            graph+=';[v0][v1][v2][v3]xstack=inputs=4:layout=0_0|640_0|0_360|640_360[out]'
            subprocess.run(['ffmpeg','-nostdin','-hide_banner','-loglevel','error',*inputs,
                '-filter_complex',graph,'-map','[out]','-frames:v','24','-c:v','libx264','-threads','4',
                '-crf','17','-pix_fmt','yuv420p','-movflags','+faststart',str(clips/f'{scene}-{movement}.mp4')],check=True)
    # Retain every ROI route plus the independent 16-sample coverage reference.
    for p in (root/'roi').glob('*.png'):shutil.copy2(p,images/p.name)
    for p in (root/'native-review').glob('*-cpu-aa.png'):shutil.copy2(p,images/p.name)


def montage(path, arrays, box):
    import numpy as np
    from PIL import Image, ImageDraw
    names=[('sampled','S: cached'),('refine03125','C: refined cache'),('combined','C+N: source normals'),('source','A: analytical')]
    canvas=Image.new('RGB',(384*4,420),(15,21,30));draw=ImageDraw.Draw(canvas)
    for i,(route,label) in enumerate(names):
        img=Image.fromarray(np.asarray(arrays[route],dtype=np.uint8)).crop(box)
        img=img.resize((384,384),Image.Resampling.NEAREST)
        canvas.paste(img,(i*384,36));draw.text((i*384+10,12),label,fill=(240,240,240))
    canvas.save(path)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True)
    p.add_argument('--evidence',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True)
    a=p.parse_args();assemble(a.root,a.evidence,a.report)


if __name__=='__main__': main()
