#!/usr/bin/env python3
"""Publish-ready paired sphere/guardian material gallery with scoped receipts."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();root=Path(__file__).resolve().parents[3];base=root/'outputs/sdf'
    read=lambda p:json.loads(p.read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    comparison=base/'material-study-comparison';foundation=base/'material-study-final-foundation';profile=base/'material-study-profile';legacy=base/'material-study-final-legacy'
    c,f,pr,l=[read(d/'manifest.json') for d in [comparison,foundation,profile,legacy]]
    assert c['passed'] and f['passed'] and l['passed']
    for r in [f,pr,l]:assert r['binary_sha256']==c['binary_sha256']
    assert c['shader_sha256']==f['shader_sha256']==pr['shader_sha256']
    assert sha(root/'build/release/projects/sdf/sdf')==c['binary_sha256']
    for row in c['cases'].values():
        for run in row['runs'].values():assert sha(comparison/run['capture'])==run['sha256']
        if 'orbit' in row:
            assert sha(comparison/row['orbit']['capture'])==row['orbit']['sha256']
    for row in c['glass_diagnostics'].values():assert sha(comparison/row['capture'])==row['sha256']
    for row in f['runs'].values():assert sha(foundation/row['capture'])==row['sha256']
    controls=[base/'material-study-native-controls-qualified',base/'material-study-glass-controls-qualified']
    for directory in controls:
        receipt=read(directory/'windowed-interaction.json')
        assert receipt['passed'] and receipt['binary_sha256']==c['binary_sha256']
        assert receipt['shader_sha256']==c['shader_sha256']
    records={}

    for kind in c['materials']:
        s=read(comparison/f'sphere-{kind}/source/scene.json');g=read(comparison/f'guardian-{kind}/source/scene.json')
        assert s['materials']==g['materials'];records[kind]=s['materials'][0]
        for subject in ['sphere','guardian']:
            case=f'{subject}-{kind}';assert pr['runs'][case]['scene_sha256']==c['cases'][case]['scene_sha256']
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    for source,name in [(comparison,'comparison'),(foundation,'foundation'),(profile,'profile'),(legacy,'legacy')]:shutil.copytree(source,out/name)
    for source,name in zip(controls,['controls-ceramic','controls-glass']):shutil.copytree(source,out/name)
    shutil.copytree(base/'material-study-final-cut-history',out/'cut-history')
    proof=out/'source';proof.mkdir()
    for file in ['sdf_authored.h','sdf_authored.cpp','sdf_authored_tests.cpp','sdf_material.h','sdf_material.cpp','sdf_material_tests.cpp','sdf_renderer.h','sdf_renderer.cpp','sdf_app.cpp','sdf_config.h','sdf_validation.cpp']:
        shutil.copy2(root/'projects/sdf'/file,proof/file)
    for file in ['lighting.glsl','optics_math.glsl','material_study.frag','authored_material.glsl','authored_validate.comp','sdf_surface.glsl']:
        shutil.copy2(root/'projects/sdf/shaders'/file,proof/file)
    for file in ['author_material_study.py','qualify_material_study.py','capture_material_study.py','material_study_report.py','material_study_report_template.html','check_authored_window.py']:
        shutil.copy2(root/'projects/sdf/tools'/file,proof/file)
    for file in ['sdf-material-study-plan.md','sdf-material-study-evidence.md','sdf-guardian-closeout.md']:
        shutil.copy2(root/'docs/notes'/file,proof/file)
    labels=dict(plain='Plain reference',limestone='Limestone',marble='Subtle marble',clay='Dry clay',ceramic='Glazed ceramic',bronze='Satin bronze',glass='Tinted glass')
    explanations=dict(plain='The retained neutral surface reference.',limestone='Accepted rough stone, unchanged material parameters.',marble='Accepted Balanced detail and subtle 0.08 cream variation.',clay='Uniform terracotta with broad, matte highlights. Roughness 0.90.',ceramic='A simple teal dielectric glaze. Roughness 0.22; no crackle or layered coating.',bronze='Colored conductor reflection with no diffuse body lobe. Metallic 1; roughness 0.32.',glass='Measured thickness controls absorption. Four crossings maximum; environment-only reflections and transmission.')
    data=dict(materials=c['materials'],labels=labels,explanations=explanations,records=records,
        cases=c['cases'],glass_verdict=c['glass_verdict'],glass_diagnostics=c['glass_diagnostics'],
        performance=pr['runs'],profile_settings=pr['settings'],device=c['device'],
        foundation_runs=len(f['runs']),source_commit=c['source_commit'],binary_sha256=c['binary_sha256'],
        browser_review='Unavailable: no browser connected',artistic_acceptance='Native trial inspected; user preference remains separate')
    (out/'review.json').write_text(json.dumps(data,indent=2)+'\n')
    template=Path(__file__).with_name('material_study_report_template.html').read_text()
    (out/'index.html').write_text(template.replace('__DATA__',json.dumps(data).replace('</','<\\/')))
    assets={str(p.relative_to(out)):dict(bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()}
    (out/'assets.json').write_text(json.dumps(assets,indent=2)+'\n')
    size=sum(p.stat().st_size for p in out.rglob('*') if p.is_file());assert size<100*1024*1024,size
    print(json.dumps(dict(output=str(out),files=len(assets)+1,bytes=size)))


if __name__=='__main__':main()
