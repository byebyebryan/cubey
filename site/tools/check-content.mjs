import {readFileSync,existsSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {resolve,dirname} from 'node:path';
import {hoverPreviewErrors} from './hover-preview-contract.mjs';
const site=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const root=resolve(site,'..');
const content=JSON.parse(readFileSync(resolve(root,'docs/notes/cubey-site-content-map.json'),'utf8'));
const media=JSON.parse(readFileSync(resolve(site,'media/manifest.json'),'utf8'));
const previewReceipt=JSON.parse(readFileSync(resolve(site,'media/receipts/hover-previews.json'),'utf8'));
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
for(const [name,selection] of Object.entries(media.selections)){
  for(const id of [selection.poster,selection.film,selection.preview,...(selection.gallery||[])].filter(Boolean))if(!assets.has(id))errors.push(`Missing selected asset ${name}/${id}`);
}
errors.push(...hoverPreviewErrors(media,assets,previewReceipt));
const supported=new Set([...readFileSync(resolve(root,'docs/architecture/gltf-assets.md'),'utf8').matchAll(/^\| `(KHR_[^`]+)` \| supported/gm)].map(m=>m[1]));
const mapped=new Set(content.entries.flatMap(e=>e.extensions));
if(supported.size!==mapped.size||[...supported].some(id=>!mapped.has(id)))errors.push('Supported glTF extension map changed');
if(content.families.length!==7||content.entries.length!==23||new Set(content.entries.map(e=>e.owner)).size!==13)errors.push('Reviewed subject/demo/native-owner roster changed');
const csgIds=['observatory','robot','canal-mill','courtyard'];
if(ids.has('guardian')||content.entries.filter(e=>e.family==='sdf').map(e=>e.id).join(',')!==csgIds.join(','))errors.push('The active CSG showcase must contain the approved quartet; Guardian is a retained study');
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
const csgRoster=JSON.parse(readFileSync(resolve(root,'projects/sdf/tools/csg_showcase.json'),'utf8'));
const csgReceipt=JSON.parse(readFileSync(resolve(site,'media/receipts/csg-showcase.json'),'utf8'));
if(csgRoster.entries.map(e=>e.id).join(',')!==csgIds.join(','))errors.push('CSG native/site roster differs');
for(const e of csgRoster.entries){
  const source=resolve(root,e.scene);
  if(!existsSync(source)||sha(readFileSync(source))!==e.approvedSceneSha256)errors.push(`Approved CSG scene changed: ${e.id}`);
  const selected=media.selections[e.id];
  if(selected?.poster!==`csg-${e.id}`||selected?.film!==`csg-${e.id}-film`)errors.push(`CSG current-source media selection changed: ${e.id}`);
}
for(const [id,capture]of Object.entries(csgReceipt.assets)){
  const a=assets.get(id),source=resolve(root,capture.sourceScene),preset=resolve(root,capture.sourcePreset);
  if(!a||a.width*9!==a.height*16||capture.nativeExitCode!==0)errors.push(`CSG native capture invalid: ${id}`);
  if(a?.provenance.sourceRevision!==csgReceipt.sourceHead||a?.provenance.sourceSha256!==capture.nativeSha256||a?.sha256!==capture.webEncode.sha256)
    errors.push(`CSG derivative provenance mismatch: ${id}`);
  if(!existsSync(source)||sha(readFileSync(source))!==capture.sourceSceneSha256||!existsSync(preset)||sha(readFileSync(preset))!==capture.sourcePresetSha256)
    errors.push(`CSG capture inputs changed: ${id}`);
}
const robotCapture=csgReceipt.assets['csg-robot-film'];
const robotHistory=JSON.parse(readFileSync(resolve(root,csgRoster.entries.find(e=>e.id==='robot').scene),'utf8')).history;
if(robotCapture?.savedStatesVisited?.join(',')!==robotHistory.map((_,i)=>i+1).join(','))errors.push('Robot film must visit every approved saved history state');
const intact=JSON.parse(readFileSync(resolve(root,csgRoster.cutaway.intactPreset),'utf8'));
const section=JSON.parse(readFileSync(resolve(root,csgRoster.cutaway.sectionPreset),'utf8'));
delete intact['authored-scene'];delete section['authored-scene'];
if(JSON.stringify(intact)!==JSON.stringify(section))errors.push('Observatory cutaway camera/lighting settings differ');
if(sha(readFileSync(resolve(root,csgRoster.cutaway.sectionScene)))!==csgRoster.cutaway.approvedSectionSha256)errors.push('Approved observatory section changed');
for(const id of ['csg-observatory-intact','csg-observatory-section'])if(!csgReceipt.assets[id])errors.push(`Missing matched cutaway capture: ${id}`);
if(content.entries.filter(e=>e.family==='simulation').length!==6)errors.push('Simulation collection is incomplete');
for(const c of content.connections)for(const id of [...c.from_entries,...c.to_entries])if(!ids.has(id))errors.push(`Unknown connection ${c.id}/${id}`);
const result={passed:errors.length===0,errors,subjects:content.families.length,entries:content.entries.length,nativeOwners:new Set(content.entries.map(e=>e.owner)).size,gltfExtensions:mapped.size,assets:assets.size,bytes:media.assets.reduce((n,a)=>n+a.bytes+(a.thumbnailBytes||0),0)};
console.log(JSON.stringify(result,null,2));
if(errors.length)process.exit(1);
