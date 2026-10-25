const pauseFilms = (except) => {
  for (const video of document.querySelectorAll('video')) if (video !== except) video.pause();
};
for (const video of document.querySelectorAll('video')) {
  const button=video.parentElement.querySelector('[data-play]');
  if (button) {
    button.hidden=false;
    button.addEventListener('click', async () => {
      pauseFilms(video);
      try { await video.play(); button.hidden=true; }
      catch { button.textContent='Play film'; }
    });
    video.addEventListener('play',()=>{pauseFilms(video);button.hidden=true;});
    video.addEventListener('ended',()=>{button.hidden=false;button.textContent='Watch again';});
  } else video.addEventListener('play',()=>pauseFilms(video));
}
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

for (const library of document.querySelectorAll('[data-material-library]')) {
  const controls=library.querySelector('[data-library-controls]');
  const buttons=[...library.querySelectorAll('[data-renderer]')];
  const phaseButtons=[...library.querySelectorAll('[data-phase]')];
  const caption=library.querySelector('[data-renderer-caption]');
  const video=library.querySelector('[data-library-film]');
  const source=video.querySelector('source');
  const play=video.parentElement.querySelector('[data-play]');
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
      play.hidden=false;play.textContent='Watch film';
      resume=async()=>{
        video.currentTime=Math.min(time,Math.max(0,video.duration-.04));
        if(playing){try{await video.play();}catch{play.hidden=false;}}
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
