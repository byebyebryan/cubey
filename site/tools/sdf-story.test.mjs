import assert from 'node:assert/strict';
import test from 'node:test';
import {fieldValue,fieldContour,traceSteps,traceCaption,TRACE} from '../public/scripts/sdf-story.js';

test('joining extends the solid and subtraction opens an interior boundary',()=>{
  assert.ok(fieldValue(.5,0,'circle')>0);
  assert.ok(fieldValue(.5,0,'join')<0);
  assert.ok(fieldValue(.22,.04,'join')<0);
  assert.ok(fieldValue(.22,.04,'cut')>0);
  assert.ok(Math.abs(fieldValue(.22,.27,'cut'))<1e-12);
  assert.ok(fieldValue(-.4,0,'cut')<0);
  assert.ok(fieldValue(1,0,'cut')>0);
});

test('moving the subtractor removes the new region and restores the old one',()=>{
  assert.ok(fieldValue(-.4,0,'cut',-.4)>0);
  assert.ok(fieldValue(.22,0,'cut',-.4)<0);
  assert.ok(fieldValue(-.4,0,'cut',.22)<0);
});

test('sampled contours stay finite and close to the zero boundary across the controls',()=>{
  for(const mode of ['circle','join','cut'])for(const cutter of [-.65,-.4,0,.22,.5,.85]){
    const segments=fieldContour(mode,cutter);
    assert.ok(segments.length>70);
    for(const segment of segments)for(const [x,y] of segment){
      assert.ok(Number.isFinite(x)&&Number.isFinite(y));
      assert.ok(Math.abs(fieldValue(x,y,mode,cutter))<.012,'linear contour stays within the sampling error');
    }
  }
});

test('outside rays advance monotonically and converge near the analytic entry point',()=>{
  for(const y of [-.45,-.25,0,.25,.45]){
    const steps=traceSteps(y);
    assert.equal(steps.at(-1).status,'hit');
    const analytic=TRACE.cx-Math.sqrt(TRACE.radius**2-y*y);
    assert.ok(Math.abs(steps.at(-1).x-analytic)<.015);
    for(let i=0;i<steps.length;i++){
      const p=steps[i];
      assert.ok(Number.isFinite(p.distance)&&p.distance>=-1e-12);
      if(i)assert.ok(p.x>steps[i-1].x);
      assert.ok(p.x<=analytic+1e-12,'exact steps do not cross the surface');
    }
    assert.match(traceCaption(y,steps.length-1),/ray stops/);
  }
});

test('rays above and below the circle leave the illustration rather than report a hit',()=>{
  for(const y of [-.9,-.65,.65,.9]){
    const steps=traceSteps(y);
    assert.equal(steps.at(-1).status,'miss');
    assert.equal(steps.at(-1).x,TRACE.end);
    assert.match(traceCaption(y,steps.length-1),/without hitting/);
  }
});

test('every allowed ray height has bounded finite work, including grazing rays',()=>{
  for(let i=0;i<=72;i++){
    const height=-.9+i*.025,steps=traceSteps(height);
    assert.ok(steps.length<=24);
    assert.ok(['hit','miss','limit'].includes(steps.at(-1).status));
    assert.ok(steps.every(p=>Number.isFinite(p.x)&&Number.isFinite(p.distance)));
  }
});
