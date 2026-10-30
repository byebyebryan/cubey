import fs from "node:fs";import vm from "node:vm";import path from "node:path";
const root=path.resolve(process.argv[2]),html=fs.readFileSync(path.join(root,"index.html"),"utf8"),elements=new Map();
for(const m of html.matchAll(/id="([^"]+)"/g)){const id=m[1];elements.set(id,{value:({scene:"guardian",view:"preset",ss:"2",mode:"balanced",split:"50"})[id]??"",innerHTML:"",textContent:"",style:{},events:{},addEventListener(k,f){this.events[k]=f},getAttribute(k){return this[k]??null},load(){}})}
elements.get("motion").src="clips/guardian.mp4";
const context=vm.createContext({document:{getElementById(id){if(!elements.has(id))throw Error("Missing DOM id "+id);return elements.get(id)}},console});
vm.runInContext(html.match(/<script>([\s\S]*)<\/script>/)[1],context);
const exists=p=>{if(!fs.existsSync(path.join(root,p)))throw Error("Missing asset "+p)};let selections=0;
for(const scene of ["guardian","robot"])for(const view of ["preset","close"])for(const ss of ["1","2"])for(const mode of ["cached","cached-refined","balanced","analytic","hybrid"]){
for(const [id,value] of Object.entries({scene,view,ss,mode}))elements.get(id).value=value;
vm.runInContext("update()",context);for(const id of ["candidate","reference","sampling-image","motion"])exists(elements.get(id).src);
for(const id of ["bars","view-data","command"])if(/NaN|undefined/.test(elements.get(id).innerHTML+elements.get(id).textContent))throw Error("Invalid content "+id);selections++;
}
elements.get("split").value="25";elements.get("split").events.input();if(elements.get("divider").style.left!=="25%")throw Error("Slider failed");
for(const m of html.split("<script>")[0].matchAll(/(?:src|href)="([^"]+)"/g)){const p=m[1];if(!p.startsWith("http")&&p!=="#")exists(p)}
console.log(JSON.stringify({offline_js_passed:true,mode_scene_view_sample_selections:selections,slider_passed:true,local_assets_present:true,browser_render_acceptance:false}));
