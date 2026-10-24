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
