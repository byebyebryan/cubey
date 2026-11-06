"""Assemble only qualified native material-workshop evidence for internal review."""
import argparse
import json
from pathlib import Path
import shutil
from profile_first_pass import sha


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--evidence',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();root=a.evidence.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    assets=out/'assets';assets.mkdir();data=out/'data';data.mkdir()
    def read(path):return json.loads(path.read_text())
    def copy(path,destination):shutil.copy2(path,destination)
    receipts=[]
    for name in ['p0','p1','p2','p3-math','p3','p4']:
        path=Path('docs/evidence/raytracing-material-workshop-2026-11')/(name+'.json')
        copy(path,data/path.name);receipts.append(name)
    final=read(data/'p4.json');assert final['passed']
    identity=read(root/'timing/identity.json');assert identity['status']==''
    for stage in ['gallery-quality','gallery-cost','stills','films','profiles','retention']:
        value=read(root/stage/'receipt.json');assert value['passed']
        copy(root/stage/'receipt.json',data/(stage+'.json'));receipts.append(stage)
    entries=read(Path('assets/materials/catalog.json'))['entries']
    ids=[e['id'] for e in entries]
    for renderer in ['forward','sdf','rt']:
        for suffix in [*ids,'gallery']:
            copy(root/'stills'/f'{renderer}-{suffix}.png',assets/f'{renderer}-{suffix}.png')
        for motion in ['orbit','dawn','dusk']:
            for suffix in ['.mp4','-poster.png']:
                copy(root/'films'/f'{renderer}-{motion}{suffix}',assets/f'{renderer}-{motion}{suffix}')
    copy(root/'stills/rt-gallery-scene.png',assets/'rt-gallery-scene.png')
    copy(Path('outputs/raytracing/material-workshop/p3-ff979923/native-frost/resized.png'),assets/'native-workshop.png')
    copy(Path('outputs/raytracing/material-workshop/reference/capture/materials.png'),assets/'donor-materials.png')
    copy(Path('projects/raytracing/MATERIAL_WORKSHOP.md'),out/'operation.md')
    manifest=read(Path('projects/raytracing/presets/material-workshop/manifest.json'))
    copy(Path('projects/raytracing/presets/material-workshop/manifest.json'),data/'manifest.json')
    quality=read(root/'gallery-quality/quality.json')
    worst=[]
    for material in ids:
        for route in ['environment','scene']:
            rows=[r for r in quality if r['material']==material and r['route']==route and r['samples']==256]
            assert len(rows)==2 and all(r['passed'] for r in rows)
            worst.append(dict(material=material,route=route,mean=max(r['mean'] for r in rows),p95=max(r['p95'] for r in rows)))
    timing=read(root/'timing/timing.json');assert len(timing)==78 and all(r['passed'] for r in timing)
    timings=[]
    for material in [*ids,'gallery']:
        row=dict(material=material)
        for route in ['environment','scene']:
            rows=[r for r in timing if r['material']==material and r['source']==route];assert len(rows)==3
            row[route]=max(r['gpu']['raytracing.total_frame']['p95_ms'] for r in rows)
        timings.append(row)
    profiles=read(root/'profiles/receipt.json')['results']
    costs=[]
    for renderer,label,span in [('forward','Forward PBR','material workshop whole scene'),('sdf','SDF tracing','sdf.whole_scene')]:
        costs.append(dict(label=label,value=max(r['gpu'][span]['p95_ms'] for r in profiles if r['renderer']==renderer),scope='Worst repetition p95 · one completed frame'))
    costs.append(dict(label='Hardware RT',value=next(r['environment'] for r in timings if r['material']=='gallery'),scope='Worst repetition p95 · one primary/history iteration'))
    settled=read(root/'gallery-cost/cost.json')
    env=next(r for r in settled if r['name']=='environment-settled-256');scene=next(r for r in settled if r['name']=='scene-settled-256')
    text=(f"RT gallery at 256 primary samples: {env['gpu_ms']['raytracing.total_frame']/1000:.3f} s environment / "
          f"{scene['gpu_ms']['raytracing.total_frame']/1000:.3f} s scene project GPU work. "
          f"Separate completed-app wall times: {env['total_app_wall_ms']/1000:.2f} / {scene['total_app_wall_ms']/1000:.2f} s, "
          "including preparation, launch and readback. The fixed acceptance screen is 2 s of GPU work.")
    gates=[dict(title='P0 · Reference and contracts',text='Pinned native donor; independent GGX arithmetic, furnace and sun/sky accounting.',receipt='p0'),
        dict(title='P1/P2 · Opaque library',text='Ten recipe families, shared procedural domains, relief, rough diffuse and sheen; native operations and minification controls.',receipt='p2'),
        dict(title='P3 · Both dielectrics',text='Independent rough-interface integrals, unequal-IOR two-body Snell/Beer, energy bounds, exhaustion and 1,000-edit lifetime controls.',receipt='p3'),
        dict(title='P4 · Gallery convergence',text='All twelve materials, both routes, two fixed 256-SPP seeds against independent 4096-SPP HDR and 8192-SPP stability controls.',receipt='gallery-quality'),
        dict(title='P4 · Matched media',text='Three-renderer stills and nine 1080p native films. All 240 RT output states complete their requested 256 primary iterations.',receipt='films'),
        dict(title='P4 · Integration and retention',text='Final dev suite, independent legacy controls and byte-identical retained forward/SDF captures; original SDF/horse files preserved.',receipt='p4')]
    payload=dict(materials=[dict(id=e['id'],label=e.get('label',e['id'])) for e in entries],quality=worst,costs=costs,timings=timings,
        settled=text,gates=gates,receipts=receipts,
        identity=dict(runtime_source=identity['source'],binary_sha256=identity['binary_sha256'],build=identity['build'],gpu=identity['gpu'],
            environment=manifest['environment'],geometry=manifest['geometry'],sample_budget=manifest['transport']['limits'],
            memory_scope='Requested project resources and process-local VK_EXT_memory_budget shutdown estimate are separate; neither establishes physical peak usage.'))
    template=Path(__file__).with_name('material_workshop_report.html').read_text()
    (out/'index.html').write_text(template.replace('__DATA__',json.dumps(payload).replace('</','<\\/')))
    artifacts={str(f.relative_to(out)):sha(f) for f in out.rglob('*') if f.is_file()}
    (out/'artifact-manifest.json').write_text(json.dumps(dict(source=identity['source'],artifacts=artifacts),indent=2)+'\n')
    print(out)


if __name__=='__main__':main()
