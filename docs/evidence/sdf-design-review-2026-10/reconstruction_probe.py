# /// script
# dependencies = ["numpy==2.5.4", "matplotlib==3.11.2", "pillow==13.0.0"]
# ///
"""Independent reconstruction/undo diagnostics, not Cubey rendering code.

The plotted fields are 2D slices of fields constant in z in the pictured area,
so bilinear reconstruction is identical to trilinear reconstruction there.
Lengths are normalized to the sample spacing h. Actual Cubey h is 1/(256*7).
"""
from pathlib import Path
import hashlib
import json
import math
import platform
import subprocess

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
import PIL

OUT = Path(__file__).resolve().parent
H_WORLD = 1 / (256 * 7)
ANGLE = np.deg2rad(23)
C, S = np.cos(ANGLE), np.sin(ANGLE)

def plane(x, y):
    return C * x + S * y - .37

def box(x, y, half=(11.3, 7.7), center=(.23, .37), angle=ANGLE):
    x, y = x - center[0], y - center[1]
    c, s = np.cos(angle), np.sin(angle)
    a = np.abs(c*x + s*y) - half[0]
    b = np.abs(-s*x + c*y) - half[1]
    return np.hypot(np.maximum(a, 0), np.maximum(b, 0)) + np.minimum(np.maximum(a, b), 0)

def sticks(x, y):
    a = box(x, y, half=(12.3, .75), center=(.23, .37))
    b = box(x, y, half=(10.7, .75), center=(-.31, -.27), angle=-.71)
    return np.minimum(a, b)

def interpolate(function, x, y, half=False, phase=(0., 0.)):
    gx, gy = np.floor(x-phase[0])+phase[0], np.floor(y-phase[1])+phase[1]
    fx, fy = x-gx, y-gy
    values = [function(gx,gy), function(gx+1,gy), function(gx,gy+1), function(gx+1,gy+1)]
    if half:
        values = [(v*H_WORLD).astype(np.float16).astype(np.float64)/H_WORLD for v in values]
    return (values[0]*(1-fx)+values[1]*fx)*(1-fy)+(values[2]*(1-fx)+values[3]*fx)*fy

def quantiles(values):
    return {'median':float(np.median(values)), 'p95':float(np.quantile(values,.95)),
            'max':float(np.max(values))}

def radial_surface_error(function, half=False):
    # Both convex box and union of overlapping long sticks contain the origin.
    angles = np.linspace(0, 2*np.pi, 4096, endpoint=False)
    dx, dy = np.cos(angles), np.sin(angles)
    def roots(f):
        # Reconstructed fields need not stay star-convex. Find the first
        # sampled sign-change bracket instead of bisecting the whole ray.
        low, high = np.zeros_like(dx), np.full_like(dx,20.)
        assert np.all(f(low*dx,low*dy)<0) and np.all(f(high*dx,high*dy)>0)
        found=np.zeros_like(dx,dtype=bool)
        for step in range(1,1281):
            radius=step/64
            new=(~found)&(f(radius*dx,radius*dy)>=0)
            low=np.where(new,radius-1/64,low)
            high=np.where(new,radius,high)
            found|=new
            if np.all(found):break
        assert np.all(found)
        for _ in range(50):
            mid=(low+high)*.5
            inside=f(mid*dx,mid*dy)<0
            low=np.where(inside,mid,low);high=np.where(inside,high,mid)
        return (low+high)*.5
    exact=roots(function)
    sampled=roots(lambda x,y:interpolate(function,x,y,half=half))
    return quantiles(np.abs(sampled-exact))

axis = np.linspace(-16,16,641)
X,Y=np.meshgrid(axis,axis)
results = {
    'scope':'Independent float64 numerical slices and analytic examples. No native hybrid tracer implemented; no GPU timings or native image acceptance.',
    'units':'Distances normalized to sample spacing h; radial zero-crossing errors use rays from origin in a 2D slice, not a 3D Hausdorff metric.',
    'radial_method':'4096 directions, first sign-change bracket from h/64 samples, then 50 bisections. Narrower sign excursions are not ruled out by this diagnostic.',
    'sample_spacing_world':H_WORLD,
    'source_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
    'environment':{'python':platform.python_version(),'numpy':np.__version__,'matplotlib':matplotlib.__version__,'pillow':PIL.__version__},
    'cases':{},
}
for name,function in [('oblique-plane',plane),('rotated-box',box),('crossed-thin-sticks',sticks)]:
    exact=function(X,Y);sampled=interpolate(function,X,Y);r16=interpolate(function,X,Y,half=True)
    row={'field_error_h':quantiles(np.abs(sampled-exact)), 'r16_field_error_h':quantiles(np.abs(r16-exact))}
    if name!='oblique-plane':
        row['radial_zero_error_h']=radial_surface_error(function)
        row['r16_radial_zero_error_h']=radial_surface_error(function,half=True)
    results['cases'][name]=row
assert results['cases']['oblique-plane']['field_error_h']['max']<1e-12

# Samples of a tiny 3D sphere entirely between grid vertices are all positive.
radius=.35
corner_value=math.sqrt(.5**2*3)-radius
results['subgrid_sphere']={
    'center_h':[.5,.5,.5],'radius_h':radius,
    'all_eight_corner_distances_h':corner_value,
    'interpolated_center_distance_h':corner_value,
    'exact_center_distance_h':-radius,
    'trilinear_zero_set_inside_this_cell':False,
    'maximum_lipschitz_interpolation_error_bound_h':math.sqrt(3)/2,
}
assert corner_value>0 and -radius<0

def smooth_min(a,b,k):
    h=max(0,min(1,.5+.5*(b-a)/k))
    return b*(1-h)+a*h-k*h*(1-h)
a,b,c,k=0.,0.,.05,.1
left=smooth_min(smooth_min(a,b,k),c,k)
right=smooth_min(a,smooth_min(b,c,k),k)
results['smooth_associativity']={'a':a,'b':b,'c':c,'k':k,'left_grouped':left,'right_grouped':right,'difference':abs(left-right)}
assert left!=right

# Two different histories collapse to the same visible pruned sphere B.
p=.15
results['undo_compaction']={
    'probe_x':p,'history_1':['sphere A radius .1','union sphere B radius .3'],
    'history_2':['sphere C radius .2','union sphere B radius .3'],
    'visible_value_history_1':min(p-.1,p-.3),
    'visible_value_history_2':min(p-.2,p-.3),
    'value_after_undo_B_history_1':p-.1,
    'value_after_undo_B_history_2':p-.2,
    'finding':'Identical current geometry and a current-only pruned list do not identify the state to recover on undo. Retain authoritative history or historical data.',
}
assert results['undo_compaction']['visible_value_history_1']==results['undo_compaction']['visible_value_history_2']
assert results['undo_compaction']['value_after_undo_B_history_1']!=results['undo_compaction']['value_after_undo_B_history_2']

node_count=19173961
published_bytes=1024*64**3+node_count*4
results['snapshot_accounting']={
    'scope':'Capacity-based calculation for one full published atlas + one node array; excludes scratch, working buffers, sharing, compression and driver overhead.',
    'one_published_state_bytes':published_bytes,
    'sixty_six_unshared_states_bytes':published_bytes*66,
    'current_full_field_resource_bytes':912997552,
}

fig,axes=plt.subplots(1,3,figsize=(13.2,4.6),layout='constrained')
for ax,name,function in zip(axes,['Oblique plane: control','Rotated sharp box: corner','Crossed thin sticks: junction'],[plane,box,sticks]):
    exact=function(X,Y);sampled=interpolate(function,X,Y,half=True)
    ax.contour(X,Y,exact,levels=[0],colors=['#13795b'],linewidths=2)
    ax.contour(X,Y,sampled,levels=[0],colors=['#c94343'],linewidths=1.5,linestyles='--')
    ax.set_title(name,fontsize=11)
    ax.set_aspect('equal');ax.grid(alpha=.22)
    ax.set_xlabel('x / sample spacing');ax.set_ylabel('y / sample spacing')
    if function==plane:ax.set_xlim(-4,4);ax.set_ylim(-4,4)
    elif function==box:
        corner=np.array([[C,-S],[S,C]])@np.array([11.3,7.7])+np.array([.23,.37])
        ax.set_xlim(corner[0]-2.5,corner[0]+2.5);ax.set_ylim(corner[1]-2.5,corner[1]+2.5)
    else:ax.set_xlim(-3,3);ax.set_ylim(-3,3)
from matplotlib.lines import Line2D
fig.legend(handles=[Line2D([0],[0],color='#13795b',lw=2,label='Analytic zero set'),Line2D([0],[0],color='#c94343',lw=1.5,ls='--',label='R16 sampled reconstruction')],loc='outside lower center',ncol=2,frameon=False)
fig.suptitle('Independent reconstruction study — mathematical slices, not native Cubey renders',fontsize=12)
fig.savefig(OUT/'reconstruction.png',dpi=160)
fig.savefig(OUT/'reconstruction.svg')

fig,ax=plt.subplots(figsize=(6.5,4.8),layout='constrained')
t=np.linspace(0,2*np.pi,400)
ax.plot(.5+radius*np.cos(t),.5+radius*np.sin(t),color='#13795b',label='Analytic sphere equator')
ax.scatter([0,0,1,1],[0,1,0,1],c='#c94343',s=55,zorder=3,label='All corner samples positive')
ax.plot([0,1,1,0,0],[0,0,1,1,0],color='#777777',lw=1)
ax.text(.5,.5,'Exact center: -0.350 h\nInterpolated center: +0.516 h',ha='center',va='center',fontsize=10)
ax.set_aspect('equal');ax.set_xlim(-.2,1.2);ax.set_ylim(-.2,1.2)
ax.set_xlabel('x / sample spacing');ax.set_ylabel('y / sample spacing')
ax.set_title('3D sphere between eight grid samples\nEquatorial slice; trilinear cache has no zero crossing')
ax.legend(loc='upper left',fontsize=8,frameon=False)
fig.savefig(OUT/'subgrid-sphere.png',dpi=150)
fig.savefig(OUT/'subgrid-sphere.svg')
results['probe_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
(OUT/'reconstruction.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
