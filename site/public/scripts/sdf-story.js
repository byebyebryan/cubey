// Shared by the static figures, their browser controls and the model tests.
// These are deliberately small analytic 2D illustrations, not Cubey's renderer.
const FIELD = {width:640,height:400,xMin:-1.25,xMax:1.25,yMin:-.78125,yMax:.78125};
const px = x => (x-FIELD.xMin)/(FIELD.xMax-FIELD.xMin)*FIELD.width;
const py = y => (FIELD.yMax-y)/(FIELD.yMax-FIELD.yMin)*FIELD.height;
const n = value => Number(value.toFixed(2));

export function fieldValue(x,y,mode='cut',cutter=.22){
  const circle=Math.hypot(x+.18,y)-.53;
  if(mode==='circle')return circle;
  const qx=Math.abs(x-.28)-.35,qy=Math.abs(y)-.36;
  const box=Math.hypot(Math.max(qx,0),Math.max(qy,0))+Math.min(Math.max(qx,qy),0);
  const joined=Math.min(circle,box);
  return mode==='join'?joined:Math.max(joined,-(Math.hypot(x-cutter,y-.04)-.23));
}

// Marching squares gives the illustration a sampled, approximate zero contour.
export function fieldContour(mode='cut',cutter=.22){
  const cols=100,rows=64,segments=[];
  for(let j=0;j<rows;j++)for(let i=0;i<cols;i++){
    const x=FIELD.xMin+i/cols*(FIELD.xMax-FIELD.xMin),y=FIELD.yMin+j/rows*(FIELD.yMax-FIELD.yMin);
    const dx=(FIELD.xMax-FIELD.xMin)/cols,dy=(FIELD.yMax-FIELD.yMin)/rows;
    const points=[[x,y],[x+dx,y],[x+dx,y+dy],[x,y+dy]];
    const values=points.map(([a,b])=>fieldValue(a,b,mode,cutter));
    const crossings=[];
    for(let e=0;e<4;e++){
      const next=(e+1)%4;
      if((values[e]<0)===(values[next]<0))continue;
      const t=values[e]/(values[e]-values[next]);
      crossings.push([points[e][0]+t*(points[next][0]-points[e][0]),points[e][1]+t*(points[next][1]-points[e][1])]);
    }
    for(let k=0;k+1<crossings.length;k+=2)segments.push([crossings[k],crossings[k+1]]);
  }
  return segments;
}

export function renderField(mode='cut',cutter=.22){
  const colors=['#6c956d','#57785a','#3f5945','#2e4136','#1b2b2b','#213839','#29474c','#31545c','#365d69'];
  const paths=colors.map(()=>'');
  for(let j=0;j<25;j++)for(let i=0;i<40;i++){
    const x=FIELD.xMin+(i+.5)/40*(FIELD.xMax-FIELD.xMin),y=FIELD.yMax-(j+.5)/25*(FIELD.yMax-FIELD.yMin);
    const d=fieldValue(x,y,mode,cutter);
    const band=d<0?Math.max(0,3-Math.floor(-d/.09)):Math.min(8,4+Math.floor(d/.12));
    paths[band]+=`M${i*16},${j*16}h16v16h-16z`;
  }
  const tiles=paths.map((d,i)=>`<path fill="${colors[i]}" d="${d}"/>`).join('');
  const contour=fieldContour(mode,cutter).map(([[x1,y1],[x2,y2]])=>`M${n(px(x1))},${n(py(y1))}L${n(px(x2))},${n(py(y2))}`).join('');
  const guide=mode==='cut'?`<circle cx="${n(px(cutter))}" cy="${n(py(.04))}" r="${n(.23*256)}" fill="none" stroke="#e7a184" stroke-width="2" stroke-dasharray="6 5"/>`:'';
  return tiles+`<path d="${contour}" fill="none" stroke="#f1e6b3" stroke-width="3"/>`+guide;
}

export const TRACE={origin:-1.45,end:1.1,cx:.35,radius:.5,epsilon:.006};
export function traceSteps(height=.25){
  let x=TRACE.origin;
  const steps=[];
  for(let i=0;i<24;i++){
    const distance=Math.hypot(x-TRACE.cx,height)-TRACE.radius;
    const status=distance<=TRACE.epsilon?'hit':x>=TRACE.end?'miss':'advance';
    steps.push({x,y:height,distance,status});
    if(status!=='advance')return steps;
    x=Math.min(TRACE.end,x+distance);
  }
  steps[steps.length-1].status='limit';
  return steps;
}
const tx=x=>(x+1.8)/3*640;
const ty=y=>210-y*640/3;
export function renderTrace(height=.25,index=0){
  const steps=traceSteps(height),current=steps[Math.min(index,steps.length-1)];
  const radius=Math.max(0,current.distance)*640/3;
  const visited=steps.slice(0,index+1);
  return `<line x1="${n(tx(TRACE.origin))}" y1="${n(ty(height))}" x2="${n(tx(TRACE.end))}" y2="${n(ty(height))}" stroke="#607e78" stroke-width="2" stroke-dasharray="5 5"/>`
    +`<circle cx="${n(tx(TRACE.cx))}" cy="${ty(0)}" r="${n(TRACE.radius*640/3)}" fill="#38543f" stroke="#f1e6b3" stroke-width="3"/>`
    +`<circle cx="${n(tx(current.x))}" cy="${n(ty(height))}" r="${n(radius)}" fill="#b8cf8b08" stroke="#b8cf8b" stroke-width="1.5"/>`
    +`<line x1="${n(tx(TRACE.origin))}" y1="${n(ty(height))}" x2="${n(tx(current.x))}" y2="${n(ty(height))}" stroke="#e7a184" stroke-width="3"/>`
    +visited.map((p,i)=>`<circle cx="${n(tx(p.x))}" cy="${n(ty(height))}" r="${i===visited.length-1?6:3}" fill="${i===visited.length-1?'#f1e6b3':'#e7a184'}" stroke="#111715" stroke-width="1.5"/>`).join('');
}

export function traceCaption(height,index){
  const steps=traceSteps(height),i=Math.min(index,steps.length-1),p=steps[i];
  if(p.status==='hit')return `Sample ${i+1}: close enough to the surface. The ray stops here.`;
  if(p.status==='miss')return `Sample ${i+1}: the ray leaves this view without hitting the circle.`;
  if(p.status==='limit')return `Sample ${i+1}: the illustration’s step limit is reached.`;
  return `Sample ${i+1}: ${p.distance.toFixed(2)} units to the surface. Advance by this distance.`;
}

export function initSdfStory(root=document){
  const field=root.querySelector('[data-field-figure]');
  if(field){
    let mode='cut';
    const slider=field.querySelector('input'),buttons=[...field.querySelectorAll('[data-field-mode]')];
    const captions={circle:'One circle. Negative values are inside; zero marks the boundary.',join:'The circle and rectangle become one solid. Follow their combined outer boundary.',cut:'Remove the circular region. Its boundary becomes a new inside wall.'};
    const update=()=>{
      field.querySelector('[data-field-drawing]').innerHTML=renderField(mode,Number(slider.value));
      buttons.forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.fieldMode===mode)));
      slider.disabled=mode!=='cut';
      field.querySelector('[data-field-caption]').textContent=captions[mode];
    };
    buttons.forEach(b=>b.addEventListener('click',()=>{mode=b.dataset.fieldMode;update();}));
    slider.addEventListener('input',update);
    field.querySelector('[data-figure-controls]').hidden=false;
  }
  const ray=root.querySelector('[data-trace-figure]');
  if(ray){
    let index=0;
    const slider=ray.querySelector('input'),next=ray.querySelector('[data-trace-next]');
    const update=()=>{
      const height=Number(slider.value),steps=traceSteps(height);
      index=Math.min(index,steps.length-1);
      ray.querySelector('[data-trace-drawing]').innerHTML=renderTrace(height,index);
      ray.querySelector('[data-trace-caption]').textContent=traceCaption(height,index);
      next.disabled=index===steps.length-1;
    };
    slider.addEventListener('input',()=>{index=0;update();});
    next.addEventListener('click',()=>{index++;update();});
    ray.querySelector('[data-trace-reset]').addEventListener('click',()=>{index=0;update();});
    ray.querySelector('[data-figure-controls]').hidden=false;
  }
}
if(typeof document!=='undefined')initSdfStory();
