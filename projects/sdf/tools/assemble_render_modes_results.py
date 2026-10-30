#!/usr/bin/env python3
"""Archive native render-mode qualification and build its static HTML report."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def read(p):return json.loads(Path(p).read_text())
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,d):Path(p).write_text(json.dumps(d,indent=2)+'\n')
def p95(a):
    a=sorted(a);k=.95*(len(a)-1);i=int(k)
    return a[i]+(a[min(i+1,len(a)-1)]-a[i])*(k-i)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    root,ev,report=a.root,a.evidence,a.report;ev.mkdir(parents=True,exist_ok=False);report.mkdir(parents=True,exist_ok=False)
    packets={'sampling.json':'sampling/sampling.json','timing.json':'timing-qualified/timing.json',
        'aliases.json':'aliases-rootfar/aliases.json','transitions.json':'transitions-rootfar/transitions.json',
        'history.json':'history-rootfar/history.json','features.json':'features/features.json',
        'window.json':'window-final/window.json','balanced-window-reload.json':'window-reload/windowed-interaction.json',
        'balanced-window-history.json':'window-history/windowed-interaction.json',
        'analytic-window-reload.json':'window-rootfar-reload/windowed-interaction.json',
        'analytic-window-history.json':'window-rootfar-history/windowed-interaction.json',
        'publication.json':'publication/publication.json','captures.json':'captures/captures.json',
        'motion.json':'motion/motion.json','large-transitions.json':'large-transitions/large-transitions.json',
        'working-material.json':'working-material/working-material.json',
        'workshop-window.json':'window-workshop/window.json','defaults.json':'defaults/defaults.json',
        'native-color-screen.json':'native-color-screen.json',
        'contracts.json':'contracts/contracts.json','negative.json':'negative/negative.json',
        'baseline.json':'baseline.json'}
    for name,source in packets.items():shutil.copy2(root/source,ev/name)
    # Keep the stopped A16 investigation explicitly separate from acceptance.
    legacy=read(root/'timing-final/timing.json')
    legacy['runs']=[r for r in legacy['runs'] if r['mode']=='analytic']
    legacy['excluded_from_final_acceptance']=True;legacy['reason']=read(root/'timing-final/exclusion.json')['reason']
    save(ev/'a16-investigation.json',legacy)
    grouped=defaultdict(list)
    timing=read(ev/'timing.json');assert timing['complete'] and len(timing['runs'])==108
    for row in timing['runs']:
        for data in row['passes'].values():
            assert sorted(data['frame_indices'])==list(range(30,180))
            assert abs(p95(data['values_ms'])-data['p95_ms'])<1e-6
        grouped[row['scene'],row['view'],row['mode'],row['supersample']].append(row)
    timings=[]
    for (scene,view,mode,ss),rows in grouped.items():
        assert sorted(r['repeat'] for r in rows)==[1,2,3]
        values=[r['passes']['sdf.whole_scene']['p95_ms'] for r in rows]
        timings.append(dict(scene=scene,view=view,mode=mode,ss=ss,low=min(values),high=max(values),
            resources=rows[0]['metrics']['sdf.resources'],repeats=values))
    pub=read(ev/'publication.json');assert pub['complete'] and len(pub['runs'])==12
    pubs=[]
    for scene in ('guardian','robot'):
        for route in ('sampled','combined'):
            rows=[a for r in pub['runs'] if r['scene']==scene and r['route']==route for a in r['actions']]
            geometry=[r['request_to_waited_gpu_completion_ms'] for r in rows if r['label'] in ('edit','undo','redo','retry')]
            pubs.append(dict(scene=scene,mode={'sampled':'cached','combined':'balanced'}[route],
                geometry_p95_ms=p95(geometry),edit_ms=[r['request_to_waited_gpu_completion_ms'] for r in rows if r['label']=='edit']))
    pub_stops=[]
    for scene in ('guardian','robot'):
        cached=next(r for r in pubs if r['scene']==scene and r['mode']=='cached')['geometry_p95_ms']
        balanced=next(r for r in pubs if r['scene']==scene and r['mode']=='balanced')['geometry_p95_ms']
        pub_stops.append(dict(scene=scene,increase_ms=balanced-cached,increase_fraction=balanced/cached-1,
            stop=balanced>cached*1.25 and balanced>cached+10))
    assert not any(r['stop'] for r in pub_stops),'Publication stop rule requires investigation before promotion'
    assert read(ev/'contracts.json')['runs'][0]['source_geometry_score']['passed']
    transitions=read(ev/'transitions.json');assert transitions['complete'] and len(transitions['directed_edges'])==20
    assert read(ev/'window.json')['passed']
    baseline=read(ev/'baseline.json')
    preserved={p:h for p,h in baseline['spv_sha256'].items() if sha(p)==h}
    assert len(preserved)==192
    changed=[p for p,h in baseline['source_sha256'].items() if sha(p)!=h]
    assert not any(p.startswith(('include/','src/','assets/')) for p in changed)
    for name in ('sdf_field.cpp','sdf_regions.cpp','sdf_authored.cpp','sdf_history.cpp','sdf_material.cpp','sdf_recipe.cpp'):
        assert 'projects/sdf/'+name not in changed
    identities=dict(binary_sha256=sha('build/release/projects/sdf/sdf'),source_head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        source_sha256={p:sha(p) for p in subprocess.check_output(['git','ls-files','projects/sdf','include','src'],text=True).splitlines()},
        spv_sha256={str(p):sha(p) for p in sorted(Path('build/release/projects/sdf/shaders').glob('*.spv'))},
        preserved_original_spvs=192,changed_original_sources=changed)
    save(ev/'identities.json',identities)
    summary=dict(date='2026-10-29',default='Balanced for authored scenes/workshops; Cached for box recipes',
        supersample_defaults_changed=False,binary_sha256=identities['binary_sha256'],timings=timings,
        publication=pubs,publication_stops=pub_stops,public_analytic_backend='A64 root-far + automatic shadow specialization in atmosphere; ordinary A16 in material/optical lighting',
        sampling_runs=96,final_timing_runs=108,aliases=20,directed_transitions=20,failure_stages=5,
        history_frames=45,feature_captures=40,native_motion_frames=96,original_spvs_preserved=192,
        budgets=dict(balanced_ss1_all_six_views_below_16_7=all(r['high']<16.7 for r in timings if r['mode']=='balanced' and r['ss']==1),
            balanced_ss2_close_below_20=all(r['high']<20 for r in timings if r['mode']=='balanced' and r['ss']==2 and r['view']=='close')),
        limits=['Cached primary visibility and shadows still lose source detail; the earlier detail-preserving profile remains failed.',
            'SS2 close views exceed 20 ms GPU p95; sample settings are independent and preserved.',
            'Geometry edits rebuild synchronously; source-to-cache selection can incur that full rebuild.',
            'Native Xvfb acceptance is separate from physical desktop and human visual acceptance.',
            'Feature RGB diagnostic-color screening is not complete internal floating-point instrumentation.',
            'Prepared-cone/adaptive studies and accelerated hybrid remain under their original private admission.'])
    summary['switches']=[dict(scene=r['scene'],mode=row['render_mode'],switch_ms=row['mode_switch_ms'],
        field_builds=row['field_builds'],region_builds=row['region_builds'])
        for r in read(ev/'large-transitions.json')['runs'] for row in r['actions'][1:]]
    summary['material_setup_observations']=dict(first_observed_pipeline_ms=11449.9,repeated_pipeline_ms=8.30474,
        scope='Same material-workshop/window configuration; observed first and repeated process setup, not a deliberately cleared cold driver cache')
    save(ev/'summary.json',summary)
    media(root,report)
    shutil.copytree(ev,report/'evidence')
    template=Path(__file__).with_name('render_modes_report.html').read_text()
    (report/'index.html').write_text(template.replace('__STUDY__',json.dumps(summary).replace('</','<\\/')))
    print(json.dumps(dict(report=str(report),publication_stops=pub_stops,budgets=summary['budgets']),indent=2))


def media(root,report):
    images=report/'images';images.mkdir();clips=report/'clips';clips.mkdir()
    for row in read(root/'captures/captures.json')['runs']:
        path=Path(row['image']);assert sha(path)==row['image_sha256'];shutil.copy2(path,images/path.name)
    for path in (root/'visual-review').glob('*.png'):shutil.copy2(path,images/('sampling-'+path.name))
    for name in ('initial','mode-menu','samples-1','samples-2','resized-restored'):
        path=root/'window-final'/(name+'.png');shutil.copy2(path,images/('window-'+name+'.png'))
    # CPU encoding runs only after all native timings and captures are complete.
    for scene in ('guardian','robot'):
        inputs=[]
        for mode,ss in [('cached',2),('balanced',1),('balanced',2)]:
            inputs+=['-framerate','12','-i',str(root/'motion'/f'{scene}-{mode}-ss{ss}.frame-%d.png')]
        graph=';'.join(f'[{i}:v]scale=640:360,setsar=1[v{i}]' for i in range(3))
        graph+=';[v0][v1][v2]hstack=inputs=3[out]'
        subprocess.run(['ffmpeg','-nostdin','-hide_banner','-loglevel','error',*inputs,'-filter_complex',graph,
            '-map','[out]','-frames:v','16','-c:v','libx264','-threads','4','-crf','17','-pix_fmt','yuv420p',
            '-movflags','+faststart',str(clips/(scene+'.mp4'))],check=True)


if __name__=='__main__':main()
