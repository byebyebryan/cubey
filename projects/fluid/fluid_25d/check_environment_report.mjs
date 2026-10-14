#!/usr/bin/env node
// Owned headless Chrome/CDP only; never attaches to the user's browser/display.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {once} from 'node:events';
const args=Object.fromEntries(process.argv.slice(2).reduce((a,v,i,all)=>i%2===0?[...a,[v.replace(/^--/,''),all[i+1]]]:a,[]));
assert(args.chrome&&args.report&&args.out,'--chrome BINARY --report URL --out DIRECTORY required');
const scratch=await mkdtemp(join(tmpdir(),'cubey-environment-report-'));
const env={...process.env};delete env.DISPLAY;delete env.WAYLAND_DISPLAY;
const child=spawn('rtk',['proxy',args.chrome,'--headless','--no-sandbox','--disable-gpu','--remote-debugging-port=0','--user-data-dir='+scratch,'about:blank'],{detached:true,env,stdio:['ignore','pipe','pipe']});
let diagnostic='',socket;
const deadline=setTimeout(()=>{try{process.kill(-child.pid,'SIGKILL');}catch{}},60000);
try {
  const endpoint=await new Promise((accept,reject)=>{
    const collect=data=>{diagnostic+=data.toString();const m=diagnostic.match(/DevTools listening on (ws:\/\/\S+)/);if(m)accept(m[1]);};
    child.stderr.on('data',collect);child.stdout.on('data',collect);child.once('error',reject);child.once('exit',code=>reject(new Error('Chrome exited: '+code+'\n'+diagnostic)));
  });
  socket=new WebSocket(endpoint);await once(socket,'open');
  let next=0;const waiting=new Map();
  socket.addEventListener('message',event=>{const r=JSON.parse(event.data),s=waiting.get(r.id);if(!s)return;waiting.delete(r.id);r.error?s.reject(new Error(JSON.stringify(r.error))):s.accept(r.result);});
  socket.addEventListener('close',()=>{for(const s of waiting.values())s.reject(new Error('CDP closed'));waiting.clear();});
  const call=(method,params={},sessionId)=>new Promise((accept,reject)=>{const id=++next;waiting.set(id,{accept,reject});socket.send(JSON.stringify({id,method,params,...(sessionId?{sessionId}:{})}));});
  const {targetId}=await call('Target.createTarget',{url:'about:blank'});
  const {sessionId}=await call('Target.attachToTarget',{targetId,flatten:true});
  const page=(method,params)=>call(method,params,sessionId);
  const evaluate=async expression=>{const r=await page('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw new Error(JSON.stringify(r.exceptionDetails));return r.result.value;};
  const until=async expression=>{for(let n=0;n<200;n++){if(await evaluate(expression))return;await new Promise(r=>setTimeout(r,50));}throw new Error('Timed out: '+expression);};
  await page('Page.enable');
  await page('Emulation.setDeviceMetricsOverride',{width:1280,height:900,deviceScaleFactor:1,mobile:false});
  await page('Page.navigate',{url:args.report});
  await until('document.readyState==="complete"&&!document.getElementById("view").disabled&&document.getElementById("scene").naturalWidth===1280');
  let selections=0;
  for(const view of ['collection','runoff'])for(const time of ['daylight','noon','sunset','night'])for(const weather of ['clear','weather']){
    await evaluate(`for(const [id,value] of Object.entries(${JSON.stringify({view,time,weather})})){document.getElementById(id).value=value;document.getElementById(id).dispatchEvent(new Event('change'));}`);
    await until(`document.getElementById('scene').src.endsWith('${view}-${time}-${weather}.png')&&document.getElementById('scene').complete&&document.getElementById('scene').naturalWidth===1280`);
    assert.match(await evaluate('document.getElementById("caption").textContent'),/not a running solver/);selections++;
  }
  for(const component of ['reference','clear','background','reflection','shadow','weather']){
    await evaluate(`document.getElementById('component').value='${component}';document.getElementById('component').dispatchEvent(new Event('change'))`);
    await until(`document.getElementById('ablation').src.endsWith('collection-daylight-${component}.png')&&document.getElementById('ablation').complete&&document.getElementById('ablation').naturalWidth===1280`);
  }
  const clips=[];
  for(const [id,duration] of [['cycle',32],['wind',6]]){
    await evaluate(`document.getElementById('${id}').closest('details')?.setAttribute('open','');document.getElementById('${id}').play()`);
    await until(`document.getElementById('${id}').readyState>=2&&document.getElementById('${id}').currentTime>.25`);
    const r=await evaluate(`({id:'${id}',duration:document.getElementById('${id}').duration,width:document.getElementById('${id}').videoWidth,height:document.getElementById('${id}').videoHeight})`);
    assert(Math.abs(r.duration-duration)<.1);assert.equal(r.width,1280);assert.equal(r.height,720);clips.push(r);
    await evaluate(`document.getElementById('${id}').pause()`);
  }
  await evaluate('for(const [id,value] of Object.entries({view:"collection",time:"daylight",weather:"clear"})){document.getElementById(id).value=value;document.getElementById(id).dispatchEvent(new Event("change"));}');
  await until('document.getElementById("scene").complete&&document.getElementById("scene").naturalWidth===1280');
  const links=await evaluate('Promise.all(Array.from(document.querySelectorAll("a[href],img[src],video[src]")).map(e=>e.href||e.src).filter(u=>new URL(u).origin===location.origin).map(async url=>({url,status:(await fetch(url,{method:"HEAD"})).status})))');
  assert(links.every(r=>r.status===200),JSON.stringify(links.filter(r=>r.status!==200)));
  const screenshot=async name=>{const r=await page('Page.captureScreenshot',{format:'png'});await writeFile(join(resolve(args.out),name),Buffer.from(r.data,'base64'),{flag:'wx'});};
  await screenshot('report-desktop.png');
  await page('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
  assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth'),true);await screenshot('report-mobile.png');
  await page('Emulation.setScriptExecutionDisabled',{value:true});await page('Page.reload');
  await until('document.readyState==="complete"&&document.getElementById("scene").naturalWidth===1280');
  assert.equal(await evaluate('["view","time","weather","component"].every(id=>document.getElementById(id).disabled)'),true);
  const receipt={report:args.report,selections,components:6,clips,links,checks:['held-field labels','390px width','no-script still and videos'],headless_only:true,native_gui:'not tested',snap_reachability:'not tested',browser:(await call('Browser.getVersion')).product};
  await writeFile(join(resolve(args.out),'browser-check.json'),JSON.stringify(receipt,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(receipt,null,2));
  await call('Browser.close');
} finally {
  clearTimeout(deadline);socket?.close();
  const done=!child.pid||child.exitCode!==null||child.signalCode!==null?Promise.resolve():once(child,'exit');
  try{process.kill(-child.pid,'SIGTERM');}catch{}
  const kill=setTimeout(()=>{try{process.kill(-child.pid,'SIGKILL');}catch{}},3000);await done;clearTimeout(kill);
  await rm(scratch,{recursive:true});
}
