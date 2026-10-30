// Static interaction and asset checks. No browser rendering is claimed.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';

const root=path.resolve(process.argv[2]);
const html=fs.readFileSync(path.join(root,'index.html'),'utf8');
const data=JSON.parse(fs.readFileSync(path.join(root,'summary.json'),'utf8'));
const ids=[...html.matchAll(/\bid="([^"]+)"/g)].map(m=>m[1]);
assert.equal(new Set(ids).size,ids.length);
assert(!html.includes('@@'));
for(const [,href] of html.matchAll(/href="([^"]+)"/g)){
  if(href.startsWith('#'))assert(ids.includes(href.slice(1)));
  else if(!href.startsWith('http'))assert(fs.existsSync(path.join(root,href)),href);
}
let assets=0;
for(const images of Object.values(data.images))for(const uri of Object.values(images)){
  assert(fs.existsSync(path.join(root,uri)),uri);assets++;
}
for(const states of Object.values(data.motion))for(const frames of Object.values(states)){
  assert.equal(frames.length,16);
  for(const uri of frames){assert(fs.existsSync(path.join(root,uri)),uri);assets++;}
}
const elements=new Map(ids.map(id=>[id,{
  value:({subject:'robot',rows:'all','motion-subject':'robot',frame:'0'})[id]??'',
  innerHTML:'',textContent:'',src:'',listeners:{},
  addEventListener(type,callback){this.listeners[type]=callback;},
}]));
let tick;
const sandbox={window:{},document:{getElementById(id){assert(elements.has(id),id);return elements.get(id);}},
  setInterval(callback){tick=callback;return 1;},clearInterval(){tick=null;}};
vm.createContext(sandbox);
const scripts=[...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]);
assert.equal(scripts.length,1);vm.runInContext(scripts[0],sandbox,{timeout:10000});
const api=sandbox.window.liveEditReport;
assert.equal(api.data.timing.length,36);
assert.equal(api.data.guards.source_gate,true);
assert.equal(api.data.guards.automatic_sampled_handoff_gate,false);
let timingStates=0,imageStates=0,motionStates=0;
for(const subject of ['robot','guardian'])for(const filter of ['all','live','controls']){
  elements.get('subject').value=subject;elements.get('rows').value=filter;
  elements.get('rows').listeners.change();
  const rows=api.updateTiming();assert(rows.length>0);
  assert(rows.every(r=>r.subject===subject));
  if(filter!=='all')assert(rows.every(r=>(filter==='live')!==r.policy.startsWith('fresh-')));
  assert.equal((elements.get('timing-table').innerHTML.match(/<tr>/g)||[]).length,rows.length);
  assert(!elements.get('chart').innerHTML.includes('NaN'));timingStates++;
}
elements.get('reset').listeners.click();
assert.equal(elements.get('subject').value,'robot');assert.equal(elements.get('rows').value,'all');
for(const [c,modes] of Object.entries(data.images))for(const mode of Object.keys(modes)){
  elements.get('case').value=c;elements.get('left-mode').value=mode;
  elements.get('case').listeners.change();assert.equal(elements.get('left-image').src,modes[mode]);imageStates++;
}
for(const subject of ['robot','guardian'])for(let n=0;n<16;n++){
  elements.get('motion-subject').value=subject;elements.get('frame').value=String(n);
  elements.get('frame').listeners.input();assert.equal(elements.get('motion-a').src,data.motion[subject].cached[n]);
  assert.equal(elements.get('motion-b').src,data.motion[subject].source[n]);motionStates++;
}
elements.get('play').listeners.click();assert(tick);tick();assert.equal(elements.get('frame').value,0);
elements.get('play').listeners.click();assert.equal(tick,null);
console.log(JSON.stringify({passed:true,scope:'Static HTML assets and mock DOM interactions; no rendered browser acceptance',
  ids:ids.length,image_assets:assets,timing_states:timingStates,image_states:imageStates,motion_states:motionStates,
  reset_and_play_controls:true},null,2));
