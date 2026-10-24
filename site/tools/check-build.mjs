import {readFileSync,readdirSync,existsSync,writeFileSync} from 'node:fs';
import {resolve,dirname,extname} from 'node:path';
import {fileURLToPath} from 'node:url';
const site=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const dist=resolve(site,'dist');
const files=readdirSync(dist,{recursive:true}).filter(p=>p.endsWith('.html'));
const errors=[];let refs=0;const loadingBounds=[];
const pages=new Map(files.map(p=>[p,readFileSync(resolve(dist,p),'utf8')]));
const prefix='/mounted/cubey/';
for(const [name,html] of pages){
  const initialAssets=new Set();
  if((html.match(/<h1(?:\s|>)/g)||[]).length!==1)errors.push(`${name}: needs exactly one main heading`);
  if(/LAYOUT PLACEHOLDER|DESIGN MOCKUP|Choose a scene|localhost:|starship:|outputs\/cubey-site/i.test(html))errors.push(`${name}: development content leaked`);
  const ids=[...html.matchAll(/\sid="([^"]+)"/g)].map(m=>m[1]);
  if(new Set(ids).size!==ids.length)errors.push(`${name}: duplicate anchor IDs`);
  for(const match of html.matchAll(/\s(href|src|poster|data-image|data-mesh|data-sdf)="([^"]+)"/g)){
    const value=match[2].replaceAll('&amp;','&');
    if(/^(?:https?:|mailto:|data:)/.test(value))continue;
    refs++;
    if(value.startsWith('/'))errors.push(`${name}: non-portable absolute URL ${value}`);
    const pageRoute=name.endsWith('index.html')?name.slice(0,-10):name;
    const url=new URL(value,'https://static.test'+prefix+pageRoute);
    if(!url.pathname.startsWith(prefix)){errors.push(`${name}: escapes hosting prefix ${value}`);continue;}
    let target=decodeURIComponent(url.pathname.slice(prefix.length));
    if(!target)target='index.html';
    else if(target.endsWith('/')||!extname(target))target+=(target.endsWith('/')?'':'/')+'index.html';
    if(!existsSync(resolve(dist,target))){errors.push(`${name}: broken URL ${value}`);continue;}
    const tag=html.slice(html.lastIndexOf('<',match.index),match.index).match(/^<([a-zA-Z]+)/)?.[1];
    const embedded=match[1]==='src'||match[1]==='poster'||(match[1]==='href'&&tag==='link');
    if(embedded&&['.css','.js','.webp','.png','.svg'].includes(extname(target)))initialAssets.add(target);
    if(url.hash&&target.endsWith('.html')){
      const targetHTML=pages.get(target)||readFileSync(resolve(dist,target),'utf8');
      if(!targetHTML.includes(`id="${decodeURIComponent(url.hash.slice(1))}"`))errors.push(`${name}: missing anchor ${value}`);
    }
  }
  for(const video of html.matchAll(/<video\b[^>]*>/g))if(!video[0].includes('preload="none"')||/\bautoplay\b|\bloop\b/.test(video[0]))errors.push(`${name}: unexpected eager/autoplay/looping film`);
  if(/<a\b[^>]*>(?:(?!<\/a>)[\s\S])*?<video\b/.test(html))errors.push(`${name}: native player nested in a navigation link`);
  if(/demos\/fractals|Fractal field/.test(html))errors.push(`${name}: deferred fractals leaked into the public showcase`);
  for(const image of html.matchAll(/<img\b[^>]*>/g))if(!/\balt=/.test(image[0])||!/\bwidth=/.test(image[0])||!/\bheight=/.test(image[0]))errors.push(`${name}: image lacks accessible text/dimensions`);
  const bound=Buffer.byteLength(html)+[...initialAssets].reduce((sum,p)=>sum+readFileSync(resolve(dist,p)).length,0);
  loadingBounds.push({page:name,bytes:bound});
  if(bound>600_000)errors.push(`${name}: HTML/styles/scripts/all maximum-size poster images exceed 600 KB budget`);
}
for(const name of ['index.html','demos/index.html','demos/simulation/index.html','demos/sdf/index.html'])if(!pages.get(name)?.includes('<video '))errors.push(`${name}: overview has no playable film`);
const result={passed:errors.length===0,errors,pages:pages.size,internalReferences:refs,checkedMount:prefix,maximumPageImageResourceBound:Math.max(...loadingBounds.map(p=>p.bytes)),loadingBounds,scope:'Built HTML/asset/anchor and media policy audit; image resource bounds include full-size posters before responsive/lazy savings, exclude user-requested films; browser and public deployment checks remain separate'};
console.log(JSON.stringify(result,null,2));
if(process.env.SITE_CHECK_RECEIPT)writeFileSync(process.env.SITE_CHECK_RECEIPT,JSON.stringify(result,null,2)+'\n');
if(errors.length)process.exit(1);
