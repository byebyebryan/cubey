#!/usr/bin/env python3
"""One bounded surface candidate, frozen sources, actual Terrain and Fluid consumers.

No generation or solver launch. Phase directories are exclusive; inputs, commands,
binaries, shader identities, pixel parity and measured scopes remain attributable.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'projects/fluid/fluid_25d'))
import run_native_presentation_v1 as fluid

SITES = ('temperate-mountain-valley', 'alpine-range', 'rolling-wet-lowland')
APP = ROOT / 'build/dev/projects/terrain/terrain'
SOURCES = ROOT / 'cache/terrain/sources/v1/landscape-variations'
GATE = dict(resolution=[1280, 720], added_gpu_resident_mib_ceiling=24,
            incremental_median_ms_ceiling=0.15, incremental_p95_ms_ceiling=0.15,
            visual='obvious at half size on at least two sites; no third-site regression',
            candidate_limit=1, focused_correction_limit=1,
            human_visual_acceptance='deferred')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, value):
    fluid.write_json_exclusive(path, value)


def inputs():
    result = {}
    for site in SITES:
        leaf = SOURCES / site
        result[site] = {p.name: digest(p) for p in sorted(leaf.iterdir()) if p.is_file()}
    return result


def identity():
    return {'terrain_binary': digest(APP), 'terrain_shaders': {
        p.name: digest(p) for p in sorted((APP.parent / 'shaders').glob('*.spv'))},
        'fluid': fluid.runtime_identity(include_source=False)}


def run(command, log):
    started = time.monotonic()
    p = subprocess.run(['rtk', 'proxy', *map(str, command)], cwd=ROOT,
        env={k: v for k, v in os.environ.items() if k not in
             ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY')},
        text=True, capture_output=True, timeout=180)
    fluid.write_text_exclusive(log, p.stdout + p.stderr)
    if p.returncode or 'vulkan validation error' in (p.stdout + p.stderr).lower():
        raise RuntimeError(f'capture failed; see {log}')
    return time.monotonic() - started


def terrain_command(site, model, out, view='surface', yaw=0, hour=9, altitude=200):
    return [APP, '--headless', '--width', '1280', '--height', '720',
        '--terrain-heightfield', SOURCES / site / 'heightfield.json',
        '--terrain-surface-fields', SOURCES / site / 'surface-fields.json',
        '--terrain-surface-model', model, '--terrain-camera-preset', 'backdrop',
        '--terrain-render-stride', '3', '--terrain-foreground-height', str(altitude),
        '--terrain-surface-detail', 'flat' if view == 'flat' else 'filtered-detail',
        '--debug-view', 'surface' if view == 'flat' else view,
        '--terrain-shadows', '--terrain-backdrop-azimuth', str(yaw),
        '--time-of-day-mode', 'solar', '--time-hours', str(hour),
        '--day-of-year', '172', '--latitude-degrees', '35', '--pause-time',
        '--no-clouds', '--output', out]


def capture(out, phase, quick=False):
    leaf = out / phase
    fluid.reserve_directory(leaf)
    frozen, runtime = inputs(), identity()
    write(leaf / 'predeclared-gate.json', GATE)
    rows = []
    plan = [('day-' + str(y), 'surface', y, 9, 200) for y in (0, 90, 180, 270)]
    if quick:
        plan = plan[:1]
    else:
        plan += [('raking', 'surface', 0, 6, 200), ('night', 'surface', 0, 0, 200),
                 ('stress', 'surface', 0, 9, 100)]
        plan += [(v, v, 0, 9, 200) for v in
                 ('material-albedo', 'material-weights', 'material-normal',
                  'material-roughness', 'flat')]
    models = ('mineral-control', 'climate-transition') if phase == 'baseline' else (
        'mineral-control', 'climate-transition', 'correlated-surface')
    for site in SITES:
        for model in models:
            for label, view, yaw, hour, altitude in plan:
                name = f'{site}-{model}-{label}'
                path = leaf / (name + '.png')
                command = terrain_command(site, model, path, view, yaw, hour, altitude)
                seconds = run(command, leaf / (name + '.log'))
                rows.append(dict(id=name, consumer='terrain', site=site, model=model,
                    view=label, command=list(map(str, command)), path=path.name,
                    sha256=digest(path), process_wall_s=seconds))
                print('captured ' + name, flush=True)
    for camera, t in (('overview', 0), ('runoff', 0), ('runoff', 6000)):
        for mode in (('legacy', 'climate') if phase == 'baseline' else
                     ('legacy', 'climate', 'correlated')):
            a = fluid.still_asset('rain-on', camera, t)
            a.update(width=1280, height=720)
            name = f'fluid-{camera}-{t}-{mode}'
            path = leaf / (name + '.png')
            command = fluid.app_command(a, path, 'scenic', False) + [
                '--fluid25d-scenic-material', 'macro', '--fluid25d-scenic-surface-source',
                str(SOURCES / SITES[0] / 'heightfield.json'),
                '--fluid25d-scenic-surface-mode', mode]
            seconds = run(command, leaf / (name + '.log'))
            rows.append(dict(id=name, consumer='fluid', site=SITES[0], model=mode,
                view=f'{camera}-{t}', command=list(map(str, command)), path=path.name,
                sha256=digest(path), process_wall_s=seconds))
    if frozen != inputs() or runtime != identity():
        raise RuntimeError('inputs/runtime changed during capture')
    write(leaf / 'manifest.json', dict(inputs=frozen, runtime=runtime, assets=rows,
        numerical_inputs='unchanged; no solver launch', gate=GATE,
        render_state='rejected material prototype' if phase=='candidate' else
                     'shared source fields, original materials' if phase=='final' else 'baseline'))


def review(out, phase='candidate'):
    old = json.loads((out / 'baseline/manifest.json').read_text())
    new = json.loads((out / phase / 'manifest.json').read_text())
    prefix='' if phase=='candidate' else 'final-'
    if old['inputs'] != new['inputs']:
        raise RuntimeError('source identity changed')
    after = {a['id']: a for a in new['assets']}
    parity = {a['id']: a['sha256'] == after[a['id']]['sha256'] for a in old['assets']}
    if not all(parity.values()):
        raise RuntimeError('legacy pixels changed: ' + str([k for k,v in parity.items() if not v]))
    for view in ('day-0', 'day-90', 'day-180', 'day-270', 'raking', 'night', 'stress',
                 'material-albedo', 'material-weights', 'material-normal', 'material-roughness'):
        commands,filters,slots=[],[],[]
        index=0
        font=fluid.find_font()
        for row, site in enumerate(SITES):
            for col, model in enumerate(('mineral-control', 'climate-transition', 'correlated-surface')):
                item = after.get(f'{site}-{model}-{view}')
                if not item:
                    continue
                commands += ['-i',str(out/phase/item['path'])]
                title=site+' / '+model
                filters.append(f"[{index}:v]scale=640:360:flags=lanczos,pad=640:386:0:26:color=0x18212a,drawtext=fontfile='{font}':text='{title}':x=8:y=5:fontsize=15:fontcolor=white[v{index}]")
                slots.append(f'[v{index}]')
                index+=1
        if index:
            layout='|'.join(f'{(i%3)*640}_{(i//3)*386}' for i in range(index))
            filters.append(''.join(slots)+f'xstack=inputs={index}:layout={layout}[out]')
            run(['ffmpeg','-v','error','-n',*commands,'-filter_complex_threads','1',
                 '-filter_complex',';'.join(filters),'-map','[out]','-frames:v','1',out/(prefix+view+'.png')],
                out/(prefix+view+'.assembly.log'))
    write(out / (prefix+'parity.json'), dict(pixel_parity=parity, gate=GATE,
                                  visual_verdict='requires inspection; not inferred from differences'))


def profile(out, final=False):
    leaf=out/('profiles-final' if final else 'profiles')
    fluid.reserve_directory(leaf)
    frozen,runtime=inputs(),identity()
    rows=[]
    write(leaf/'predeclared-gate.json',GATE)
    for batch in range(3):
        models=('mineral-control','correlated-surface') if batch%2==0 else (
            'correlated-surface','mineral-control')
        for model in models:
            name=f'terrain-{model}-{batch}'
            prefix=leaf/name
            command=terrain_command(SITES[0],model,prefix.with_suffix('.mp4'),yaw=90)
            command += ['--capture','video','--frames','120','--fps','60',
                '--profile-output',str(prefix),'--profile-warmup-frames','12','--profile-diagnostics']
            wall=run(command,leaf/(name+'.log'))
            totals={}
            with prefix.with_suffix('.passes.csv').open() as stream:
                for row in csv.DictReader(stream):
                    if row.get('kind')=='gpu' and row.get('label') in (
                            'terrain shadow','terrain surface','terrain stage proxy',
                            'terrain atmosphere','terrain post'):
                        frame=int(row['frame_index'])
                        if frame>=12:
                            totals[frame]=totals.get(frame,0)+float(row['duration_ms'])
            if len(totals)<100:
                raise RuntimeError('missing matched terrain GPU timestamp scope')
            values=sorted(totals.values())
            metrics=list(csv.DictReader(prefix.with_suffix('.metrics.csv').open()))
            last={r['name']:r['value'] for r in metrics if r['category']=='terrain.backdrop'}
            rows.append(dict(consumer='terrain',model=model,batch=batch,process_wall_s=wall,
                median_ms=statistics.median(values),p95_ms=values[int(len(values)*.95)],
                samples=len(values),metrics=last,command=list(map(str,command))))
        for mode in (('legacy','correlated') if batch%2==0 else ('correlated','legacy')):
            name=f'fluid-{mode}-{batch}'
            prefix=leaf/name
            a=fluid.video_asset(name,'rain-on','runoff',6000,120,30,5)
            a.update(width=1280,height=720,profile_warmup_frames=12)
            command=fluid.app_command(a,prefix.with_suffix('.mp4'),'scenic',False,prefix)+[
                '--fluid25d-scenic-material','macro','--fluid25d-scenic-surface-source',
                str(SOURCES/SITES[0]/'heightfield.json'),'--fluid25d-scenic-surface-mode',mode]
            wall=run(command,leaf/(name+'.log'))
            summary=fluid.profile_summary(prefix,12,108)
            rows.append(dict(consumer='fluid',model=mode,batch=batch,process_wall_s=wall,
                median_ms=summary['gpu_median_ms'],p95_ms=summary['gpu_p95_ms'],command=command))
        print('profile batch '+str(batch),flush=True)
    if inputs()!=frozen or identity()!=runtime:
        raise RuntimeError('runtime/inputs changed during profile')
    summaries={}
    for consumer,control,candidate in (('terrain','mineral-control','correlated-surface'),
                                       ('fluid','legacy','correlated')):
        result={mode:{key:statistics.median(r[key] for r in rows
                if r['consumer']==consumer and r['model']==mode) for key in ('median_ms','p95_ms')}
                for mode in (control,candidate)}
        result['delta']={key:result[candidate][key]-result[control][key]
                         for key in ('median_ms','p95_ms')}
        result['status']='PASS' if all(v<=.15 for v in result['delta'].values()) else 'FAIL'
        summaries[consumer]=result
    write(leaf/'result.json',dict(inputs=frozen,runtime=runtime,rows=rows,summaries=summaries,
        gate=GATE,scope='headless GPU timestamps; source preparation, uploads and CUDA excluded'))


def archive(out):
    leaf=out/'prototype-runtime'
    fluid.reserve_directory(leaf)
    expected=json.loads((out/'candidate/manifest.json').read_text())['runtime']
    if expected!=identity():
        raise RuntimeError('prototype runtime changed before archival')
    for name,app in (('terrain',APP),('fluid',fluid.APP)):
        destination=leaf/name
        destination.mkdir()
        shutil.copy2(app,destination/app.name)
        shutil.copytree(app.parent/'shaders',destination/'shaders')
    write(leaf/'identity.json',expected)


def seal(out):
    runtime,frozen=identity(),inputs()
    phases={name:json.loads((out/name/'manifest.json').read_text())
            for name in ('baseline','candidate','final')}
    if phases['final']['runtime']!=runtime:
        raise RuntimeError('final captures are not from the current runtime')
    for name,phase in phases.items():
        if phase['inputs']!=frozen:
            raise RuntimeError('immutable source changed: '+name)
        for item in phase['assets']:
            if digest(out/name/item['path'])!=item['sha256']:
                raise RuntimeError('capture hash changed: '+item['id'])
    old=phases['baseline']['runtime']
    if old['terrain_shaders']!=runtime['terrain_shaders']:
        raise RuntimeError('final Terrain renderer differs from baseline shader binaries')
    before=old['fluid']['compiled_shaders']['files']
    after=runtime['fluid']['compiled_shaders']['files']
    changed=[n for n,h in before.items() if after.get(n)!=h]
    if changed!=['fluid_25d_scenic_terrain.frag.spv'] or before.keys()!=after.keys():
        raise RuntimeError('numerical/water/unrelated shader changed: '+str(changed))
    parity=json.loads((out/'final-parity.json').read_text())['pixel_parity']
    if not parity or not all(parity.values()):
        raise RuntimeError('final legacy pixel parity failed')
    cases=ET.parse(out/'gates-final.xml').getroot().findall('.//testcase')
    if not cases or any(c.find('failure') is not None or c.find('error') is not None or
                        c.find('skipped') is not None for c in cases):
        raise RuntimeError('final full test gate failed or skipped')
    performance=json.loads((out/'profiles-final/result.json').read_text())
    if performance['runtime']!=runtime or any(s['status']!='PASS' for s in performance['summaries'].values()):
        raise RuntimeError('final matched performance gate failed')
    prototype=json.loads((out/'prototype-runtime/identity.json').read_text())
    if prototype!=phases['candidate']['runtime']:
        raise RuntimeError('archived prototype identity differs from rejected captures')
    archive_root=out/'prototype-runtime'
    if digest(archive_root/'terrain/terrain')!=prototype['terrain_binary']:
        raise RuntimeError('archived Terrain executable changed')
    if digest(archive_root/'fluid/fluid_25d')!=prototype['fluid']['executable_sha256']:
        raise RuntimeError('archived Fluid executable changed')
    write(out/'acceptance.json',dict(status='PASS',shared_contract='PASS',
        visual_recipe='REJECTED and removed; source-aligned masks retained as opt-in study',
        human_visual_acceptance='deferred; primary reviewed captures, no user GUI approval',
        full_tests=len(cases),legacy_pixel_matches=len(parity),runtime=runtime,
        inputs=frozen,performance=performance['summaries'],
        shader_changes=changed,added_terrain_gpu_bytes=0,added_fluid_mask_gpu_bytes=1398100,
        resident_scope='incremental 512x512 RGBA8 full mip chain over prior two-mask study'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase', choices=('baseline', 'candidate', 'final', 'review', 'review-final', 'profile', 'profile-final', 'archive', 'seal'))
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--quick', action='store_true')
    args = p.parse_args()
    out = args.out.resolve()
    if out.parent != (ROOT/'outputs/terrain').resolve() or not out.name.startswith('correlated-surface-v1-'):
        raise ValueError('use an exclusive outputs/terrain/correlated-surface-v1-* leaf')
    if args.phase == 'review':
        review(out)
    elif args.phase == 'review-final':
        review(out,'final')
    elif args.phase == 'profile':
        profile(out)
    elif args.phase == 'profile-final':
        profile(out,True)
    elif args.phase == 'archive':
        archive(out)
    elif args.phase == 'seal':
        seal(out)
    else:
        capture(out, args.phase, args.quick)


if __name__ == '__main__':
    main()
