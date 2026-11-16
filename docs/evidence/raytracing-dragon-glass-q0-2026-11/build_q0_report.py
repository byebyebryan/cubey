import json, shutil
from pathlib import Path
root=Path('outputs/raytracing/dragon-glass-q0-20261108')
out=root/'report'
out.mkdir(exist_ok=True)
images=out/'images'
images.mkdir(exist_ok=True)
for p in (root/'pilot').glob('*.png'):
    if '-mask-' not in p.name and '-primary-labels' not in p.name:
        shutil.copyfile(p,images/p.name)
for p in (root/'fullhd').glob('*.png'):
    if '-512-' in p.name or '-2048-' in p.name:
        shutil.copyfile(p,images/('fullhd-'+p.name))
for p in (root/'extended').glob('*.png'):
    shutil.copyfile(p,images/p.name)
for p in (root/'ablations').glob('*.png'):
    if '-128-' in p.name:
        shutil.copyfile(p,images/p.name)
analysis=json.loads((root/'analysis.json').read_text())
diagnosis=json.loads((root/'failure-diagnostics/diagnosis.json').read_text())
for name in ['analysis.json','failure-diagnostics/diagnosis.json','validation.json']:
    shutil.copyfile(root/name,out/Path(name).name)
data=json.dumps(dict(analysis=analysis,diagnosis=diagnosis)).replace('<','\\u003c')
html=Path('outputs/raytracing/dragon-glass-q0-20261108/report-template.html').read_text()
(out/'index.html').write_text(html.replace('__DATA__',data))
print(out)
