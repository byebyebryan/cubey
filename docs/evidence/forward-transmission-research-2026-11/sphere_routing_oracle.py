"""Double-precision analytic research control; does not execute GPU shaders."""
import json, math
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parent

def unit(v):
    return v / np.linalg.norm(v, axis=-1, keepdims=True)

def refract(i, n, eta):
    c = np.sum(i*n, axis=-1, keepdims=True)
    k = 1-eta*eta*(1-c*c)
    assert np.all(k > -1e-10)
    return unit(eta*i - (eta*c + np.sqrt(np.maximum(k, 0)))*n)

def rays(center, radius, ior, distance=0.855, extent=(1920,1080)):
    width,height=extent
    camera=np.array([0.,0.,distance]); center=np.array(center)
    tan=math.tan(math.radians(45)/2)
    x=(2*(np.arange(width)+.5)/width-1)*tan*width/height
    y=(1-2*(np.arange(height)+.5)/height)*tan
    xx,yy=np.meshgrid(x,y)
    incident=unit(np.stack((xx,yy,-np.ones_like(xx)),axis=-1))
    oc=camera-center
    b=np.sum(incident*oc,axis=-1)
    c=np.sum(oc*oc)-radius*radius
    disc=b*b-c
    hit=disc>1e-12
    incident=incident[hit]; b=b[hit]; disc=disc[hit]
    point=camera+incident*(-b-np.sqrt(disc))[:,None]
    normal=unit(point-center)
    internal=refract(incident,normal,1/ior)
    nr=np.sum(normal*internal,axis=-1,keepdims=True)
    helper_length=2*radius*(-nr)
    helper_exit=point+helper_length*internal
    helper_normal=unit(nr*internal-.5*normal)
    helper_direction=refract(internal,helper_normal,ior)
    # Independent quadratic sphere exit, then outward-normal Snell tangent form.
    relative=point-center
    qb=2*np.sum(relative*internal,axis=-1,keepdims=True)
    qc=np.sum(relative*relative,axis=-1,keepdims=True)-radius*radius
    oracle_length=(-qb+np.sqrt(qb*qb-4*qc))/2
    oracle_exit=point+oracle_length*internal
    outward=unit(oracle_exit-center)
    tangent=ior*(internal-np.sum(internal*outward,axis=-1,keepdims=True)*outward)
    kt=1-np.sum(tangent*tangent,axis=-1,keepdims=True)
    assert np.all(kt>-1e-10)
    oracle_direction=unit(tangent+outward*np.sqrt(np.maximum(kt,0)))
    screen_direction=unit(helper_exit-camera)
    uv=(helper_exit-camera)[:,:2]/(-(helper_exit-camera)[:,2:3])
    uv=uv/np.array([tan*width/height,tan])*.5+.5
    edge=np.maximum(2/np.array(extent),.025)
    def smooth(t):
        t=np.clip(t,0,1);return t*t*(3-2*t)
    sw=np.prod(smooth(uv/edge)*(1-smooth((uv-(1-edge))/edge)),axis=-1)
    difference=np.degrees(np.arccos(np.clip(np.sum(screen_direction*oracle_direction,axis=-1),-1,1)))
    stats={
      'pixel_count':int(len(incident)),
      'helper_max_exit_position_error_m':float(np.max(np.linalg.norm(helper_exit-oracle_exit,axis=-1))),
      'helper_max_path_length_error_m':float(np.max(abs(helper_length-oracle_length))),
      'helper_max_direction_vector_error':float(np.max(np.linalg.norm(helper_direction-oracle_direction,axis=-1))),
      'screen_vs_exit_angle_degrees':{'mean':float(np.mean(difference)),'p50':float(np.percentile(difference,50)),'p95':float(np.percentile(difference,95)),'max':float(np.max(difference))},
      'projected_exit_full_screen_weight_fraction':float(np.mean(sw==1)),
      'opposite_vertical_hemisphere_fraction':float(np.mean(screen_direction[:,1]*oracle_direction[:,1]<-1e-12)),
      'minimum_exit_snell_discriminant':float(np.min(kt))
    }
    return stats,(hit,screen_direction[:,1],oracle_direction[:,1],difference)

if __name__=='__main__':
    cases={}
    for name,center,radius,distance in [('gallery-glass',(.126,0,0),.075,.855),('gallery-frosted-position',(.378,-.195,0),.075,.855),('isolated-sphere',(0,0,0),.25,1.05)]:
        for ior in [1.,1.33,1.5,2.5]:
            stats,arrays=rays(center,radius,ior,distance)
            cases[name+'-ior-'+str(ior)]={'center_m':center,'radius_m':radius,'camera_distance_m':distance,'ior':ior,**stats}
            if name=='isolated-sphere' and ior==1.5: plot_arrays=arrays
    result={'schema':'cubey.forward_transmission_research.v1','source_commit':'87b7f61560205de9e1f1b061024234e12ac357ba',
      'kind':'CPU ideal-sphere geometry and direction-routing control; not a GPU render or radiance validation',
      'assumptions':['1920x1080 pixel centers, 45 degree vertical field of view, camera looking along -Z','Smooth sphere normals, diameter-matched thickness, unit model scale','One entry and one exit only; no Fresnel weights, roughness, radiance, internal reflection chains or other bodies','Frosted-position control measures only smooth geometry at that location; not rough-glass transport','Screen sky direction reconstructed from projected exit and camera; UV vertical sign does not alter world direction'],
      'cases':cases}
    for key,case in cases.items():
        assert case['helper_max_exit_position_error_m'] < 1e-9, key
        assert case['helper_max_path_length_error_m'] < 1e-9, key
        assert case['helper_max_direction_vector_error'] < 1e-8, key
        if case['ior'] == 1.:
            assert case['screen_vs_exit_angle_degrees']['max'] < 2e-6, key
    (ROOT/'sphere-routing.json').write_text(json.dumps(result,indent=2)+'\n')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    mask,screen_y,exit_y,angle=plot_arrays
    fig,axes=plt.subplots(1,3,figsize=(12,4),layout='constrained')
    for ax,v,title,cmap,limits in zip(axes,[screen_y,exit_y,angle],['Sky direction from projected camera pixel','Sky direction after sphere entry + exit','Angular difference (degrees)'],['coolwarm','coolwarm','magma'],[(-1,1),(-1,1),(0,90)]):
        im=np.full(mask.shape,np.nan);im[mask]=v
        ax.imshow(im,cmap=cmap,vmin=limits[0],vmax=limits[1]);ax.set_title(title,fontsize=10);ax.axis('off')
        fig.colorbar(ax.images[0],ax=ax,fraction=.035,pad=.02)
    fig.suptitle('Analytic direction control: smooth sphere, IOR 1.5, diameter thickness\nFirst two panels show world Y of sky direction; no rendered radiance or Fresnel weighting',fontsize=11)
    fig.savefig(ROOT/'sphere-directions.png',dpi=150)
    print(json.dumps({k:v for k,v in cases.items() if k.endswith('1.5')},indent=2))
