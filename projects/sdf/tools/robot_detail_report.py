#!/usr/bin/env python3
"""Present the robot's mechanical detail pass with matched native comparisons."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import re
import shutil


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,required=True)
    parser.add_argument('--controls',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--sampling-audition',type=Path,help='Optional controlled sampler comparison receipt')
    parser.add_argument('--sampler-regression',type=Path,help='Optional existing-renderer regression output')
    parser.add_argument('--fieldkit',action='store_true',help='Present the field-kit variant against the preceding detail model')
    parser.add_argument('--joints',action='store_true',help='Present the joint alignment correction against the previous field kit')
    parser.add_argument('--joint-audit',type=Path)
    parser.add_argument('--previous-joint-audit',type=Path)
    args=parser.parse_args()
    args.fieldkit=args.fieldkit or args.joints
    source,out=args.input.resolve(),args.output.resolve()
    data=json.loads((source/'manifest.json').read_text())
    history=json.loads((source/'history/manifest.json').read_text())
    controls=json.loads((args.controls/'windowed-interaction.json').read_text())
    count=len(history['history'])
    baseline_count=data.get('scene_counts',{}).get('baseline',43)
    baseline_label='Detail model' if args.fieldkit else 'Original'
    detail_label='Field kit' if args.fieldkit else 'Detail pass'
    title='Survey robot field kit' if args.fieldkit else 'Survey robot mechanical detail study'
    heading='An assembled field robot.' if args.fieldkit else 'A more mechanical survey robot.'
    lead=('A rolled optic rim, tapered limb housings, joint seals and removable power pods turn the toy into a more serviceable little machine. The cleaned lens, pose and four plain materials carry over from the preceding detail model.' if args.fieldkit else
          'Open grippers, a recessed status display, slotted joint fasteners and cut-in vents give the toy more purpose. The rounded silhouette, pose and four plain materials carry over from the accepted original.')
    features=('The optic gains a tapered barrel, rubber seal and rounded front lip. Tapered forearms and shins meet separate wrist and ankle seals; elbow caps sit inside ivory retaining rings. A rounded abdominal recess holds a service door and latch, and two tapered power pods attach beside the vented backpack.' if args.fieldkit else
              'The capsule hands are cut into two-finger grippers. The chest gains a recessed backing plate and three raised amber gauge bars. Teal fasteners sit in counterbores on the head, slotted caps explain the shoulder joints, and shallow seams divide the toe bumpers. Three cooling vents finish the backpack.')
    static_config=data.get('configs',{}).get('detail','projects/sdf/presets/survey-robot-detail.json')
    build_config=data.get('build_config','projects/sdf/presets/survey-robot-detail-build.json')
    author='author_robot_fieldkit.py' if args.fieldkit else 'author_robot.py --detailed'
    previous_url=('http://starship:8787/reports/0a6102e62987f7754d93f4baf74fd2a4cdefe6d29d9a2ff7994d8c3c7c2b14c9/' if args.fieldkit else
                  'http://starship:8787/reports/5d6bfde3c2493278b5c68e1ddce68381942ff9d8ef60672a553aa4b6a52f0c7f/')
    shape_limit=data.get('authored_limits',{}).get('leaves',128 if args.fieldkit else 64)
    orbit=history['duration']-history['construction_seconds']
    comparison_note=f'This version uses {count-baseline_count} more primitive leaves than the comparison model, reaching {count} of the study’s {shape_limit}-shape limit. The preceding scene and its generator output remain intact. The four plain materials carry across both models.'
    if args.joints:
        assert args.joint_audit and args.previous_joint_audit
        baseline_label,detail_label='Previous joints','Revised joints'
        title,heading='Survey robot joint alignment','Joints that follow the pose.'
        lead='The elbow hardware now follows each arm’s bend plane. Shared pivots align the upper legs, tapered shins and ankle seals, with feet centered beneath them. Compare the front and profile close-ups as well as the full model.'
        features='Elbow caps and complete retaining rings share a hinge axis perpendicular to both arm links. Shoulder covers follow the upper-arm swing plane and seat into the shoulder bearings. Each leg uses one hip-to-ankle centerline; its shin, ankle collar, foot and sole follow that placement. The tilted head and upper neck collar share the neck pivot.'
        previous_url='http://starship:8787/reports/52b40ce5c726673b029f0f22ec2dd21d50d9a297e938beb2c161fe8deec734de/'
        comparison_note=f'Both versions use {count} shapes, the same four materials and the same rendering settings. The generator now places the revised joints; the earlier field-kit capture remains available in the linked previous study.'
    assert data['replay_scene_bytes_identical'] and history['final_matches_static_fingerprints']
    assert history['video_builds']==count+1 and json.loads((source/'history/video-check.json').read_text())['passed']
    assert controls['passed'] and controls['binary_sha256']==data['binary_sha256']==history['binary_sha256']
    assert controls['shader_sha256']==data['shader_sha256']==history['shader_sha256']
    assert all(row['passed'] for row in data['atlas_relocation_checks'].values())
    if out.exists():raise RuntimeError('Use a fresh report export directory')
    shutil.copytree(source,out/'data')
    shutil.copytree(args.controls,out/'controls')
    sampler_links=''
    if args.sampling_audition:
        shutil.copy2(args.sampling_audition,out/'data/sampling-audition.json')
        sampler_links+='<a href="data/sampling-audition.json">Controlled sampler comparison</a>'
    if args.sampler_regression:
        regression=json.loads((args.sampler_regression/'manifest.json').read_text())
        assert regression['binary_sha256']==data['binary_sha256'] and regression['shader_sha256']==data['shader_sha256']
        shutil.copytree(args.sampler_regression,out/'data/sampler-regression')
        sampler_links+=' · <a href="data/sampler-regression/manifest.json">Primitive, material and legacy regressions</a>'
    if args.joints:
        audit=json.loads(args.joint_audit.read_text())
        previous_audit=json.loads(args.previous_joint_audit.read_text())
        assert audit['passed'] and not previous_audit['passed']
        assert audit['scene_sha256']==data['source_scene_sha256']['detail']
        assert previous_audit['scene_sha256']==data['source_scene_sha256']['baseline']
        shutil.copy2(args.joint_audit,out/'data/joint-audit.json')
        shutil.copy2(args.previous_joint_audit,out/'data/previous-joint-audit.json')
        repo=Path(__file__).resolve().parents[3]
        for name,receipt in (('previous',previous_audit),('current',audit)):
            scene_path=Path(receipt['scene'])
            if not scene_path.is_absolute(): scene_path=repo/scene_path
            assert hashlib.sha256(scene_path.read_bytes()).hexdigest()==receipt['scene_sha256']
            shutil.copy2(scene_path,out/f'data/scenes/{name}-fieldkit.json')
        shutil.copy2(Path(__file__).with_name('check_robot_joints.py'),out/'data/scenes/check_robot_joints.py')
        passed=sum(check['passed'] for check in previous_audit['checks'])
        technical_details=f'''<details open><summary>Checking modeling intent as well as SDF correctness</summary><p>The earlier model passed its renderer checks but had geometry mistakes: fixed forward-facing elbow axes, rings intersecting the bent forearms, independently placed leg segments and laterally offset feet. This pass derives their placements from shared joint pivots.</p><p>An independent audit reads the saved scene, composes its transforms and checks link endpoints, hinge-axis perpendicularity, bearing seating, collar axes, leg centerlines, centered feet and the head’s neck pivot. The preceding model passes {passed}/{len(previous_audit['checks'])} checks; the revision passes all {len(audit['checks'])}. Native front, side and opposite three-quarter inspections then check whether the assembled geometry reads clearly.</p><p><a href="data/joint-audit.json">Current joint audit</a> · <a href="data/previous-joint-audit.json">Previous model audit</a> · <a href="data/scenes/check_robot_joints.py">Audit script</a> · <a href="data/scenes/previous-fieldkit.json">Previous explicit scene</a> · <a href="data/scenes/current-fieldkit.json">Revised explicit scene</a>. Both models retain {count} primitive leaves and the same palette. These are static modeling constraints; the build history reveals the saved parts.</p></details>'''
        foundation_note='This pass corrects authored joint placements and orientations on the existing renderer; binary and shaders match the preceding field-kit study.'
    elif args.fieldkit:
        technical_details=f'''<details open><summary>What the extra vocabulary buys us</summary><p><strong>Torus:</strong> optic lips, seals and retaining rings in one primitive. <strong>Capped cone:</strong> tapered optics, forearms, shins and power pods without a stack of cylinders. <strong>Smooth subtraction:</strong> a softened display recess and service-door opening, scoped to their own bodies.</p><p>The torus uses distance to a revolved circle. The capped cone revolves the minimum distance to two cap segments and a side segment. Smooth subtraction uses a polynomial smooth maximum of the body distance and negated cutter distance. These retain the conservative distance bound needed by the existing brick builder. Cut materials stay explicit; the retained body owns ties.</p><p>The program budget is 128 shapes / 256 instructions, with the existing tree and stack depth limits. The field program buffer grows by 8 KiB. Atlas dimensions, palette and resolution carry over from the preceding model. Native fixtures cover pointed and equal-ended cones, transformed rings, cut-material inheritance and the 128-shape boundary. {sampler_links}</p></details>'''
        foundation_note='This pass adds the torus, capped cone and smooth-subtraction contracts to the existing authored geometry/material foundation.'
    else:
        technical_details=f'''<details><summary>A small detail exposed an atlas precision issue</summary><p>The initial study's hardware-filtered replay exceeded the existing image threshold at three pixels; the largest difference was 39 code values near the antenna's hard-shadow edge. Geometry fingerprints and native ray checks still agreed. Brick-local trilinear interpolation removed dependence on normalized atlas coordinates. The recorded controlled same-scene profile measured 2.634 ms median with hardware filtering and 3.073 ms with local interpolation. Replay became identical, and the original image threshold was retained. The new native captures use the corrected sampler.</p><p>{sampler_links}</p></details>'''
        foundation_note='This pass uses the existing geometry/material foundation with the brick-sampling precision fix.'
    template=Path(__file__).with_name('negative_report_template.html').read_text()
    css=template.split('<style>')[1].split('</style>')[0]
    video_js=template.split('<script>')[1].split('const stages=')[0]
    views=data['views']
    if args.fieldkit and not args.joints:
        views={key:dict(value) for key,value in views.items()}
        for key,label in (('face','Layered optic'),('upper','Service door and joints'),('backpack','Power pods and vents')):
            views[key]['label']=label
    if args.joints:
        order=['upper','arms-left','legs-front','legs-side','hero','front','side','rear','face','feet','backpack']
        views={key:views[key] for key in order}
    initial='upper' if args.joints else 'hero'
    initial_label=html.escape(views[initial]['label'])
    view_buttons=''.join(f'<button data-view="{key}" aria-pressed="{str(key==initial).lower()}">{html.escape(value["label"])}</button>' for key,value in views.items())
    def preview(node):return next(p['time'] for p in history['previews'] if p['node']==node)*(history['frames']-1)/history['frames']
    chapters=[(0,'Start'),(preview('head-left-fastener-cut'),'Head fastener'),
              (preview('chest-display-cut'),'Display recess'),(preview('right-gripper-slot'),'Open the gripper'),
              (history['construction_seconds'],'Finished orbit')]
    if args.joints:
        def committed(node):
            index=next(i+1 for i,step in enumerate(history['history']) if step['node']==node)
            return history['steps'][index]['time']*(history['frames']-1)/history['frames']
        chapters=[(0,'Start'),(committed('right-shoulder-cap'),'Shoulder frame'),
                  (committed('right-wrist-seal'),'Elbow and forearm'),
                  (committed('right-sole'),'Leg and foot'),(history['construction_seconds'],'Finished orbit')]
    chapter_buttons=''.join(f'<button disabled data-time="{time}">{label}</button>' for time,label in chapters)
    step_options=''.join(f'<option value="{s["index"]}" {"selected" if s["index"]==count else ""}>{s["index"]:02} · {html.escape(s["label"])}</option>' for s in history['steps'])
    rows=''
    for key,label in (('baseline',f'{baseline_label} · {baseline_count} shapes'),('detail',f'{detail_label} · {count} shapes')):
        timing=data['runs'][f'profile-{key}']['gpu_frame']
        rows+=f'<tr><td>{label}</td><td>{timing["median_ms"]:.3f} ms</td><td>{timing["p95_ms"]:.3f} ms</td><td>{timing["samples"]}</td></tr>'
    build=next(s for s in data['runs']['detail-hero']['stats'] if s.startswith('sdf: authored instructions='))
    bricks=int(re.search(r'bricks=(\d+)',build)[1])
    allocated=int(re.search(r'allocated=(\d+)',build)[1])
    hidden=[p['label'] for p in history['previews'] if not p['visible_from_capture_camera']]
    hidden_text=', '.join(hidden) if hidden else 'None'
    page=f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cubey SDF — {title}</title><link rel="icon" href="data:,"><style>{css}
button:disabled{{opacity:.45;cursor:default}}video{{height:auto;aspect-ratio:4/3;object-fit:contain}}.stage,.hero-video{{max-width:1000px;margin-inline:auto}}
.stage{{--split:50%;position:relative;overflow:hidden;border-radius:12px;background:#17262d}}.stage img{{display:block;width:100%;height:auto}}.overlay{{position:absolute;inset:0;clip-path:inset(0 0 0 var(--split))}}.overlay img{{height:100%;object-fit:fill}}
.stage[data-mode=baseline] .overlay{{display:none}}.stage[data-mode=detail] .overlay{{clip-path:none}}.split-line{{position:absolute;top:0;bottom:0;left:var(--split);border-left:2px solid white;pointer-events:none}}
.stage:not([data-mode=compare]) .split-line{{display:none}}.image-label{{position:absolute;top:12px;padding:5px 9px;background:#0e171dbb;border-radius:5px;font-size:.85rem}}.old-label{{left:12px}}.new-label{{right:12px}}.stage[data-mode=detail] .old-label,.stage[data-mode=baseline] .new-label{{display:none}}
.range-row{{display:flex;align-items:center;gap:14px;margin:16px 0}}.range-row input{{min-width:0;width:100%}}.range-row output{{white-space:nowrap;color:#e4b66b}}select{{width:100%;max-width:660px}}[hidden]{{display:none!important}}
 </style></head><body><main><div class="eyebrow">Cubey · Authored SDF · {title}</div>
<h1>{heading}</h1>
<p class="lead">{lead}</p>
<div class="controls" aria-label="Comparison treatment"><button data-mode="baseline" aria-pressed="false">{baseline_label}</button><button data-mode="detail" aria-pressed="true">{detail_label}</button><button data-mode="compare" aria-pressed="false">Compare</button></div>
<div class="controls" aria-label="Native comparison view">{view_buttons}</div>
<div id="comparison" class="stage" data-mode="detail" data-view="{initial}"><img id="baseline-image" src="data/captures/baseline-{initial}.png" alt="{baseline_label}, {initial_label}"><div class="overlay"><img id="detail-image" src="data/captures/detail-{initial}.png" alt="{detail_label}, {initial_label}"></div><span class="image-label old-label">{baseline_label}</span><span class="image-label new-label">{detail_label}</span><div class="split-line"></div></div>
<div id="split-controls" class="range-row" hidden><label for="split">Before / after</label><input id="split" type="range" min="0" max="100" value="50" aria-label="Before and after comparison split"></div>
<p class="note" id="view-caption" aria-live="polite">{initial_label} · identical camera and lighting for both versions.</p>
<p class="note"><a id="baseline-link" href="data/captures/baseline-{initial}.png">{baseline_label} PNG</a> · <a id="detail-link" href="data/captures/detail-{initial}.png">{detail_label} PNG</a>. Rear and backpack inspections use a rear-facing sun; each pair shares the same settings.</p>
<h2>{'Connections that meet' if args.joints else 'Details with a job to do'}</h2>
<p>{features}</p>
<p>{comparison_note}</p>
<h2>Watch the detailed robot take shape</h2>
<div class="hero-video"><video controls preload="metadata" width="1280" height="960" data-video-src="data/history/presentation.mp4" poster="data/captures/detail-hero.png" aria-label="Native detailed robot construction and final orbit"></video></div>
<p class="note" id="video-caption" aria-live="polite">Build the detailed robot from an empty workspace.</p>
<div class="chapters">{chapter_buttons}</div><p class="note" id="video-status" role="status">Preparing the video…</p>
<p class="note">{history['duration']} seconds · {history['construction_seconds']} seconds of construction, then a {orbit}-second orbit · 1280×960 · 30 FPS · 2× internal sampling · hard self-shadows and shared atmosphere. Labels were added after native capture. <a href="data/history/presentation.mp4">Labeled video</a> · <a href="data/history/native.mp4">Original native capture</a>.</p>
<details id="steps-panel"><summary>Inspect any of the {count} committed edits</summary>
<p>Choose a named edit, drag the slider or step backward and forward. The translucent cutter preview in playback precedes the actual subtraction; these images show committed states.</p>
<div class="range-row"><output id="step-count" for="step">Edit {count} / {count}</output><input id="step" type="range" min="0" max="{count}" value="{count}" step="1" aria-label="Committed edit"></div>
<div class="controls"><button id="previous">Previous edit</button><button id="next" disabled>Next edit</button><button id="watch-step" disabled data-time="0">Watch this edit</button></div>
<select id="step-select" aria-label="Named committed edit">{step_options}</select><p id="step-label" aria-live="polite"></p>
<div class="viewer"><a id="step-link" href="data/history/captures/step-{count:02}.png"><img loading="lazy" id="step-image" src="data/history/captures/step-{count:02}.png" alt="Finished detailed robot"></a></div></details>
<h2>Native checks and cost</h2><div class="facts"><div><strong>{count+1} scene states</strong><small>Empty workspace through the completed model</small></div><div><strong>{len(history['previews'])} local cuts</strong><small>Inherited transforms and per-part materials</small></div><div><strong>{bricks:,} bricks</strong><small>262,144-brick axis-64 capacity</small></div></div>
<p>Every intermediate state passes required Vulkan validation, the sampled-field ray oracle and direct CPU/GPU expression, material and part comparisons. Backward/reset revisits and unchanged-state cache reuse pass. The final history view matches the static detail scene; the explicit exported scene reloads byte for byte. The desktop check passes playback, previous/next, scrub-to-final/midpoint, reset, resize and camera input. Moving identical brick data between the axis-64, 80 and 92 atlas layouts produces identical images.</p>
<p>All {history['frames']:,} native video frames were decoded and screened for visible magenta traversal exhaustion; none was found. Sampling ambiguities retain the existing finite-grid tolerance. Previews expected to be occluded from the fixed construction camera are declared individually and recorded separately; rear details can be inspected above and in orbit.</p>
<p>Matched 1920×1080, 1×, hard-shadow static profiles on the RTX 5070 Ti submit 180 frames, discard 30 warmup frames and report completed GPU trace-plus-post timestamps. Field construction, readback and encoding are excluded.</p>
<div class="table-wrap"><table><thead><tr><th>Treatment</th><th>Median GPU frame</th><th>p95</th><th>Samples</th></tr></thead><tbody>{rows}</tbody></table></div>
<p class="note">Geometry changes still rebuild synchronously. The axis-64 field allocates {allocated:,} bytes, excluding frame targets and driver resources.</p>
{technical_details}
<details><summary>Run it and inspect the evidence</summary><pre>build/dev/projects/sdf/sdf --config {static_config}
build/dev/projects/sdf/sdf --config {build_config}

python3 projects/sdf/tools/{author}</pre>
<p>Content/capture source <code>{data['source_commit']}</code> · worktree {'dirty' if data['source_dirty'] else 'clean'} at qualification start. Native binary/shader hashes, the source scenes and authoring scripts are recorded. {foundation_note}</p>
<p><a href="data/manifest.json">Matched views and profiles</a> · <a href="data/history/manifest.json">Complete history qualification</a> · <a href="data/history/logs/history-validation.log">Every-state native comparisons</a> · <a href="data/history/scenes/robot.json">Explicit detailed scene/history</a> · <a href="data/scenes/{'author_robot_fieldkit.py' if args.fieldkit else 'author_robot.py'}">Authoring script</a> · <a href="controls/windowed-interaction.json">Desktop receipt</a> · <a href="data/history/video-check.json">Video screening</a></p>
<p class="note">Previews occluded from the construction camera: {html.escape(hidden_text)}.</p></details>
<footer><a href="{previous_url}">Previous robot study</a> · <a href="http://starship:8787/reports/#project=cubey">Cubey report catalog</a></footer>
</main><script>{video_js}
const viewLabels={json.dumps({k:v['label'] for k,v in views.items()})};const baselineLabel={json.dumps(baseline_label)},detailLabel={json.dumps(detail_label)};const steps={json.dumps(history['steps'])};const events={json.dumps(history['video_events'])};
const last=steps.length-1,seekScale={(history['frames']-1)/history['frames']};let selected=last,viewRequest=0;
const stage=document.querySelector('#comparison');
async function showView(view){{const request=++viewRequest;const a=new Image(),b=new Image();a.src=`data/captures/baseline-${{view}}.png`;b.src=`data/captures/detail-${{view}}.png`;await Promise.all([a.decode(),b.decode()]);if(request!==viewRequest)return;
document.querySelector('#baseline-image').src=a.src;document.querySelector('#detail-image').src=b.src;document.querySelector('#baseline-image').alt=`${{baselineLabel}}: ${{viewLabels[view]}}`;document.querySelector('#detail-image').alt=`${{detailLabel}}: ${{viewLabels[view]}}`;
document.querySelector('#baseline-link').href=a.src;document.querySelector('#detail-link').href=b.src;stage.dataset.view=view;document.querySelector('#view-caption').textContent=`${{viewLabels[view]}} · identical camera and lighting for both versions.`;document.querySelectorAll('[data-view]').forEach(button=>button.setAttribute('aria-pressed',String(button.dataset.view===view)));}}
document.querySelectorAll('button[data-view]').forEach(button=>button.addEventListener('click',()=>showView(button.dataset.view)));
document.querySelectorAll('button[data-mode]').forEach(button=>button.addEventListener('click',()=>{{stage.dataset.mode=button.dataset.mode;document.querySelector('#split-controls').hidden=button.dataset.mode!=='compare';document.querySelectorAll('button[data-mode]').forEach(other=>other.setAttribute('aria-pressed',String(other===button)));}}));
document.querySelector('#split').addEventListener('input',event=>stage.style.setProperty('--split',`${{Number(event.target.value)}}%`));
function showStep(value){{selected=Math.max(0,Math.min(last,Number(value)));const state=steps[selected],path=`data/history/${{state.capture}}`;document.querySelector('#step').value=selected;document.querySelector('#step-select').value=selected;document.querySelector('#step-count').textContent=`Edit ${{String(selected).padStart(2,'0')}} / ${{last}}`;document.querySelector('#step-label').textContent=state.label;document.querySelector('#step-image').src=path;document.querySelector('#step-image').alt=`Edit ${{selected}}: ${{state.label}}`;document.querySelector('#step-link').href=path;document.querySelector('#previous').disabled=selected===0;document.querySelector('#next').disabled=selected===last;document.querySelector('#watch-step').dataset.time=String(state.time*seekScale);}}
document.querySelector('#step').addEventListener('input',event=>showStep(event.target.value));document.querySelector('#step-select').addEventListener('change',event=>showStep(event.target.value));document.querySelector('#previous').addEventListener('click',()=>showStep(selected-1));document.querySelector('#next').addEventListener('click',()=>showStep(selected+1));
function caption(){{const event=events.findLast(event=>event.time<=video.currentTime);if(event)document.querySelector('#video-caption').textContent=event.text;}}video.addEventListener('timeupdate',caption);video.addEventListener('seeked',caption);showStep(last);
</script></body></html>'''
    (out/'index.html').write_text(page)
    print(out)


if __name__=='__main__':main()
