import fs from "node:fs";
import vm from "node:vm";
import path from "node:path";
const root=path.resolve(process.argv[2]);
const html=fs.readFileSync(path.join(root,"index.html"),"utf8");
const script=html.match(/<script>([\s\S]*)<\/script>/)[1];
const elements=new Map();
for(const match of html.matchAll(/id="([^"]+)"/g)){
 const id=match[1], attrs={};
 elements.set(id,{id,value:({scene:"guardian",view:"preset",policy:"combined",movement:"arc",fixture:"subgrid-sphere",width:"2",phase:"0",split:"50"})[id]??"",innerHTML:"",textContent:"",style:{},events:{},complete:true,naturalWidth:1920,
 addEventListener(k,fn){this.events[k]=fn},getAttribute(k){return this[k]??attrs[k]??null},
 insertAdjacentHTML(_,v){this.innerHTML+=v},getContext(){return {clearRect(){},drawImage(){}}},
 getBoundingClientRect(){return {left:0,top:0,width:960,height:540}},load(){}});
}
elements.get("motion").src="clips/guardian-arc.mp4";
const context=vm.createContext({document:{getElementById(id){if(!elements.has(id))throw Error("Missing DOM id "+id);return elements.get(id)}},console});
vm.runInContext(script,context);
const exists=p=>{if(!fs.existsSync(path.join(root,p)))throw Error("Missing asset "+p)};
let views=0,rois=0;
for(const s of ["guardian","robot"])for(const v of ["preset","side","rear","close","distant"])for(const r of ["sampled","refine03125","normal","combined"]){
 elements.get("scene").value=s;elements.get("view").value=v;elements.get("policy").value=r;vm.runInContext("update()",context);
 for(const id of ["candidate","reference","normal-map","mask-map"])exists(elements.get(id).src);
 for(const id of ["metrics","frontier"])if(/NaN|undefined/.test(elements.get(id).innerHTML))throw Error("Invalid content "+id);
 views++;
}
for(const f of ["subgrid-sphere","thin-stick-0.5h"])for(const w of ["0.5","1","2","4"])for(const p of ["0","0.5"]){
 elements.get("fixture").value=f;elements.get("width").value=w;elements.get("phase").value=p;vm.runInContext("roi()",context);
 for(const m of elements.get("roi").innerHTML.matchAll(/src="([^"]+)"/g))exists(m[1]);rois++;
}
for(const s of ["guardian","robot"])for(const m of ["arc","dolly"]){elements.get("scene").value=s;elements.get("movement").value=m;vm.runInContext("motion()",context);exists(elements.get("motion").src)}
elements.get("split").value="25";elements.get("split").events.input();if(elements.get("divider").style.left!=="25%")throw Error("Slider failed");
elements.get("comparison").events.click({clientX:480,clientY:270});
for(const m of html.split('<script>')[0].matchAll(/(?:src|href)="([^"]+)"/g)){const p=m[1];if(!p.startsWith("http")&&p!=="#")exists(p)}
console.log(JSON.stringify({offline_js_passed:true,policy_views:views,fixture_selections:rois,motion_sources:4,slider_and_crop_handlers:true,browser_render_acceptance:false}));
