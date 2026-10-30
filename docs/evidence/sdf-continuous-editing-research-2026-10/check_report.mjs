// Static interaction checks with a mock DOM/canvas. This is not browser rendering.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';

const directory = path.resolve(process.argv[2]);
const html = fs.readFileSync(path.join(directory, 'index.html'), 'utf8');
const probe = JSON.parse(fs.readFileSync(path.join(directory, 'probe.json'), 'utf8'));
const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(m => m[1]);
assert.equal(new Set(ids).size, ids.length, 'HTML IDs must be unique');
assert(!html.includes('@@TOKEN'), 'all inline placeholders must resolve');
const links = [...html.matchAll(/href="([^"]+)"/g)].map(m => m[1]);
for (const link of links) {
  if (link.startsWith('#')) assert(ids.includes(link.slice(1)), `anchor ${link}`);
  else if (!link.startsWith('http')) assert(fs.existsSync(path.join(directory, link)), `asset ${link}`);
}
const ctx = new Proxy({
  createImageData(w, h) { return { data: new Uint8ClampedArray(w * h * 4) }; },
}, { get(target, key) { return key in target ? target[key] : () => {}; } });
const elements = new Map(ids.map(id => [id, {
  value: ({ radius: '.35', phase: '.5', arrivals: '120', service: '80' })[id] ?? '',
  textContent: '', width: 40, height: 34, listeners: {},
  getContext() { return ctx; },
  addEventListener(name, callback) { this.listeners[name] = callback; },
}]));
const sandbox = { window: {}, document: { getElementById(id) { assert(elements.has(id), `DOM ${id}`); return elements.get(id); } } };
vm.createContext(sandbox);
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
assert.equal(scripts.length, 1);
vm.runInContext(scripts[0], sandbox, { timeout: 10000 });
const api = sandbox.window.continuousResearch;
const first = api.updateGeometry();
assert.equal(first.changed, true);
assert(Math.abs(first.live - probe.cases.live_to_baked_subgrid_cavity.analytic_center_value_h) < 1e-12);
assert(Math.abs(first.baked - probe.cases.live_to_baked_subgrid_cavity.r16_trilinear_center_value_h) < 1e-12);
elements.get('radius').value = '.8';
elements.get('phase').value = '0';
elements.get('phase').listeners.input();
assert.equal(api.updateGeometry().changed, false);
elements.get('phase').value = '.5';
elements.get('phase').listeners.input();
assert.equal(api.updateGeometry().changed, true, 'grid phase can change sampled absence');
elements.get('radius').value = '1.2';
elements.get('radius').listeners.input();
assert.equal(api.updateGeometry().changed, false);
elements.get('reset-geometry').listeners.click();
assert.equal(api.updateGeometry().changed, true);
assert(elements.get('radius-label').textContent.includes('0.35'));

let samples = 0;
for (const radius of [.1, .35, .8, 1.2]) for (const phase of [0, .3, .5, 1]) {
  const grid = api.sliceGrid(radius, phase);
  for (let i = 0; i < 31; i++) {
    const x = -.99 + i / 31 * 2.97, y = 1.99 - i / 31 * 2.95;
    assert(Math.abs(api.sliceSample(x, y, grid) - api.sampled([x, y, .5], radius, phase)) < 1e-12);
    samples++;
  }
}
assert.equal(api.queueState(120, 80).backlog, 200);
assert.equal(api.queueState(120, 80).secondsToCap, 1.6);
elements.get('arrivals').value = '80'; elements.get('service').value = '120';
elements.get('service').listeners.input();
assert.equal(api.updateQueue().backlog, 0);
assert(elements.get('queue-output').textContent.includes('No average backlog growth'));
elements.get('service').value = '0'; elements.get('arrivals').value = '240';
elements.get('arrivals').listeners.input();
assert.equal(api.updateQueue().backlog, 1200);
assert.equal(elements.get('radius-label').textContent, '0.35 h');
const result = {
  scope: 'Static HTML/link checks and mock DOM/canvas interaction; no rendered browser acceptance',
  passed: true, ids: ids.length, hrefs: links.length,
  independent_python_center_match: true, grid_phase_and_radius_controls: true,
  reset_control: true, slice_optimization_equivalence_samples: samples,
  queue_controls_and_capacity: true,
};
console.log(JSON.stringify(result, null, 2));
