// Static data, assets and mock-DOM coverage. Browser acceptance is separate.
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
for(const name of fs.readdirSync(root).filter(n=>n.endsWith('.json')))JSON.parse(fs.readFileSync(path.join(root,name),'utf8'));
const elements=new Map(ids.map(id=>[id,{value:'',innerHTML:'',textContent:'',src:'',max:4,listeners:{},addEventListener(event,callback){this.listeners[event]=callback;}}]));
const sandbox={window:{},document:{getElementById(id){assert(elements.has(id),id);return elements.get(id);}}};
vm.createContext(sandbox);const scripts=[...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]);
assert.equal(scripts.length,1);vm.runInContext(scripts[0],sandbox,{timeout:10000});
const api=sandbox.window.commitReport;assert.equal(data.timing.length,16);
assert.equal(data.qualified_pairs,61);assert.equal(data.stored_texel_comparisons,2953737728);
assert(data.original_public_binary_unchanged);assert(data.speedups.every(r=>r.passed));
assert.equal(data.measured_commits,480);assert.equal(data.warmup_commits,144);
assert.equal(data.timing.filter(r=>r.mode==='reuse').reduce((n,r)=>n+r.under_16_67,0),0);
let costStates=0,imageStates=0,sequenceStates=0;
for(const subject of ['guardian','robot'])for(const operation of ['add','cut'])for(const allocation of ['pooled','new']){
 elements.get('subject').value=subject;elements.get('operation').value=operation;elements.get('allocation').value=allocation;
 elements.get('allocation').listeners.change();const rows=api.updateCost();assert.equal(rows.length,2);
 assert.equal((elements.get('cost-table').innerHTML.match(/<tr>/g)||[]).length,2);
 assert.equal((elements.get('overview').innerHTML.match(/<tr>/g)||[]).length,4);
 assert(!elements.get('chart').innerHTML.includes('NaN'));costStates++;
}
for(const subject of ['guardian','robot'])for(const operation of ['add','cut'])for(const compare of ['before','handoff']){
 elements.get('subject').value=subject;elements.get('operation').value=operation;elements.get('compare').value=compare;
 const row=api.updateImages(),i=compare==='before'?0:1;
 assert.equal(elements.get('left-image').src,row.frames[i].image);
 assert.equal(elements.get('right-image').src,row.frames[i+1].image);imageStates++;
}
for(const row of data.sequences){
 elements.get('subject').value=row.subject;elements.get('operation').value=row.operation;elements.get('failure').value=row.failure;
 elements.get('failure').listeners.change();assert.equal(Number(elements.get('frame').value),0);
 for(let i=0;i<row.frames.length;i++){
  elements.get('frame').value=String(i);elements.get('frame').listeners.input();
  assert.equal(elements.get('sequence-image').src,row.frames[i].image);
  assert(fs.existsSync(path.join(root,row.frames[i].image)));
  assert(elements.get('sequence-state').textContent.includes(`Pending after completion: ${row.frames[i].pending_after_completed_frame}`));sequenceStates++;
 }
 assert(fs.existsSync(path.join(root,row.control)));
 if(row.failure!=='none')assert.equal(row.frames[2].pending_after_completed_frame,1);
 assert.equal(row.frames[row.retired_after].pending_after_completed_frame,0);
}
elements.get('reset').listeners.click();
for(const [key,value] of Object.entries({subject:'guardian',operation:'cut',allocation:'pooled',compare:'before',failure:'build-capacity',frame:0}))assert.equal(String(elements.get(key).value),String(value));
assert(data.rejected_pruning.filter(r=>r.differing_r16===null).length===2);
assert(!elements.get('pruning-table').innerHTML.includes('NaN'));
console.log(JSON.stringify({passed:true,scope:'Static HTML/data/assets and mock-DOM interaction; browser rendering is checked separately',ids:ids.length,cost_states:costStates,image_states:imageStates,sequences:data.sequences.length,sequence_states:sequenceStates,reset:true},null,2));
