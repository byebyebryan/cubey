#!/usr/bin/env node
// Private headless CDP check; never attaches to a user's browser or display.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { once } from 'node:events';

const args = Object.fromEntries(process.argv.slice(2).reduce((a, v, i, all) =>
  i % 2 === 0 ? [...a, [v.replace(/^--/, ''), all[i + 1]]] : a, []));
assert(args.chrome && args.report && args.out, '--chrome BINARY --report URL --out DIRECTORY required');
const scratch = await mkdtemp(join(tmpdir(), 'cubey-patch-report-browser-'));
const env = { ...process.env };
delete env.DISPLAY; delete env.WAYLAND_DISPLAY;
const child = spawn('rtk', ['proxy', args.chrome, '--headless', '--no-sandbox', '--disable-gpu',
  '--remote-debugging-port=0', '--user-data-dir=' + scratch, 'about:blank'],
  { detached: true, env, stdio: ['ignore', 'pipe', 'pipe'] });
let diagnostic = '', socket;
const deadline = setTimeout(() => { try { process.kill(-child.pid, 'SIGKILL'); } catch {} }, 60000);
try {
  const endpoint = await new Promise((accept, reject) => {
    const collect = data => {
      diagnostic += data.toString();
      const match = diagnostic.match(/DevTools listening on (ws:\/\/\S+)/);
      if (match) accept(match[1]);
    };
    child.stderr.on('data', collect); child.stdout.on('data', collect);
    child.once('error', reject);
    child.once('exit', code => reject(new Error('Browser exited before CDP: ' + code + '\n' + diagnostic)));
  });
  socket = new WebSocket(endpoint);
  await once(socket, 'open');
  let next = 0;
  const waiting = new Map();
  socket.addEventListener('message', event => {
    const result = JSON.parse(event.data), slot = waiting.get(result.id);
    if (!slot) return;
    waiting.delete(result.id);
    if (result.error) slot.reject(new Error(JSON.stringify(result.error)));
    else slot.accept(result.result);
  });
  socket.addEventListener('close', () => {
    for (const slot of waiting.values()) slot.reject(new Error('Browser connection closed'));
    waiting.clear();
  });
  const call = (method, params = {}, sessionId) => new Promise((accept, reject) => {
    const id = ++next;
    waiting.set(id, { accept, reject });
    socket.send(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }));
  });
  const { targetId } = await call('Target.createTarget', { url: 'about:blank' });
  const { sessionId } = await call('Target.attachToTarget', { targetId, flatten: true });
  const page = (method, params) => call(method, params, sessionId);
  const evaluate = async expression => {
    const result = await page('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  };
  const until = async expression => {
    for (let attempt = 0; attempt < 200; attempt++) {
      if (await evaluate(expression)) return;
      await new Promise(accept => setTimeout(accept, 50));
    }
    throw new Error('Browser condition timed out: ' + expression);
  };
  await page('Page.enable');
  await page('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  await page('Page.navigate', { url: args.report });
  await until('document.readyState === "complete" && !document.getElementById("view").disabled && document.getElementById("scene").naturalWidth === 1280');
  assert.equal(await evaluate('document.getElementById("view").value'), 'runoff');
  const clips = [];
  for (const camera of ['overview', 'runoff', 'collection']) {
    await evaluate(`document.getElementById('view').value=${JSON.stringify(camera)}; document.getElementById('view').dispatchEvent(new Event('change'))`);
    await until(`document.getElementById('scene').src.endsWith('final-world1k-${camera}.png') && document.getElementById('scene').complete && document.getElementById('scene').naturalWidth === 1280`);
    await evaluate('document.getElementById("motion").click()');
    await until(`document.getElementById('clip').src.endsWith('motion-${camera}.mp4') && document.getElementById('clip').readyState >= 2 && document.getElementById('clip').currentTime > 0.25`);
    assert.match(await evaluate('document.getElementById("caption").textContent'), /not a running solver/);
    const clip = await evaluate('({duration:document.getElementById("clip").duration,width:document.getElementById("clip").videoWidth,height:document.getElementById("clip").videoHeight,hidden:document.getElementById("clip").hidden,still_hidden:document.getElementById("image-link").hidden})');
    assert(Math.abs(clip.duration - 6) < 0.1);
    assert.equal(clip.width, 1280); assert.equal(clip.height, 720);
    assert.equal(clip.hidden, false); assert.equal(clip.still_hidden, true);
    clips.push({ camera, ...clip });
    await evaluate('document.getElementById("motion").click()');
    assert.equal(await evaluate('document.getElementById("clip").hidden && document.getElementById("clip").paused'), true);
  }
  await evaluate('document.getElementById("view").value="runoff";document.getElementById("view").dispatchEvent(new Event("change"))');
  await until('document.getElementById("scene").complete && document.getElementById("scene").naturalWidth === 1280');
  const links = await evaluate('Promise.all(Array.from(document.querySelectorAll("a[href],img[src],video[src]")).map(e=>e.href||e.src).filter(u=>new URL(u).origin===location.origin).map(async url=>({url,status:(await fetch(url,{method:"HEAD"})).status})))');
  assert(links.every(row => row.status === 200), JSON.stringify(links.filter(row => row.status !== 200)));
  const desktop = await page('Page.captureScreenshot', { format: 'png' });
  await writeFile(join(resolve(args.out), 'report-desktop.png'), Buffer.from(desktop.data, 'base64'), { flag: 'wx' });
  await page('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
  assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true);
  const mobile = await page('Page.captureScreenshot', { format: 'png' });
  await writeFile(join(resolve(args.out), 'report-mobile.png'), Buffer.from(mobile.data, 'base64'), { flag: 'wx' });
  await page('Emulation.setScriptExecutionDisabled', { value: true });
  await page('Page.reload');
  await until('document.readyState === "complete" && document.getElementById("scene").naturalWidth === 1280');
  assert.equal(await evaluate('document.getElementById("view").disabled && document.getElementById("motion").disabled'), true);
  assert.equal(await evaluate('document.getElementById("scene").hidden'), false);
  const receipt = { report: args.report, headless_only: true, clips, links,
    checks: ['three view selections', 'three decoded playing 6s clips', 'held-field labels',
      'return to still/pause', 'evidence links', '390px width', 'no-script still fallback'],
    native_gui_acceptance: 'not tested', snap_reachability: 'not tested',
    browser: (await call('Browser.getVersion')).product };
  await writeFile(join(resolve(args.out), 'browser-check.json'), JSON.stringify(receipt, null, 2) + '\n', { flag: 'wx' });
  console.log(JSON.stringify(receipt, null, 2));
  await call('Browser.close');
} finally {
  clearTimeout(deadline);
  socket?.close();
  const done = !child.pid || child.exitCode !== null || child.signalCode !== null ? Promise.resolve() : once(child, 'exit');
  try { process.kill(-child.pid, 'SIGTERM'); } catch {}
  const kill = setTimeout(() => { try { process.kill(-child.pid, 'SIGKILL'); } catch {} }, 3000);
  await done;
  clearTimeout(kill);
  await rm(scratch, { recursive: true });
}
