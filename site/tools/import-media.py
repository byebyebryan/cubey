"""One-time editorial import, not part of the clean Node-only site build."""
from pathlib import Path
import argparse, hashlib, json, shutil, subprocess

ROOT=Path(__file__).resolve().parents[2]
SITE=ROOT/'site'
p=argparse.ArgumentParser()
p.add_argument('id');p.add_argument('source',type=Path)
p.add_argument('--entry');p.add_argument('--role',choices=['poster','film','gallery'])
p.add_argument('--caption',required=True)
p.add_argument('--credit',default='Cubey original media · MIT')
p.add_argument('--credit-url',default='https://github.com/byebyebryan/cubey/blob/main/LICENSE')
p.add_argument('--author',default='Bryan')
p.add_argument('--source-url');p.add_argument('--revision',default='')
p.add_argument('--recipe',default='');p.add_argument('--note',default='')
args=p.parse_args()
src=args.source.resolve()
if not src.is_file():raise SystemExit('Missing selected input: '+str(src))
manifest=SITE/'media/manifest.json';m=json.loads(manifest.read_text())
out=SITE/'public/media';out.mkdir(parents=True,exist_ok=True)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
video=src.suffix.lower()=='.mp4'
dest=out/(args.id+('.mp4' if video else '.webp'))
if video:
 shutil.copy2(src,dest)
 info=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(dest)]))
 stream=next(s for s in info['streams'] if s['codec_type']=='video');width,height=stream['width'],stream['height']
else:
 info=json.loads(subprocess.check_output(['node',str(SITE/'tools/prepare-image.mjs'),str(src),str(dest)]))
 width,height=info['width'],info['height']
asset={'id':args.id,'file':'/media/'+dest.name,'kind':'video' if video else 'image','width':width,'height':height,'sha256':sha(dest),'bytes':dest.stat().st_size,'caption':args.caption,'credit':{'label':args.credit,'url':args.credit_url,'author':args.author},'provenance':{'source':str(src.relative_to(ROOT)) if src.is_relative_to(ROOT) else str(src),'sourceSha256':sha(src),'sourceRevision':args.revision or None,'recipe':args.recipe,'note':args.note}}
if args.source_url:asset['credit']['source']=args.source_url
if not video and width>640:
 thumbnail=out/(args.id+'-640.webp')
 asset.update(thumbnail='/media/'+thumbnail.name,thumbnailSha256=sha(thumbnail),thumbnailBytes=thumbnail.stat().st_size)
m['assets']=[a for a in m['assets'] if a['id']!=args.id]+[asset]
if args.entry:
 selection=m['selections'].setdefault(args.entry,{})
 if args.role=='gallery':selection.setdefault('gallery',[]).append(args.id)
 elif args.role:selection[args.role]=args.id
m['assets'].sort(key=lambda a:a['id'])
manifest.write_text(json.dumps(m,indent=2)+'\n')
print(json.dumps({'id':args.id,'kind':asset['kind'],'bytes':asset['bytes'],'entry':args.entry,'role':args.role}))
