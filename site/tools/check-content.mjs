import {readFileSync,existsSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {resolve,dirname} from 'node:path';
const site=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const root=resolve(site,'..');
const content=JSON.parse(readFileSync(resolve(root,'docs/notes/cubey-site-content-map.json'),'utf8'));
const media=JSON.parse(readFileSync(resolve(site,'media/manifest.json'),'utf8'));
const library=JSON.parse(readFileSync(resolve(root,'assets/materials/catalog.json'),'utf8'));
const errors=[];
const assets=new Map(media.assets.map(a=>[a.id,a]));
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
function fastStart(bytes){
  let moov=-1,mdat=-1;
  for(let offset=0;offset<bytes.length;){
    if(offset+8>bytes.length)return false;
    const type=bytes.toString('ascii',offset+4,offset+8);let size=bytes.readUInt32BE(offset),header=8;
    if(size===1){if(offset+16>bytes.length)return false;size=Number(bytes.readBigUInt64BE(offset+8));header=16;}
    else if(size===0)size=bytes.length-offset;
    if(!Number.isSafeInteger(size)||size<header||offset+size>bytes.length)return false;
    if(type==='moov'&&moov<0)moov=offset;if(type==='mdat'&&mdat<0)mdat=offset;
    offset+=size;
  }
  return moov>=0&&mdat>=0&&moov<mdat;
}
if(assets.size!==media.assets.length)errors.push('Duplicate media IDs');
for(const a of media.assets){
  const file=resolve(site,'public',a.file.replace(/^\//,''));
  if(!file.startsWith(resolve(site,'public/media')+'/'))errors.push(`Asset outside media directory: ${a.id}`);
  if(!existsSync(file)){errors.push(`Missing public asset: ${a.id}`);continue;}
  const bytes=readFileSync(file);
  if(sha(bytes)!==a.sha256||bytes.length!==a.bytes)errors.push(`Asset identity changed: ${a.id}`);
  if(!(a.width>0&&a.height>0))errors.push(`Missing dimensions: ${a.id}`);
  if(!a.credit?.label||!a.credit?.url||!a.provenance?.sourceSha256)errors.push(`Incomplete attribution/provenance: ${a.id}`);
  if(a.thumbnail){const thumb=resolve(site,'public',a.thumbnail.replace(/^\//,''));if(!existsSync(thumb)||sha(readFileSync(thumb))!==a.thumbnailSha256)errors.push(`Thumbnail identity changed: ${a.id}`);}
  if(a.provenance.recipe?.startsWith('site/')&&!existsSync(resolve(root,a.provenance.recipe)))errors.push(`Missing capture receipt: ${a.id}`);
  if(a.provenance.webDeliveryReceipt&&!existsSync(resolve(root,a.provenance.webDeliveryReceipt)))errors.push(`Missing web delivery receipt: ${a.id}`);
  if(a.kind==='image'&&a.bytes>250_000)errors.push(`Image exceeds 250 KB budget: ${a.id}`);
  if(a.kind==='video'&&a.bytes>8_000_000)errors.push(`Film exceeds 8 MB budget: ${a.id}`);
  if(a.kind==='video'&&!fastStart(bytes))errors.push(`Film is not fast-start: ${a.id}`);
}
const ids=new Set(content.entries.map(e=>e.id));
for(const e of content.entries){
  const selected=media.selections[e.id];
  if(!selected?.poster||assets.get(selected.poster)?.kind!=='image')errors.push(`No native poster: ${e.id}`);
  if(selected?.film&&assets.get(selected.film)?.kind!=='video')errors.push(`No native film: ${e.id}`);
  if(!existsSync(resolve(root,e.source)))errors.push(`Missing project/source guide: ${e.id}`);
  for(const related of e.related)if(!ids.has(related))errors.push(`Unknown related demo: ${e.id}/${related}`);
}
for(const [name,selection] of Object.entries(media.selections))for(const id of [selection.poster,selection.film,...(selection.gallery||[])].filter(Boolean))if(!assets.has(id))errors.push(`Missing selected asset ${name}/${id}`);
const supported=new Set([...readFileSync(resolve(root,'docs/architecture/gltf-assets.md'),'utf8').matchAll(/^\| `(KHR_[^`]+)` \| supported/gm)].map(m=>m[1]));
const mapped=new Set(content.entries.flatMap(e=>e.extensions));
if(supported.size!==mapped.size||[...supported].some(id=>!mapped.has(id)))errors.push('Supported glTF extension map changed');
if(content.families.length!==7||content.entries.length!==21||new Set(content.entries.map(e=>e.owner)).size!==13)errors.push('Reviewed subject/demo/native-owner roster changed');
if(content.families.some(f=>f.id==='fractals')||content.entries.some(e=>e.family==='fractals'))errors.push('Deferred fractals returned to the public catalog');
if(library.entries.length!==12)errors.push('Shared twelve-material catalog changed');
for(const material of library.entries)for(const renderer of ['mesh','sdf'])if(assets.get(`library-${renderer}-${material.id}`)?.kind!=='image')errors.push(`Missing ${renderer} library specimen: ${material.id}`);
for(const id of ['mesh-materials','library-mesh-dawn','library-sdf-dusk','library-sdf-dawn']){
  for(const [suffix,kind] of [['','image'],['-film','video']]){
    const a=assets.get(id+suffix);
    if(a?.kind!==kind||a.width*9!==a.height*16)errors.push(`Missing landscape material study: ${id+suffix}`);
  }
}
const nativeReceipt=JSON.parse(readFileSync(resolve(site,'media/receipts/native-showcases.json'),'utf8'));
for(const [id,capture] of Object.entries(nativeReceipt.assets)){
  const a=assets.get(id);
  if(!a||a.width*9!==a.height*16)errors.push(`Recapture lost its native landscape frame: ${id}`);
  if(a?.provenance.sourceRevision!==capture.sourceHead||capture.exitCode!==0)errors.push(`Native capture identity mismatch: ${id}`);
  if(a?.kind==='video'&&a.sha256!==capture.webEncode?.sha256)errors.push(`Film differs from its delivery receipt: ${id}`);
  if(a?.kind==='image'&&a.provenance.sourceSha256!==capture.sha256)errors.push(`Poster differs from its native master: ${id}`);
}
if(content.entries.filter(e=>e.family==='simulation').length!==6)errors.push('Simulation collection is incomplete');
for(const c of content.connections)for(const id of [...c.from_entries,...c.to_entries])if(!ids.has(id))errors.push(`Unknown connection ${c.id}/${id}`);
const result={passed:errors.length===0,errors,subjects:content.families.length,entries:content.entries.length,nativeOwners:new Set(content.entries.map(e=>e.owner)).size,gltfExtensions:mapped.size,assets:assets.size,bytes:media.assets.reduce((n,a)=>n+a.bytes+(a.thumbnailBytes||0),0)};
console.log(JSON.stringify(result,null,2));
if(errors.length)process.exit(1);
