#!/usr/bin/env python3
"""Bundle a dated overview from retained SDF evidence; no native measurement."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[3]
RAW = ROOT / 'outputs/sdf/edit-service-loop-20261101'


def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def build(output):
    output.mkdir(parents=True, exist_ok=False)
    (output/'assets').mkdir(); (output/'evidence').mkdir()
    sources = []
    def copy(source, name):
        source = ROOT/source
        shutil.copy2(source, output/name)
        sources.append(dict(source=str(source.relative_to(ROOT)), asset=name, sha256=digest(source)))
    copy('site/public/media/guardian-limestone.webp', 'assets/guardian.webp')
    for operation in ('add','cut'):
        copy(f'outputs/sdf/edit-service-loop-20261101/final-regression/replay-v2/120-{operation}-0-private.png', f'assets/stream-{operation}.png')
    for source,name in [
        ('docs/evidence/sdf-edit-service-2026-11/summary.json','edit-summary.json'),
        ('docs/evidence/sdf-edit-service-2026-11/source-pins.json','source-pins.json'),
        ('docs/evidence/sdf-edit-service-2026-11/verification.json','verification.json'),
        ('docs/notes/sdf-edit-service-results.md','edit-results.md'),
        ('docs/notes/sdf-interactive-edit-solution-research.md','solution-research.md'),
        ('docs/notes/sdf-render-mode-promotion-results.md','historical-render-results.md'),
        ('docs/evidence/sdf-render-mode-promotion-2026-10/summary.json','historical-render-summary.json'),
        ('projects/sdf/README.md','native-guide.md')]:
        copy(source,'evidence/'+name)
    summary = json.loads((ROOT/'docs/evidence/sdf-edit-service-2026-11/summary.json').read_text())
    modes = json.loads((ROOT/'docs/evidence/sdf-render-mode-promotion-2026-10/summary.json').read_text())
    assert summary['complete'] and summary['finite_streams_qualified']
    preparations=[]
    for mode,label in [('legacy','Original palette round trip'),('palette','Palette-only serialization'),('sealed','Palette + immutable result')]:
        cohort=json.loads((RAW/f'preparation-{mode}/cohort.json').read_text())
        rows=[r['native'] for r in cohort['runs'] if r['subject']=='guardian']
        preparations.append(dict(label=label,worker=statistics.median(r['worker_preparation_ms'] for r in rows),
                                 admission=statistics.median(r['build']['admission_ms'] for r in rows)))
    strict=[r for r in summary['service'] if r['cohort'].startswith('strict-8-')]
    batch_status={s:dict(passes=sum(r['passes'] for r in strict if r['subject']==s),
                         trials=sum(r['trials'] for r in strict if r['subject']==s)) for s in ('guardian','robot')}
    traces=[]
    cohort=json.loads((RAW/'strict-stream-off/cohort.json').read_text())
    for row in cohort['runs']:
        if row['repeat'] != 0:continue
        inp=Path(row['command'][row['command'].index('--edit-input')+1])
        poses=json.loads(inp.read_text())['stream']['poses']
        traces.append(dict(rate=row['rate'],operation='cut' if row['subtract'] else 'add',
                           poses=poses,accepted=row['native']['accepted'],jobs=row['native']['jobs'],
                           frames=row['native']['frames']))
    data=dict(date='2026-11-02',source_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
              batch_status=batch_status,service=summary['service'],preparation=preparations,
              streams=summary['finite_streams'],admission_followup=summary['admission_followup'],
              render_date=modes['date'],render_timings=modes['timings'],traces=traces)
    encoded=json.dumps(data,separators=(',',':'),allow_nan=False)
    template=Path(__file__).with_suffix('.html').read_text()
    assert template.count('__REPORT_DATA__')==1
    (output/'index.html').write_text(template.replace('__REPORT_DATA__',encoded.replace('</','<\\/')))
    (output/'report-data.json').write_text(json.dumps(data,indent=2,allow_nan=False)+'\n')
    (output/'manifest.json').write_text(json.dumps(dict(date=data['date'],source_head=data['source_head'],
        scope='Overview only; retained measurements, no new GPU runs or runtime changes.',
        generator_sha256=digest(Path(__file__)),template_sha256=digest(Path(__file__).with_suffix('.html')),
        inputs=sources),indent=2)+'\n')
    print(json.dumps(dict(export=str(output),files=sum(p.is_file() for p in output.rglob('*')),
                          bytes=sum(p.stat().st_size for p in output.rglob('*') if p.is_file()))))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    build(parser.parse_args().output.resolve())
