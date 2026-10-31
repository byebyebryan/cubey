// Static data/assets and interactive DOM contracts; visual browser QA is separate.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const root=path.resolve(process.argv[2]),html=fs.readFileSync(path.join(root,'index.html'),'utf8');
const data=JSON.parse(fs.readFileSync(path.join(root,'data.json'),'utf8'));
const ids=[...html.matchAll(/\bid="([^"]+)"/g)].map(x=>x[1]);
assert.equal(ids.length,new Set(ids).size);assert(!html.includes('__REPORT_DATA__'));
for(const [,href]of html.matchAll(/href="([^"]+)"/g)){
 if(href.startsWith('#'))assert(ids.includes(href.slice(1)));
 else if(!href.startsWith('http'))assert(fs.existsSync(path.join(root,href)));
}
const defaults={'commit-subject':'guardian','commit-operation':'add',radius:'0.065',full:'off',
 'preview-subject':'guardian','preview-operation':'add','preview-view':'preset',
 'image-subject':'guardian','image-operation':'add','image-view':'preset','image-left':'preview',divider:'50',
 'sequence-subject':'guardian','sequence-operation':'add','budget-select':'2',groups:'32'};
const elements=new Map(ids.map(id=>[id,{value:defaults[id]||'',textContent:'',innerHTML:'',src:'',style:{},listeners:{},
 addEventListener(event,fn){this.listeners[event]=fn;}}]));
elements.get('report-data').textContent=JSON.stringify(data);
const sandbox={document:{getElementById(id){assert(elements.has(id),id);return elements.get(id);}}};
vm.createContext(sandbox);const scripts=[...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(x=>x[1]);
assert.equal(scripts.length,1);vm.runInContext(scripts[0],sandbox,{timeout:10000});
function set(id,value,event='change'){const e=elements.get(id);e.value=value;e.listeners[event]();}
function rows(id,n){const h=elements.get(id).innerHTML;assert.equal((h.match(/<tr>/g)||[]).length,n);assert(!h.includes('NaN'));}
let commitStates=0,renderStates=0,imageStates=0,budgetStates=0,sequenceStates=0;
for(const s of ['guardian','robot'])for(const op of ['add','cut'])for(const radius of ['0.02','0.065','0.15'])for(const full of ['off','on']){
 set('commit-subject',s);set('commit-operation',op);set('radius',radius);set('full',full);rows('commit-table',4);
 assert.equal((elements.get('commit-bars').innerHTML.match(/class="barrow"/g)||[]).length,full==='on'?4:3);commitStates++;
}
for(const s of ['guardian','robot'])for(const op of ['add','cut'])for(const view of ['preset','close']){
 set('preview-subject',s);set('preview-operation',op);set('preview-view',view);rows('render-table',5);renderStates++;
 for(const left of ['base','preview']){
  set('image-subject',s);set('image-operation',op);set('image-view',view);set('image-left',left);
  for(const id of ['pending-image','baked-image'])assert(fs.existsSync(path.join(root,elements.get(id).src)));
  assert(elements.get('image-caption').textContent.includes(s));imageStates++;
 }
}
for(const v of ['0','37','100']){set('divider',v,'input');assert.equal(elements.get('image-divider').style.left,v+'%');assert.equal(elements.get('pending-image').style.clipPath,`inset(0 ${100-Number(v)}% 0 0)`);}
for(const s of ['guardian','robot'])for(const op of ['add','cut']){
 set('sequence-subject',s);set('sequence-operation',op);
 assert.equal((elements.get('sequence-images').innerHTML.match(/<figure>/g)||[]).length,4);
 for(const row of data.prefix_images.filter(x=>x.subject===s&&x.operation===op))assert(fs.existsSync(path.join(root,row.src)));
 sequenceStates++;
}
for(const b of ['0.5','1','2'])for(const groups of ['0','32','128','512','2048']){
 set('budget-select',b);set('groups',groups);rows('budget-table',4);budgetStates++;
}
assert(data.qualification.all_exact);assert.equal(data.qualification.routes.local,39);
assert.equal(data.qualification.routes.dominance,39);assert.equal(data.qualification.routes.local_chains,32);
assert.equal(data.qualification.routes.dominance_chains,32);assert.equal(data.qualification.prefix_sequences,28);
assert.equal(data.qualification.routes.calibration,60);assert.equal(data.commits.length,48);assert.equal(data.render.length,48);
assert.equal(data.qualification.retained_consumer_replays,4);
assert(data.budgets.find(x=>x.subject==='guardian'&&x.operation==='cut'&&x.budget_ms===2).gpu_median_ms>8);
assert(data.requested_memory.combined_bytes<2*1024**3);assert(data.preservation.public_executable_byte_identical);
console.log(JSON.stringify({passed:true,scope:'Static data/assets and mock-DOM contracts; browser QA separate',
 ids:ids.length,commitStates,renderStates,imageStates,budgetStates,sequenceStates,dividerStates:3},null,2));
