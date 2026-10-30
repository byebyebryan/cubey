// Static HTML/assets/mock-DOM checks; browser acceptance is separate.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const root=path.resolve(process.argv[2]);
const html=fs.readFileSync(path.join(root,'index.html'),'utf8');
const data=JSON.parse(fs.readFileSync(path.join(root,'summary.json'),'utf8'));
const ids=[...html.matchAll(/\bid="([^"]+)"/g)].map(m=>m[1]);
assert.equal(ids.length,new Set(ids).size);assert(!html.includes('@@'));
for(const [,href] of html.matchAll(/href="([^"]+)"/g)){
 if(href.startsWith('#'))assert(ids.includes(href.slice(1)));
 else if(!href.startsWith('http'))assert(fs.existsSync(path.join(root,href)),href);
}
const elements=new Map(ids.map(id=>[id,{value:({subject:'guardian',view:'preset',compare:'before','seq-subject':'guardian','seq-op':'add',fault:'false',frame:'0'})[id]??'',innerHTML:'',textContent:'',src:'',max:3,listeners:{},addEventListener(event,callback){this.listeners[event]=callback;}}]));
const sandbox={window:{},document:{getElementById(id){assert(elements.has(id),id);return elements.get(id);}}};
vm.createContext(sandbox);const scripts=[...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]);
assert.equal(scripts.length,1);vm.runInContext(scripts[0],sandbox,{timeout:10000});
const api=sandbox.window.cachedEditReport;assert.equal(data.timing.length,24);
assert(data.zero_checks.every(r=>r.passed));assert(data.replay.runs.every(r=>r.changed_cached_rays>=10));
let costStates=0,imageStates=0,sequenceStates=0;
for(const subject of ['guardian','robot'])for(const view of ['preset','close']){
 elements.get('subject').value=subject;elements.get('view').value=view;
 elements.get('view').listeners.change();const rows=api.updateCost();assert.equal(rows.length,6);
 assert.equal((elements.get('timing-table').innerHTML.match(/<tr>/g)||[]).length,6);
 assert(!elements.get('chart').innerHTML.includes('NaN'));costStates++;
}
elements.get('reset').listeners.click();assert.equal(elements.get('subject').value,'guardian');assert.equal(elements.get('view').value,'preset');
for(const [key,images] of Object.entries(data.images))for(const compare of ['before','handoff']){
 elements.get('image-case').value=key;elements.get('compare').value=compare;elements.get('compare').listeners.change();
 assert.equal(elements.get('left-image').src,images[compare==='before'?'base':'preview']);
 assert.equal(elements.get('right-image').src,images[compare==='before'?'preview':'committed']);
 for(const source of Object.values(images))assert(fs.existsSync(path.join(root,source)),source);imageStates++;
}
for(const row of data.sequences){
 elements.get('seq-subject').value=row.subject;elements.get('seq-op').value=row.operation;elements.get('fault').value=String(row.fault);
 elements.get('fault').listeners.change();assert.equal(Number(elements.get('frame').value),0);
 for(let i=0;i<row.frames.length;i++){
  elements.get('frame').value=String(i);elements.get('frame').listeners.input();
  assert.equal(elements.get('sequence-image').src,row.frames[i].image);
  assert(fs.existsSync(path.join(root,row.frames[i].image)));sequenceStates++;
 }
}
for(const row of data.windows)assert(fs.existsSync(path.join(root,row.image)));
console.log(JSON.stringify({passed:true,scope:'Static HTML, links/assets and mock DOM interaction; separate from browser rendering',ids:ids.length,cost_states:costStates,image_states:imageStates,sequence_states:sequenceStates,reset:true},null,2));
