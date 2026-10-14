#!/usr/bin/env node
// Private headless browser only: never attaches to the user's desktop/browser.
// Uses the installed browser and Node's built-in CDP/WebSocket, no downloads.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { once } from 'node:events';

const args = Object.fromEntries(process.argv.slice(2).reduce((a, v, i, all) =>
  i % 2 === 0 ? [...a, [v.replace(/^--/, ''), all[i + 1]]] : a, []));
assert(args.chrome && args.report && args.out, '--chrome BINARY --report URL --out DIRECTORY required');
const scratch = await mkdtemp(join(tmpdir(), 'cubey-wide-view-browser-'));
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
    const result = JSON.parse(event.data);
    const slot = waiting.get(result.id);
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
  await until('document.readyState === "complete" && typeof update === "function" && document.getElementById("left").naturalWidth === 1280 && document.getElementById("right").naturalWidth === 1280');
  assert.equal(await evaluate('document.getElementById("view").value'), 'wide64');
  assert.equal(await evaluate('document.getElementById("treatment").value'), 'default');
  assert.equal(await evaluate('document.getElementById("filter").checked'), true);
  await evaluate('document.getElementById("filter").click()');
  await until('document.getElementById("right").src.endsWith("wide64-default.png") && document.getElementById("right").complete');
  await evaluate('document.getElementById("treatment").value="clay-no-shadow"; document.getElementById("treatment").dispatchEvent(new Event("change"))');
  await evaluate('document.getElementById("filter").click()');
  await until('document.getElementById("right").src.endsWith("wide64-clay-no-shadow-filtered.png") && document.getElementById("right").complete');
  await evaluate('document.getElementById("treatment").value="uniform-clay"; document.getElementById("treatment").dispatchEvent(new Event("change"))');
  assert.equal(await evaluate('document.getElementById("filter").disabled'), true);
  assert.equal(await evaluate('document.getElementById("filter").checked'), false);
  await evaluate('document.getElementById("view").value="boundary32"; document.getElementById("view").dispatchEvent(new Event("change"))');
  await until('document.getElementById("right").src.endsWith("boundary32-default.png") && document.getElementById("right").complete');
  assert.match(await evaluate('document.getElementById("caption").textContent'), /straight water cutoff/);
  await evaluate('document.getElementById("filter").click(); document.getElementById("flip").click()');
  await until('document.getElementById("left").src.endsWith("boundary32-default-filtered.png") && document.getElementById("left").complete');
  await evaluate('document.getElementById("flip").click(); document.getElementById("view").value="wide64"; document.getElementById("view").dispatchEvent(new Event("change")); document.getElementById("treatment").value="clay-no-shadow"; document.getElementById("treatment").dispatchEvent(new Event("change"))');
  await until('document.getElementById("right").src.endsWith("wide64-clay-no-shadow-filtered.png") && document.getElementById("right").complete');
  const links = await evaluate('Promise.all(Array.from(document.querySelectorAll("a[href],img[src]")).map(e=>e.href||e.src).filter(u=>new URL(u).origin===location.origin).map(async url=>({url,status:(await fetch(url,{method:"HEAD"})).status})))');
  assert(links.every(row => row.status === 200), JSON.stringify(links.filter(row => row.status !== 200)));
  const assets = await evaluate('Promise.all(Object.entries(data.matrix).flatMap(([v,items])=>items.map(t=>v+"-"+t+".png")).concat(data.trial.map(s=>s.replace("/","-")+"-filtered.png")).map(async path=>({path,status:(await fetch(path,{method:"HEAD"})).status})))');
  assert(assets.every(row => row.status === 200));
  const desktop = await page('Page.captureScreenshot', { format: 'png' });
  await writeFile(join(resolve(args.out), 'report-desktop.png'), Buffer.from(desktop.data, 'base64'), { flag: 'wx' });
  await page('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
  assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true);
  const mobile = await page('Page.captureScreenshot', { format: 'png' });
  await writeFile(join(resolve(args.out), 'report-mobile.png'), Buffer.from(mobile.data, 'base64'), { flag: 'wx' });
  await page('Emulation.setScriptExecutionDisabled', { value: true });
  await page('Page.reload');
  await until('document.readyState === "complete" && document.getElementById("left").naturalWidth === 1280');
  assert.equal(await evaluate('document.getElementById("view").disabled'), true);
  assert.equal(await evaluate('document.getElementById("filter").disabled'), true);
  const receipt = { report: args.report, headless_only: true, controls: ['view/treatment selection',
    'trial lighting toggle', 'unsupported trial disabled', 'finite-boundary caption', 'swap sides',
    'relative evidence links', 'all matrix/trial images', '390px width', 'no-script fallback'],
    links, assets, native_gui_acceptance: 'not tested', browser: (await call('Browser.getVersion')).product };
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
