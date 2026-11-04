#!/usr/bin/env python3
"""Prepare and optionally select web derivatives of the approved native CSG packet."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]
SITE=ROOT/'site'
RECEIPT='site/media/receipts/csg-showcase.json'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path,value):path.write_text(json.dumps(value,indent=2)+'\n')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True,help='Fresh derivative staging directory')
    parser.add_argument('--import-media',action='store_true',help='Install staged files and select the approved quartet in the local site')
    args=parser.parse_args();native=args.native.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    manifest=json.loads((native/'manifest.json').read_text())
    assert manifest['complete']and not manifest['postersOnly']
    roster=json.loads((ROOT/'projects/sdf/tools/csg_showcase.json').read_text())
    assert manifest['rosterSha256']==sha(ROOT/'projects/sdf/tools/csg_showcase.json')
    candidate=json.loads((SITE/'media/manifest.json').read_text())
    old_ids={a['id']for a in candidate['assets']}
    receipt=dict(format='cubey.site.csg-showcase',version=1,sourceHead=manifest['sourceHead'],
        approvedModelCommit=manifest['approvedModelCommit'],nativeManifest=str((native/'manifest.json').relative_to(ROOT)),
        nativeManifestSha256=sha(native/'manifest.json'),binarySha256=manifest['binarySha256'],
        shaderSha256=manifest['shaderSha256'],assets={},scope='Native CSG stills, saved construction and camera arcs; web derivatives and hover previews. No new frame-time claim.')
    extra=[];source_paths={}
    credit=dict(label='Cubey original media · MIT',url='https://github.com/byebyebryan/cubey/blob/main/LICENSE',author='Bryan')
    titles={e['id']:e['title']for e in roster['entries']}
    for identifier,row in manifest['runs'].items():
        assert identifier not in old_ids,'Refuse to replace historical media: '+identifier
        source=native/row['file'];assert sha(source)==row['sha256']
        video=row['kind']=='video';filename=identifier+('.mp4'if video else'.webp');target=out/filename
        if video:
            command=['ffmpeg','-v','error','-n','-i',str(source),'-an','-vf','scale=1280:720:flags=lanczos',
                     '-c:v','libx264','-preset','slow','-crf','22','-pix_fmt','yuv420p','-movflags','+faststart',str(target)]
        else:
            command=['ffmpeg','-v','error','-n','-i',str(source),'-c:v','libwebp','-quality','84',str(target)]
        subprocess.run(command,check=True,capture_output=True)
        title=titles[row['subject']]
        caption=('Saved native construction replay of the approved field-kit robot.'if identifier=='csg-robot-film'else
                 'Native camera arc around the '+title.lower()+'.'if video else
                 'Native CSG render: '+title+'.')
        if identifier.endswith('-section'):caption='A saved architectural cut reveals the observatory telescope, mount and keeper room; camera and light match the intact view.'
        if identifier.endswith('-intact'):caption='The intact observatory, captured with the same camera and light as its architectural section.'
        asset=dict(id=identifier,file='/media/'+filename,kind=row['kind'],width=1280 if video else 1600,
            height=720 if video else 900,sha256=sha(target),bytes=target.stat().st_size,caption=caption,credit=credit,
            provenance=dict(source=str(source.relative_to(ROOT)),sourceSha256=row['sha256'],sourceRevision=manifest['sourceHead'],
                recipe=RECEIPT,webDeliveryReceipt=RECEIPT,note='Current approved CSG source. Native artifact identity and scene/preset hashes are recorded separately.'))
        assert asset['bytes']<=(8_000_000 if video else 250_000)
        record=dict(sourceHead=manifest['sourceHead'],sourceScene=row['sourceScene'],sourceSceneSha256=row['sourceSceneSha256'],
            sourcePreset=row['sourcePreset'],sourcePresetSha256=row['sourcePresetSha256'],nativeFile=str(source.relative_to(ROOT)),
            nativeSha256=row['sha256'],nativeCommand=row['command'],nativeExitCode=row['exitCode'],nativeLogSha256=row['logSha256'],
            webEncode=dict(command=command,sha256=asset['sha256'],bytes=asset['bytes'],width=asset['width'],height=asset['height']))
        if video:
            source_paths[identifier]=target;record['nativeVideo']=row['video']
            if 'savedStatesVisited'in row:record['savedStatesVisited']=row['savedStatesVisited']
        else:
            thumb=out/(identifier+'-640.webp')
            thumb_cmd=['ffmpeg','-v','error','-n','-i',str(source),'-vf','scale=640:360:flags=lanczos','-c:v','libwebp','-quality','82',str(thumb)]
            subprocess.run(thumb_cmd,check=True,capture_output=True)
            asset.update(thumbnail='/media/'+thumb.name,thumbnailSha256=sha(thumb),thumbnailBytes=thumb.stat().st_size)
            record['thumbnail']=dict(command=thumb_cmd,sha256=sha(thumb),bytes=thumb.stat().st_size)
            extra.append((thumb,SITE/'public/media'/thumb.name))
        candidate['assets'].append(asset);receipt['assets'][identifier]=record
        extra.append((target,SITE/'public/media'/filename))
        print(identifier,asset['bytes'],'bytes',flush=True)
    for e in roster['entries']:
        identifier=e['id'];selection=dict(poster='csg-'+identifier,film='csg-'+identifier+'-film',preview='preview-csg-'+identifier)
        if identifier=='observatory':selection['gallery']=['csg-observatory-intact','csg-observatory-section']
        candidate['selections'][identifier]=selection
    module_spec=importlib.util.spec_from_file_location('csg_previews',Path(__file__).with_name('prepare-hover-previews.py'))
    previews=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(previews)
    candidate,hover_receipt,outputs=previews.prepare(candidate,out/'previews',[e['id']for e in roster['entries']],source_paths)
    save(out/'csg-showcase.json',receipt);save(out/'manifest.json',candidate);save(out/'hover-previews.json',hover_receipt)
    extra.append((out/'csg-showcase.json',ROOT/RECEIPT))
    if args.import_media:previews.commit(candidate,hover_receipt,outputs,extra_files=extra)
    print('Imported local selections'if args.import_media else'Prepared candidate ledger; local site not changed')


if __name__=='__main__':main()
