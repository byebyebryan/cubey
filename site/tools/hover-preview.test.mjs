import {readFileSync} from 'node:fs';
import {runInNewContext} from 'node:vm';
import assert from 'node:assert/strict';
import test from 'node:test';

// Exercise the delivered script with a deterministic clock and media events.
// Browser QA separately checks real decoding, layout and pointer interaction.
const script=readFileSync(new URL('../public/scripts/media.js',import.meta.url),'utf8');
function setup({hover=true,saveData=false,delayed=false}={}){
  class Element{
    hidden=false;listeners=new Map();classes=new Set();
    querySelectorAll;
    classList={add:n=>this.classes.add(n),remove:n=>this.classes.delete(n)};
    addEventListener(name,fn){this.listeners.set(name,[...(this.listeners.get(name)||[]),fn]);}
    fire(name,event={}){for(const fn of this.listeners.get(name)||[])fn(event);}
    querySelector(selector){return this.children[selector];}
    closest(){return this.article;}
  }
  class Video extends Element{
    paused=true;ended=false;readyState=0;currentTime=0;loads=0;plays=0;
    pause(){this.paused=true;}
    load(){this.loads++;this.readyState=1;}
    focus(){}
    play(){
      this.paused=false;this.plays++;this.fire('play');
      return this.delayed?new Promise(resolve=>{this.resolve=resolve;}):Promise.resolve();
    }
  }
  const tiles=Array.from({length:2},()=>{
    const element=new Element(),shell=new Element(),trigger=new Element();
    const clip=new Video(),film=new Video(),figure=new Element(),article=new Element();
    const source={dataset:{src:'preview.mp4'},hasAttribute(){return !!this.src;}};
    clip.delayed=delayed;clip.children={source};figure.children={video:film};
    element.children={'[data-preview-shell]':shell,'[data-preview-trigger]':trigger,
      '[data-hover-clip]':clip,':scope > .scene-media':figure};
    element.article=article;
    return {element,shell,trigger,clip,film,figure,article,source};
  });
  const document=new Element(),window=new Element(),media=new Element(),connection=new Element();
  document.hidden=false;media.matches=hover;connection.saveData=saveData;
  document.querySelectorAll=selector=>selector==='[data-hover-preview]'?tiles.map(t=>t.element):
    selector==='video:not([data-hover-clip])'?tiles.map(t=>t.film):[];
  let now=0,next=0,visibility;
  const timers=new Map();
  runInNewContext(script,{document,window,navigator:{connection},matchMedia:()=>media,
    IntersectionObserver:class{constructor(fn){visibility=fn;}observe(){}},
    setTimeout:(fn,ms)=>{const id=++next;timers.set(id,{fn,at:now+ms});return id;},
    clearTimeout:id=>timers.delete(id)});
  const visible=(tile,value)=>visibility([{target:tile.element,isIntersecting:value}]);
  for(const tile of tiles)visible(tile,true);
  const flush=async()=>{await Promise.resolve();await Promise.resolve();};
  return {tiles,media,connection,document,visible,flush,
    enter:tile=>tile.article.fire('pointerenter',{pointerType:'mouse'}),
    leave:tile=>tile.article.fire('pointerleave'),
    async tick(ms){now+=ms;for(const [id,timer] of timers)if(timer.at<=now){timers.delete(id);timer.fn();}await flush();}};
}

test('initial load and incidental hover do not request preview media',async()=>{
  const h=setup(),t=h.tiles[0];
  assert.equal(t.source.src,undefined);assert.equal(t.clip.loads,0);
  h.enter(t);await h.tick(349);h.leave(t);await h.tick(1000);
  assert.equal(t.source.src,undefined);assert.equal(t.clip.plays,0);
});
test('hover is silent, exclusive, cached and restored to the poster on leave',async()=>{
  const h=setup(),[a,b]=h.tiles;
  h.enter(a);await h.tick(350);
  assert.equal(a.clip.muted,true);assert.equal(a.clip.paused,false);
  assert.equal(a.element.classes.has('is-previewing'),true);
  h.enter(b);await h.tick(350);
  assert.equal(a.clip.paused,true);assert.equal(a.clip.hidden,true);
  assert.equal(b.clip.paused,false);
  h.leave(b);assert.equal(b.clip.hidden,true);
  h.enter(a);await h.tick(350);assert.equal(a.clip.loads,1);
  h.document.fire('keydown',{key:'Escape'});assert.equal(a.clip.paused,true);
});
test('click activates native playback permanently and blocks other hover previews',async()=>{
  const h=setup(),[a,b]=h.tiles;
  h.enter(a);await h.tick(350);a.trigger.fire('click');
  assert.equal(a.figure.hidden,false);assert.equal(a.shell.hidden,true);
  assert.equal(a.film.paused,false);assert.equal(a.clip.paused,true);
  h.leave(a);h.enter(b);await h.tick(1000);
  assert.equal(a.film.paused,false);assert.equal(b.clip.plays,0);
});
test('non-hover/reduced-motion capability and data saving retain native controls',()=>{
  for(const options of [{hover:false},{saveData:true}]){
    const h=setup(options);
    for(const t of h.tiles){assert.equal(t.shell.hidden,true);assert.equal(t.figure.hidden,false);assert.equal(t.clip.loads,0);}
  }
});
test('leaving, scrolling away, hiding the page and policy changes cancel pending playback',async()=>{
  const h=setup({delayed:true}),t=h.tiles[0];
  h.enter(t);await h.tick(350);h.leave(t);t.clip.resolve();await h.flush();
  assert.equal(t.clip.hidden,true);assert.equal(t.element.classes.has('is-previewing'),false);
  h.enter(t);await h.tick(350);h.visible(t,false);t.clip.resolve();await h.flush();
  assert.equal(t.clip.paused,true);assert.equal(t.clip.hidden,true);
  h.visible(t,true);h.enter(t);await h.tick(350);
  h.document.hidden=true;h.document.fire('visibilitychange');t.clip.resolve();await h.flush();
  assert.equal(t.clip.paused,true);
  h.connection.saveData=true;h.connection.fire('change');
  assert.equal(t.figure.hidden,false);assert.equal(t.shell.hidden,true);
});
