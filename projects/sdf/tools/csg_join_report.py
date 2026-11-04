#!/usr/bin/env python3
"""Deliver native before/after evidence for the CSG junction polish."""
import argparse
import json
from pathlib import Path
import shutil

from refine_csg_showcase import ROOT,sha,save

TEXT={
 'robot':dict(title='Field-kit robot',lead='Inspected; geometry retained.',
   fix='The extra rear and overhead views did not expose a join that justified another geometry change. The robot remains byte-identical to the forms-first candidate.',
   more='Its lens, neck, shoulders, elbows, wrists and power-pod collars retain the independently checked assembly relationships.',
   scope='Inspection is useful even when the best result is to keep the existing form.'),
 'canal-mill':dict(title='Canal mill',lead='Let the bridge meet the banks, and the wheel meet the water.',
   fix='Both banks now share a consistent coping height. The bridge enters through openings between the coping ends, with thinner planks and two supporting stringers. The two retained white grain sacks sit in the dry yard; the third is removed.',
   more='Moved the awning posts onto the bank, opened a small wheel race connected to the canal, lowered the paddles into the water, seated the cart wheels on their path and moved the barrels clear of it. A small rest supports the parked cart.',
   scope='The canal and race are static CSG water surfaces. The added bridge supports and race walls explain function; no new decorative pattern is introduced.'),
 'observatory':dict(title='Hill observatory',lead='A continuous route from stair to balcony.',
   fix='The widened landing overlaps the circular balcony at exactly the same height. Its support is widened beneath it. The rail now has an opening at that approach, with a terminal post at the exposed end.',
   more='Checked the last stair rise, the full landing footprint, the survey tripod pad and the architectural section. The section retains the same surviving primitives, plus its one cutter.',
   scope='The previous support checks proved that the stair stayed inside the base; this pass also checks the connection and access at its top.'),
 'courtyard':dict(title='Terraced courtyard',lead='Still water, continuous paving and a real doorway.',
   fix='Removed all four falling-water strands. The basin keeps a still water surface. One broad plaza replaces the undersized paving mat, and the bench and plant pots are seated on that surface.',
   more='Opened the upper doorway through both overlapping building masses and provided interior clearance. Matched the landing to the upper floor, made the stair rises uniform, reconnected the balusters to the rails, and seated the fountain finial on its bowl.',
   scope='The fountain reads as a quiet sculptural centerpiece. The stair and arch rhythm remain; the extra work goes into relationships rather than ornament.'),
}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--study',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();study=args.study.resolve();out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    captures=study/'final-review/captures';q=study/'qualification-final'
    receipt=json.loads((captures/'captures.json').read_text())
    qualification=json.loads((q/'qualification.json').read_text())
    joins=json.loads((study/'joins.json').read_text())
    changes=json.loads((study/'final-changes.json').read_text())
    assert receipt['complete']and qualification['complete']and joins['passed']
    assert receipt['binary_sha256']==qualification['binary_sha256']
    assert receipt['shaders']==qualification['shaders']
    (out/'images').mkdir();(out/'evidence').mkdir();(out/'reproduce').mkdir()
    for row in receipt['runs']:
        image=Path(row['image']);assert sha(image)==row['image_sha256']
        assert sha(row['scene_input'])==row['scene_sha256']
        assert sha(row['config_input'])==row['config_sha256']
        shutil.copy2(image,out/'images'/image.name)
    shutil.copytree(captures/'inputs',out/'evidence/capture-inputs')
    shutil.copytree(study/'before',out/'before')
    shutil.copytree(q,out/'evidence/native-qualification')
    for p in captures.glob('*.log'):shutil.copy2(p,out/'evidence'/p.name)
    for filename in ('inspection.json','joins.json','before-observatory-joins.json','final-changes.json'):
        shutil.copy2(study/filename,out/'evidence'/filename)
    save(out/'evidence/captures.json',receipt)
    for name in ('polish_csg_showcase.py','refine_csg_showcase.py','check_csg_joins.py','qualify_csg_refinement.py',
                 'check_robot_joints.py','sdf_author.py','csg_join_report.py','csg_join_report.html','csg_form_refinement_report.html'):
        shutil.copy2(Path(__file__).parent/name,out/'reproduce'/name)
    data=[]
    for row in changes:
        s=row['subject'];scene=ROOT/row['scene'];preset=ROOT/row['preset']
        valid=next(r for r in qualification['subjects']if r['subject']==s)
        assert sha(scene)==row['scene_sha256']==valid['scene_sha256']
        for kind,path in [('scenes',scene),('presets',preset)]:
            dest=out/'after'/kind/path.name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
        if s=='observatory-section':continue
        data.append(dict(id=s,**TEXT[s],before=row['checks']['before_leaves'],after=row['checks']['leaves']))
    payload=dict(subjects=data,frames=len(receipt['runs']),history_states=sum(len(r['validated_states'])for r in qualification['subjects']),
                 join_checks=sum(len(r['checks'])for r in joins['subjects']),source_head=receipt['source_head'],binary_sha256=receipt['binary_sha256'])
    template=Path(__file__).with_suffix('.html').read_text()
    css=(Path(__file__).parent/'csg_form_refinement_report.html').read_text().split('<style>')[1].split('</style>')[0]
    assert template.count('/*DATA*/')==template.count('/*CSS*/')==1
    (out/'index.html').write_text(template.replace('/*CSS*/',css).replace('/*DATA*/',json.dumps(payload).replace('</','<\\/')))
    save(out/'manifest.json',dict(format='cubey.sdf.join-polish-report',version=1,complete=True,data=payload,
        files=[dict(path=str(p.relative_to(out)),bytes=p.stat().st_size,sha256=sha(p))for p in sorted(out.rglob('*'))if p.is_file()]))
    print(out)


if __name__=='__main__':main()
