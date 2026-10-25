const previewControllers=[];
const films=()=>[...document.querySelectorAll('video:not([data-hover-clip])')];
const stopPreviews=()=>{for(const controller of previewControllers)controller.stop();};
const pauseFilms = (except) => {
  stopPreviews();
  for (const video of films()) if (video !== except) video.pause();
};
for (const video of films())video.addEventListener('play',()=>pauseFilms(video));

const hoverCapability=matchMedia('(hover: hover) and (pointer: fine) and (prefers-reduced-motion: no-preference)');
const canPreview=()=>hoverCapability.matches&&!navigator.connection?.saveData;
const manualPlayback=()=>films().some(v=>!v.paused&&!v.ended);
const previewVisibility=new IntersectionObserver(entries=>{
  for(const entry of entries)
    previewControllers.find(c=>c.element===entry.target)?.setVisible(entry.isIntersecting);
});
for(const element of document.querySelectorAll('[data-hover-preview]')){
  const shell=element.querySelector('[data-preview-shell]');
  const trigger=element.querySelector('[data-preview-trigger]');
  const clip=element.querySelector('[data-hover-clip]');
  const source=clip.querySelector('source');
  const figure=element.querySelector(':scope > .scene-media');
  const film=figure.querySelector('video');
  const hoverTarget=element.closest('article')||element;
  let timer,hovered=false,visible=false,activated=false,generation=0;
  const stop=()=>{
    clearTimeout(timer);
    generation++;
    clip.pause();
    clip.hidden=true;
    element.classList.remove('is-previewing');
  };
  const refresh=()=>{
    stop();
    const previewMode=canPreview()&&!activated;
    shell.hidden=!previewMode;
    figure.hidden=previewMode;
  };
  const leave=()=>{hovered=false;stop();};
  const start=async()=>{
    if(!hovered||!visible||activated||!canPreview()||manualPlayback()||document.hidden)return;
    stopPreviews();
    const ticket=++generation;
    if(!source.hasAttribute('src')){
      source.src=source.dataset.src;
      clip.load();
    }
    if(clip.readyState>0)clip.currentTime=0;
    clip.muted=true;
    clip.hidden=false;
    try{
      await clip.play();
      if(ticket===generation)element.classList.add('is-previewing');
    }catch{if(ticket===generation)stop();}
  };
  hoverTarget.addEventListener('pointerenter',event=>{
    if(event.pointerType==='touch'||activated||!canPreview())return;
    hovered=true;
    clearTimeout(timer);
    timer=setTimeout(start,350);
  });
  hoverTarget.addEventListener('pointerleave',leave);
  trigger.addEventListener('click',()=>{
    activated=true;
    refresh();
    film.focus({preventScroll:true});
    film.play().catch(()=>{}); // Native controls remain available if play is declined.
  });
  film.addEventListener('play',()=>{activated=true;refresh();});
  previewControllers.push({element,stop,refresh,setVisible:value=>{visible=value;if(!visible)leave();}});
  previewVisibility.observe(element);
  refresh();
}
hoverCapability.addEventListener('change',()=>{for(const c of previewControllers)c.refresh();});
navigator.connection?.addEventListener?.('change',()=>{for(const c of previewControllers)c.refresh();});
for (const group of document.querySelectorAll('[data-comparison]')) {
  const image=group.querySelector('[data-comparison-image]');
  const caption=group.querySelector('[data-comparison-caption]');
  const buttons=[...group.querySelectorAll('[data-view]')];
  const choose=(button,save=true)=>{
    pauseFilms();
    for (const b of buttons) b.setAttribute('aria-pressed',String(b===button));
    image.src=button.dataset.image;
    image.removeAttribute('srcset');
    image.alt=button.dataset.caption;
    caption.textContent=button.dataset.caption;
    if (save) {
      const url=new URL(location.href);
      url.searchParams.set('view',button.dataset.view);
      if(group.id)url.hash=group.id;
      history.replaceState(null,'',url);
    }
  };
  for(const button of buttons)button.addEventListener('click',()=>choose(button));
  const saved=new URLSearchParams(location.search).get('view');
  const initial=buttons.find(b=>b.dataset.view===saved);
  if(initial)choose(initial,false);
}
window.addEventListener('pagehide',()=>pauseFilms());
document.addEventListener('visibilitychange',()=>{if(document.hidden)pauseFilms();});
document.addEventListener('keydown',event=>{if(event.key==='Escape'||event.key==='Tab')stopPreviews();});

for (const library of document.querySelectorAll('[data-material-library]')) {
  const controls=library.querySelector('[data-library-controls]');
  const buttons=[...library.querySelectorAll('[data-renderer]')];
  const phaseButtons=[...library.querySelectorAll('[data-phase]')];
  const caption=library.querySelector('[data-renderer-caption]');
  const video=library.querySelector('[data-library-film]');
  const source=video.querySelector('source');
  const variants=[...library.querySelectorAll('[data-material-variant]')];
  let renderer='mesh',phase='dusk',resume;
  const choose=(nextRenderer,nextPhase,save=true)=>{
    const variant=variants.find(v=>v.dataset.rendererKey===nextRenderer&&v.dataset.phaseKey===nextPhase);
    if(!variant)return;
    const time=nextPhase===phase&&!video.ended?video.currentTime:0;
    const playing=!video.paused&&!video.ended;
    renderer=nextRenderer;phase=nextPhase;
    const label=renderer==='sdf'?'SDF ray marching':'Forward PBR';
    for(const button of buttons)button.setAttribute('aria-pressed',String(button.dataset.renderer===renderer));
    for(const button of phaseButtons)button.setAttribute('aria-pressed',String(button.dataset.phase===phase));
    if(source.getAttribute('src')!==variant.getAttribute('href')){
      pauseFilms();
      if(resume)video.removeEventListener('loadedmetadata',resume);
      source.src=variant.getAttribute('href');
      video.poster=variant.dataset.poster;
      video.setAttribute('aria-label',`Twelve material spheres · ${label} · ${phase}`);
      library.querySelector('[data-library-download]').href=variant.href;
      resume=async()=>{
        video.currentTime=Math.min(time,Math.max(0,video.duration-.04));
        if(playing){try{await video.play();}catch{}}
      };
      video.addEventListener('loadedmetadata',resume,{once:true});
      video.load();
    }
    for(const image of library.querySelectorAll('[data-material-image]')){
      image.src=image.dataset[renderer];
      image.alt=`${image.dataset.label} · ${label} specimen`;
      const link=image.closest('[data-material-link]');
      link.href=link.dataset[renderer];
      link.setAttribute('aria-label',`Open ${image.dataset.label} · ${label}`);
    }
    caption.textContent=`${label} · ${phase} · native recorded playback`;
    if(save){const url=new URL(location.href);url.searchParams.set('renderer',renderer);url.searchParams.set('light',phase);url.hash=library.id;history.replaceState(null,'',url);}
  };
  controls.hidden=false;
  for(const button of buttons)button.addEventListener('click',()=>choose(button.dataset.renderer,phase));
  for(const button of phaseButtons)button.addEventListener('click',()=>choose(renderer,button.dataset.phase));
  const saved=new URLSearchParams(location.search);
  choose(saved.get('renderer')==='sdf'?'sdf':'mesh',saved.get('light')==='dawn'?'dawn':'dusk',false);
}
