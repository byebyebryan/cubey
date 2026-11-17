# /// script
# dependencies = ["numpy==2.4.6", "matplotlib==3.10.8"]
# ///
"""Bundle original render bytes and standalone matplotlib figures."""
import hashlib
import json
import shutil
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

evidence=Path(__file__).resolve().parent
root=Path('outputs/raytracing/dragon-glass-speckle-20261116')
out=root/'report'
(out/'images').mkdir(parents=True,exist_ok=True)
data=json.loads((evidence/'analysis.json').read_text())
ordinary=json.loads((evidence/'captures.json').read_text())
broad=json.loads((evidence/'broad-sun.json').read_text())
captures={}
for light,records in [(None,ordinary['captures']),('broad',broad['captures'])]:
    for c in records:
        key=f"{light or c['lighting']}-{c['samples']}-{c['seed']}"
        image=Path(c['prefix']+'.png')
        target=out/'images'/(key+'.png')
        shutil.copyfile(image,target)
        sha=hashlib.sha256(target.read_bytes()).hexdigest()
        assert sha==c['receipt']['artifacts']['.png']
        captures[key]=dict(image='images/'+target.name,sha256=sha,samples=c['samples'],seed=c['seed'],flags=c['receipt']['status_or_pixels'])

plt.rcParams.update({'font.size':11,'axes.facecolor':'#15232a','figure.facecolor':'#15232a','text.color':'#e7eff1','axes.labelcolor':'#c3d4db','xtick.color':'#b8cbd3','ytick.color':'#b8cbd3','axes.edgecolor':'#526974','grid.color':'#405760','svg.hashsalt':'cubey-speckles','svg.fonttype':'none'})
fig,axes=plt.subplots(1,2,figsize=(10,3.4),layout='constrained')
samples=[128,512,4096,8192]
for ax,key,title,color in [(axes[0],'seed_rgb_hdr_rmse','HDR seed difference','#8edce8'),(axes[1],'seed_rgb8_rmse','Displayed seed difference','#efbd7b')]:
    values=[data['metrics']['head']['combined-'+str(n)][key] for n in samples]
    ax.plot(samples,values,'o-',color=color,lw=2)
    ax.set_xscale('log',base=2)
    ax.set_xticks(samples,[f'{s:,}' for s in samples])
    ax.set_xlabel('Samples per pixel')
    ax.set_ylabel('RMS difference · '+('linear RGB' if key=='seed_rgb_hdr_rmse' else 'RGB8 codes'))
    ax.set_title(title,loc='left')
    ax.grid(alpha=.4)
    ax.set_ylim(bottom=0)
fig.savefig(out/'convergence.svg',metadata={'Date':None})
plt.close(fig)
fig,ax=plt.subplots(figsize=(10,3.6),layout='constrained')
for p,color,label in zip(data['probes'],['#ff9944','#9ba9b2','#62efbd'],['Orange pixel · seed 19','Orange pixel · seed 43','Green pixel · seed 19']):
    curve=p['cumulative_luminance']
    ax.plot([v[0] for v in curve],[v[1] for v in curve],label=label,color=color,lw=1.6)
ax.set_yscale('log')
ax.set_xlim(1,4096)
ax.set_ylim(.1,20000)
ax.set_xlabel('Completed primary samples')
ax.set_ylabel('Running mean HDR luminance · log scale')
ax.set_title('A few solar paths dominate the accumulated pixel',loc='left')
ax.grid(alpha=.35)
ax.legend(facecolor='#15232a',edgecolor='#526974',labelcolor='#e7eff1',fontsize=9,loc='upper right')
fig.savefig(out/'sample-series.svg',metadata={'Date':None})
plt.close(fig)
for name in ('analysis.json','captures.json','broad-sun.json'):
    shutil.copyfile(evidence/name,out/name)
html=(evidence/'report-template.html').read_text().replace('__DATA__',json.dumps(data,separators=(',',':'))).replace('__CAPTURES__',json.dumps(captures,separators=(',',':')))
(out/'index.html').write_text(html)
print(out.resolve())
print('original images',len(captures),'total bytes',sum(p.stat().st_size for p in out.rglob('*') if p.is_file()))
