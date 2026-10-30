#!/usr/bin/env python3
"""Native-validated geometry and image comparisons. Uses numpy/scipy/Pillow."""
import argparse
import json
from pathlib import Path
import struct

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

H=1/(256*7)


def geometry(path):
    raw=path.read_bytes();w,h,stride,count=struct.unpack_from('<4I',raw)
    if stride!=1 or count!=w*h or len(raw)!=16+count*8 or len(raw)+count*8>32*1024**2:
        raise RuntimeError('geometry dimensions/layout/storage budget mismatch')
    words=np.frombuffer(raw,dtype='<u4',offset=16).reshape(h,w,2)
    depth=words[:,:,0].copy().view('<f4')
    if not np.isfinite(depth).all() or np.any((depth<0)&(depth!=-1)):
        raise RuntimeError('nonfinite, exhausted or unwritten geometry sample')
    bits=words[:,:,1]
    xy=np.stack([(bits&65535).astype(np.uint16).view(np.int16),
                 (bits>>16).astype(np.uint16).view(np.int16)],axis=-1).astype(np.float64)/32767
    xy=np.maximum(xy,-1)
    z=1-np.abs(xy).sum(axis=-1)
    normal=np.concatenate([xy,z[:,:,None]],axis=-1)
    lower=z<0
    normal[lower,:2]=(1-np.abs(xy[lower,::-1]))*np.where(xy[lower]<0,-1,1)
    normal/=np.maximum(np.linalg.norm(normal,axis=-1,keepdims=True),1e-20)
    return depth,normal


def quantiles(a):
    a=np.asarray(a)
    if not a.size:return dict(count=0)
    q=np.percentile(a,[50,95,99,100])
    return dict(count=int(a.size),mean=float(a.mean()),p50=float(q[0]),p95=float(q[1]),
                p99=float(q[2]),max=float(q[3]))


def compare(reference,candidate,out):
    a,an=geometry(reference.with_suffix('.bin'));b,bn=geometry(candidate.with_suffix('.bin'))
    valid=np.ones(a.shape,dtype=bool)
    for path in (reference,candidate):
        if path.with_suffix('.valid.png').exists():valid&=np.array(Image.open(path.with_suffix('.valid.png')))>0
    am=a>=0;bm=b>=0;both=am&bm&valid
    ae=am&~ndimage.binary_erosion(am);be=bm&~ndimage.binary_erosion(bm)
    edge_valid=~ndimage.binary_dilation(~valid,iterations=2)
    distances=np.concatenate([ndimage.distance_transform_edt(~be)[ae&edge_valid],
                              ndimage.distance_transform_edt(~ae)[be&edge_valid]])
    error=np.abs(a[both]-b[both])
    footprint=np.maximum(a[both]*2*np.tan(np.pi/8)/a.shape[0],1e-12)
    compatible=both&(np.abs(a-b)<=2*H)
    angle=np.degrees(np.arccos(np.clip(np.sum(an[compatible]*bn[compatible],axis=-1),-1,1)))
    ia=np.asarray(Image.open(reference.with_suffix('.png')).convert('RGB')).astype(np.int16)
    ib=np.asarray(Image.open(candidate.with_suffix('.png')).convert('RGB')).astype(np.int16)
    diff=np.max(np.abs(ia-ib),axis=-1)
    result=dict(reference=str(reference),candidate=str(candidate),pixels=int(a.size),
        reference_hits=int(am.sum()),candidate_hits=int(bm.sum()),
        unscored_pixels=int((~valid).sum()),
        false_foreground=int((bm&~am&valid).sum()),false_background=int((am&~bm&valid).sum()),
        silhouette_boundary_pixels=quantiles(distances),
        silhouette_over_1px=int((distances>1).sum()),depth_world=quantiles(error),
        depth_in_pixel_footprints=quantiles(error/footprint),
        compatible_normal_degrees=quantiles(angle),incompatible_hit_pairs=int((both&~compatible).sum()),
        normal_compatibility_world_limit=2*H,
        image=dict(changed_pixels=int((diff>0).sum()),over_8=int((diff>8).sum()),
                   over_32=int((diff>32).sum()),max_channel=int(diff.max()),mean_abs_channel=float(np.abs(ia-ib).mean())))
    if out:
        overlay=np.zeros((*a.shape,3),np.uint8)
        overlay[am&bm]=[48,48,48];overlay[bm&~am]=[255,80,0];overlay[am&~bm]=[0,180,255]
        Image.fromarray(overlay).save(out.with_suffix('.mask.png'))
        rgb=np.zeros((*a.shape,3),np.uint8);rgb[:,:,0]=np.clip(diff*4,0,255);rgb[:,:,1]=np.clip(diff*2,0,255)
        Image.fromarray(rgb).save(out.with_suffix('.difference.png'))
        heat=np.zeros((*a.shape,3),np.uint8)
        heat[compatible,0]=np.clip(angle*12,0,255).astype(np.uint8)
        heat[compatible,1]=np.clip(angle*4,0,255).astype(np.uint8)
        Image.fromarray(heat).save(out.with_suffix('.normal-error.png'))
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--captures',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--routes',nargs='+',default=['sampled'])
    args=p.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    source=json.loads((args.captures/'captures.json').read_text())
    if not source.get('complete'):raise RuntimeError('quality analysis requires completed captures')
    results=[]
    for row in source['runs']:
        if row['route'] not in args.routes:continue
        name=f"{row['scene']}-{row['view']}"
        ref=args.captures/(name+'-source');candidate=args.captures/(name+'-'+row['route'])
        result=compare(ref,candidate,args.output/(name+'-'+row['route']))
        result.update(scene=row['scene'],view=row['view'],route=row['route']);results.append(result)
        print(name,row['route'],'silhouette',result['silhouette_boundary_pixels'],
              'normal p95',result['compatible_normal_degrees'].get('p95'),flush=True)
    (args.output/'quality.json').write_text(json.dumps(dict(source=source,comparisons=results),indent=2)+'\n')


if __name__=='__main__':main()
