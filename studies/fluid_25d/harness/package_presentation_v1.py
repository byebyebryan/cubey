#!/usr/bin/env python3
"""Package already-qualified Fluid media and a small static review export.

No solver, GPU, upload, push, automatic replacement, or large archive vendoring.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

import present_fluid25d_v1 as presentation

ROOT=presentation.ROOT
MEDIA=ROOT/'docs/media/showcase'
AUDITION='http://starship:8787/reports/96e409756ba9954a1d833b89b0296ad89243c9570f119e3ca4105050904fc1c8/'


def check_capture(row,root):
    if presentation.digest(root/row['path'])!=row['sha256']:
        raise ValueError('qualified media changed')
    presentation.check_video(presentation.probe(root/row['path']),row['shot']['frames'],row['shot']['seconds'])
    if row['cleanup']['active_group_remaining']:
        raise ValueError('capture cleanup incomplete')
    if row['unique_hydraulic_states']<10:
        raise ValueError('held water cannot qualify a changing-water film')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hero',type=Path,required=True)
    parser.add_argument('--story',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--package-media',action='store_true',help='create only absent named media under docs/media/showcase')
    args=parser.parse_args()
    hero=args.hero.resolve(strict=True)
    story=args.story.resolve(strict=True)
    row=presentation.read(hero/'captures.json')[0]
    edit=presentation.read(story/'story-edit.json')
    check_capture(row,hero)
    for segment in edit['segments']:
        check_capture(segment,story)
    presentation.check_video(presentation.probe(story/edit['path']),2160,36)
    if presentation.digest(story/edit['path'])!=edit['sha256']:
        raise ValueError('story edit changed')
    protocols=[presentation.read(root/'protocol.json') for root in (hero,story)]
    if any(protocols[0][key]!=protocols[1][key] for key in
           ('app_sha256','bed_sha256','identity','recording_sha256','shaders')):
        raise ValueError('hero/story source identities differ')
    if any(p['source_dirty_diff_sha256']!=hashlib.sha256(b'').hexdigest() for p in protocols):
        raise ValueError('final capture requires a clean source revision')
    out=args.out.absolute()
    if out.exists() or out.parent!=ROOT/'outputs/showcase':
        raise ValueError('fresh direct outputs/showcase leaf required')
    out.mkdir()
    export=out/'export'
    export.mkdir()
    files={'fluid-25d.mp4':hero/row['path'],'fluid-25d.png':hero/row['poster'],
           'fluid-25d-story.mp4':story/edit['path'],'fluid-25d-story.png':story/edit['poster']}
    for name,source in files.items():
        shutil.copy2(source,export/name)
    document=dict(schema='cubey.fluid25d.presentation.v1',source_revision=protocols[0]['source_revision'],
        renderer_sha256=protocols[0]['app_sha256'],bed_sha256=protocols[0]['bed_sha256'],
        scene=protocols[0]['identity'],recording_sha256=protocols[0]['recording_sha256'],
        scopes=['Recorded native SynxFlow fields; Vulkan offscreen rendering',
                'Held between original saves; no physical interpolation',
                'Rain and whitewater are render-only visual cues',
                'Not a live solver or native scanout performance trial'],
        source_protocols=protocols,hero=row,story=edit,
        media={name:dict(path='docs/media/showcase/'+name,bytes=path.stat().st_size,sha256=presentation.digest(path))
               for name,path in files.items()},
        verdict='Agent-selected water-only eased arc; dots/trails retained in the demo. Owner creative review pending.',
        publication='Packaged only; no push or GitHub attachment upload',
        provenance=dict(terrain_generator='Terrain Diffusion',terrain_code_revision='82a0431281f21a6ec3d691a12ee61525de5b0790',
            model_id='xandergos/terrain-diffusion-30m',model_revision='9ef8030cb805b433b98ec25c5dddefbac07a9e26',
            solver='SynxFlow v1.0.1 service worker',solver_upstream_commit='8a9781504204a7b41217b483db4190c1f6f340bc',
            extension_sha256='251cea1bd0658acdfc5b5129038d93970bf1769d7cac00bbf1edff99a886315f',
            license_declarations={'cubey_renderer':'MIT','terrain_code_and_model':'MIT','external_worker_slice':'GPL-3.0-only'},
            generated_media='Cubey-generated presentation; MIT; external code not embedded'))
    presentation.write(export/'fluid-25d-recipe.json',document)
    if args.package_media:
        for name in (*files,'fluid-25d-recipe.json'):
            if (MEDIA/name).exists():
                raise ValueError('will not replace existing media: '+name)
        for name in (*files,'fluid-25d-recipe.json'):
            shutil.copy2(export/name,MEDIA/name)
    page=f'''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Fluid 2.5D — presentation and promotion</title><style>
body{{margin:0;background:#121d25;color:#e5edf3;font:17px/1.55 system-ui}}main{{max-width:1240px;margin:auto;padding:32px}}a{{color:#9cdef4}}
h1{{font-size:2.2rem}}.cards{{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,490px),1fr));gap:24px}}article{{padding:18px;background:#21323c;border-radius:12px}}video{{width:100%;border-radius:8px}}h2{{font-size:1.25rem}}button{{padding:10px 16px;margin:8px 8px 8px 0;background:#b6e5ec;border:0;border-radius:6px;color:#15242b;cursor:pointer}}.quiet{{color:#adc3d0}}details{{margin:24px 0}}li{{margin:8px 0}}</style>
<main><nav><a href="/reports/#project=cubey">Cubey reports</a> · <a href="{AUDITION}">Camera/dots audition</a> · <a href="{presentation.PREVIOUS_REPORT}">Numerical rain-response evidence</a></nav>
<h1>Fluid 2.5D: rain becomes a landscape</h1><p>Two selected cuts of the existing Mountain demo. Real recorded water evolution on unchanged generated terrain; no new solver, terrain, water shader or asset work.</p>
<div class="cards"><article><h2>8-second showcase highlight</h2><video controls muted loop playsinline preload="metadata" poster="fluid-25d.png" src="fluid-25d.mp4"></video><p>A restrained 20° eased arc around the developing stream network. Water-only; no diagnostic dots.</p><button data-play="0">Play highlight</button><button data-seek="0">Inspect halfway</button><p class="quiet">480 native rendered frames · {row['unique_hydraulic_states']} saved hydraulic states · accelerated storm-on window</p></article>
<article><h2>36-second project film</h2><video controls muted playsinline preload="metadata" poster="fluid-25d-story.png" src="fluid-25d-story.mp4"></video><p>Rain across hills → downhill streams → basin collection → retreat after rain stops.</p><button data-play="1">Play story</button><button data-seek="1">Inspect halfway</button><p class="quiet">2160 native rendered frames · six simulated hours · purposeful cuts and phase captions</p></article></div>
<section><h2>How to read it</h2><ol><li>Rain supplies the entire map, not a hidden source point. Blue water follows the existing bed.</li><li>Whitewater sells rapid downhill motion; it is a rendering cue from native flow, not a separately simulated waterfall.</li><li>Rain stops after two simulated hours. Collections can still grow briefly as upstream water arrives.</li><li>The last fixed-camera shot shows streams retreating. It does not certify a permanent, quiet lake.</li></ol></section>
<section><h2>Verdict and promotion</h2><p>The tightened, water-only framing is the strongest showcase treatment. Wider framing exposed the finite patch edge; diagnostic dots remain useful in the live demo and backup review.</p><p>Fluid 2.5D is now indexed as an active project. The quickstart separates optional setup from launch and preserves the earlier development record. Qualified worker paths are explicit, without loosening artifact checks.</p><p><strong>Committed-media package, not public attachment publication.</strong> Owner creative review remains open; no push, GitHub upload or native GUI review was performed.</p></section>
<details><summary>Reproduction and evidence boundaries</summary><p><a href="fluid-25d-recipe.json">Complete recipe, hashes, commands and provenance</a></p><p>Native fields are held between saves; 60 FPS describes rendered/encoded frames, not 60 Hz hydraulic updates or real-time solver performance. The main story uses varying timelapse rates. Rain, foam and optional dots remain visual aids.</p><p>Generated terrain, not surveyed data. Default 1024² / 16 m simulation, 30 m source elevation, 16.384 km patch; cinematic framing selects viewpoints without changing the numerical domain. The separate CUDA worker is not linked into the Vulkan renderer.</p></details></main>
<script>const videos=[...document.querySelectorAll('video')];const loaded=new WeakMap();function localVideo(v){{if(!loaded.has(v)){{const source=v.getAttribute('src');v.dataset.source=source;loaded.set(v,(async()=>{{const response=await fetch(source);if(!response.ok)throw new Error('Video download failed');const url=URL.createObjectURL(await response.blob());await new Promise((resolve,reject)=>{{v.addEventListener('loadedmetadata',resolve,{{once:true}});v.addEventListener('error',reject,{{once:true}});v.src=url;v.load()}})}})())}}return loaded.get(v)}}for(const b of document.querySelectorAll('[data-play]'))b.onclick=async()=>{{const v=videos[+b.dataset.play];await localVideo(v);v.currentTime=0;await v.play()}};for(const b of document.querySelectorAll('[data-seek]'))b.onclick=async()=>{{const v=videos[+b.dataset.seek];await localVideo(v);v.pause();v.currentTime=v.duration/2}};</script></html>'''
    with (export/'index.html').open('x') as stream:
        stream.write(page)
    print(json.dumps({'export':str(export),'files':document['media'],'source_revision':document['source_revision']},indent=2))


if __name__=='__main__':
    main()
