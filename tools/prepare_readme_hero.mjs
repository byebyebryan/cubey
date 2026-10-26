// Reproduce the approved native-capture collage, using the site's locked Sharp.
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
const sharp=createRequire(new URL('../site/package.json',import.meta.url))('sharp');
const root=new URL('../',import.meta.url);
const recipe=JSON.parse(readFileSync(new URL('docs/media/readme-hero.json',root),'utf8'));
const ledger=JSON.parse(readFileSync(new URL('site/media/manifest.json',root),'utf8'));
const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const round=value=>Math.round(value*100)/100;
const {width,height,cutOffset,dividerWidth,dividerColor}=recipe.canvas;
const count=recipe.panels.length;
let defs='',images='',dividers='';

for(const [i,panel] of recipe.panels.entries()){
  const asset=ledger.assets.find(item=>item.id===panel.asset);
  if(!asset||asset.kind!=='image')throw new Error('Missing capture: '+panel.asset);
  const bytes=readFileSync(new URL('site/public'+asset.file,root));
  if(digest(bytes)!==panel.sha256)throw new Error('Capture changed; review before updating recipe: '+panel.asset);
  const png=await sharp(bytes).png().toBuffer();
  const source='data:image/png;base64,'+png.toString('base64');
  const image=(x,y,w,h)=>`<image xlink:href="${source}" x="${x}" y="${y}" width="${w}" height="${h}" preserveAspectRatio="xMidYMid meet"/>`;
  const start=width*i/count,end=width*(i+1)/count;
  const tl=i===0?0:start+cutOffset/2,bl=i===0?0:start-cutOffset/2;
  const tr=i===count-1?width:end+cutOffset/2,br=i===count-1?width:end-cutOffset/2;
  const left=Math.min(tl,bl),right=Math.max(tr,br),id='cubey-hero-angled-'+i;
  defs+=`<clipPath id="${id}"><polygon points="${tl},0 ${tr},0 ${br},${height} ${bl},${height}"/></clipPath>`;
  let content;
  if(panel.fitVolume){
    const {bounds,scale}=panel.fitVolume;
    const w=asset.width*scale,h=asset.height*scale,centre=(start+end)/2;
    const x=centre-(bounds.left+bounds.right)*.5*scale;
    const y=height/2-(bounds.top+bounds.bottom)*.5*scale,bottom=y+h;
    // Keep the complete subject within the slanted panel, with a visible margin.
    for(const sx of [bounds.left,bounds.right])for(const sy of [bounds.top,bounds.bottom]){
      const px=x+sx*scale,py=y+sy*scale,axis=centre+cutOffset/height*(height/2-py);
      if(px<=axis-width/count/2+12||px>=axis+width/count/2-12||py<=0||py>=height)
        throw new Error('Subject extends beyond its panel: '+panel.asset);
    }
    if(x>left||x+w<right||y>0||bottom>=height)throw new Error('Unsupported background extension');
    content=`<svg x="${x}" y="${bottom}" width="${w}" height="${height-bottom}" viewBox="0 ${asset.height-1} ${asset.width} 1" preserveAspectRatio="none" overflow="hidden">${image(0,0,asset.width,asset.height)}</svg>`+image(x,y,w,h);
  }else{
    const w=height*asset.width/asset.height;
    content=image(round(left-(w-(right-left))*panel.positionX),0,round(w),height);
  }
  images+=`<g clip-path="url(#${id})">${content}</g>`;
  if(i)dividers+=`<line x1="${tl}" y1="0" x2="${bl}" y2="${height}" stroke="${dividerColor}" stroke-width="${dividerWidth}"/>`;
}

const svg=`<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}"><defs>${defs}</defs>${images}${dividers}</svg>`;
const output=await sharp(Buffer.from(svg)).resize({width:recipe.output.width}).png().toBuffer();
const metadata=await sharp(output).metadata();
if(metadata.width!==recipe.output.width||metadata.height!==recipe.output.height||digest(output)!==recipe.output.sha256)
  throw new Error('Output differs from the approved hero; review before updating its recorded identity.');
writeFileSync(new URL(recipe.output.file,root),output);
console.log(JSON.stringify({file:recipe.output.file,width:metadata.width,height:metadata.height,bytes:output.length,sha256:digest(output)}));
