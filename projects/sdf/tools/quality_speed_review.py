#!/usr/bin/env python3
"""Calibrated opaque-mask pose comparisons and independent fixture coverage."""
import argparse
import json
from pathlib import Path
import struct

import numpy as np
from PIL import Image
from scipy import ndimage
from quality_speed_analysis import geometry, quantiles


def rgb(path):return np.array(Image.open(path).convert('RGB'),dtype=np.int16)
def opaque(image,sky):return np.max(np.abs(image-sky),axis=-1)>4
def depth_only(path):
    raw=path.read_bytes();w,h,stride,count=struct.unpack_from('<4I',raw)
    if stride!=1 or count!=w*h or len(raw)!=16+count*8:raise RuntimeError('pose geometry layout mismatch')
    depth=np.frombuffer(raw,'<f4',offset=16).reshape(h,w,2)[:,:,0].copy()
    if not np.isfinite(depth).all() or np.any((depth<0)&(depth!=-1)):raise RuntimeError('invalid pose geometry')
    return depth


def review(root,output):
    captures=json.loads((root/'final-captures/captures.json').read_text())
    backgrounds=json.loads((root/'backgrounds/backgrounds.json').read_text())
    sequences=json.loads((root/'sequences/sequences.json').read_text())
    if not all(j['complete'] for j in (captures,backgrounds,sequences)):raise RuntimeError('incomplete review inputs')
    calibration=[]
    for row in captures['runs']:
        bg=next(r for r in backgrounds['runs'] if r['scene']==row['scene'] and r['movement']=='calibration')
        index=['preset','side','rear','close','distant'].index(row['view'])
        sky=rgb(bg['frames'][index]['frame_png'])
        prefix=Path(row['command'][row['command'].index('--output')+1]).with_suffix('')
        depth,_=geometry(prefix.with_suffix('.bin'));truth=depth>=0
        valid=np.array(Image.open(prefix.with_suffix('.valid.png')))>0
        mask=opaque(rgb(prefix.with_suffix('.png')),sky)
        errors=int(((mask!=truth)&valid).sum())
        calibration.append(dict(scene=row['scene'],view=row['view'],route=row['route'],
            scored_pixels=int(valid.sum()),classification_errors=errors))
    qualified=not any(r['classification_errors'] for r in calibration)
    results=[]
    for scene in ('guardian','robot'):
        for movement in ('arc','dolly'):
            ref=next(r for r in sequences['runs'] if (r['scene'],r['movement'],r['route'])==(scene,movement,'source'))
            bg=next(r for r in backgrounds['runs'] if (r['scene'],r['movement'])==(scene,movement))
            for route in ('sampled','refine03125','combined'):
                candidate=next(r for r in sequences['runs'] if (r['scene'],r['movement'],r['route'])==(scene,movement,route))
                frames=[];previous=None
                for i,(a,b) in enumerate(zip(ref['frames'],candidate['frames'])):
                    ia,ib,sky=rgb(a['frame_png']),rgb(b['frame_png']),rgb(bg['frames'][i]['frame_png'])
                    am,bm=opaque(ia,sky),opaque(ib,sky)
                    diff=np.max(abs(ia-ib),axis=-1)
                    record=dict(frame=i,image_over_8=int((diff>8).sum()),image_max=int(diff.max()),
                        foreground_mean_abs_channel=float(abs(ia-ib)[am|bm].mean()))
                    if qualified:
                        ys,xs=np.nonzero(am|bm)
                        y0,y1=max(int(ys.min())-4,0),min(int(ys.max())+5,am.shape[0])
                        x0,x1=max(int(xs.min())-4,0),min(int(xs.max())+5,am.shape[1])
                        ac,bc=am[y0:y1,x0:x1],bm[y0:y1,x0:x1]
                        ae=ac&~ndimage.binary_erosion(ac);be=bc&~ndimage.binary_erosion(bc)
                        distances=np.r_[ndimage.distance_transform_edt(~be)[ae],ndimage.distance_transform_edt(~ae)[be]]
                        record.update(false_foreground=int((bm&~am).sum()),false_background=int((am&~bm).sum()),
                            silhouette_boundary_pixels=quantiles(distances))
                        if previous is not None:
                            pa,pb=previous
                            record['unreprojected_mask_toggle_difference']=int(np.logical_xor(am^pa,bm^pb).sum())
                    previous=(am,bm);frames.append(record)
                results.append(dict(scene=scene,movement=movement,route=route,frames=frames))
    result=dict(mask_threshold_rgb8=4,mask_calibration=calibration,mask_method_qualified=qualified,
        scope='Opaque frozen-lighting contrast masks calibrated against native geometry; per-pose comparisons only, no motion reprojection or automatic flicker acceptance',
        sequences=results)
    (output/'temporal.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Temporal review: mask qualified',qualified,'calibration errors',sum(r['classification_errors'] for r in calibration),flush=True)


def supersampled_fixtures(root,output):
    original=json.loads(Path('projects/sdf/comparison/fixtures.json').read_text())
    roi=json.loads((root/'roi/roi.json').read_text());results=[]
    for label in roi['labels']:
        case=next(c for c in original['cases'] if c['name']==label['fixture'])
        node=case['scene']['root'];centre=np.array(node.get('position',[0,0,0]))
        spacing=label['viewport_world']/64
        coverage=np.zeros((64,64),dtype=float)
        for sy in range(4):
            for sx in range(4):
                y,x=np.mgrid[:64,:64]
                origins=np.stack([centre[0]+(x-32+label['pixel_phase']+(sx+.5)/4-.5)*spacing,
                    centre[1]+(32-y-label['pixel_phase']-((sy+.5)/4-.5))*spacing,
                    np.full((64,64),.7)],axis=-1)
                if node['type']=='sphere':
                    mask=np.sum((origins[:,:,:2]-centre[:2])**2,axis=-1)<=node['radius']**2
                else:
                    q=np.array(node.get('rotation_xyzw',[0,0,0,1]),dtype=float);q[:3]*=-1
                    rel=origins-centre
                    local=rel+2*np.cross(q[:3],np.cross(q[:3],rel)+q[3]*rel)
                    direction=np.array([0.,0.,-1.]);direction=direction+2*np.cross(q[:3],np.cross(q[:3],direction)+q[3]*direction)
                    with np.errstate(divide='ignore'):
                        a=(-np.array(node['half_extent'])-local)/direction
                        b=(np.array(node['half_extent'])-local)/direction
                    mask=np.max(np.minimum(a,b),axis=-1)<=np.min(np.maximum(a,b),axis=-1)
                coverage+=mask/16
        path=output/(label['name']+'-cpu-aa.png')
        Image.fromarray(np.round(coverage*255).astype(np.uint8)).save(path)
        results.append(dict(label,coverage_pixel_area=float(coverage.sum()),image=str(path)))
    (output/'fixture-aa.json').write_text(json.dumps(dict(samples_per_pixel=16,filter='box',
        reference='Independent mathematical sphere/oriented-box intersections; distinct from GPU finite-hit-band source reference',
        cases=results),indent=2)+'\n')


def native_temporal(root,output):
    sequences=json.loads((root/'sequence-geometry-final/sequences.json').read_text())
    if not sequences['complete']:raise RuntimeError('native pose geometry incomplete')
    comparisons=[]
    for scene in ('guardian','robot'):
        for movement in ('arc','dolly'):
            ref=next(r for r in sequences['runs'] if (r['scene'],r['movement'],r['route'])==(scene,movement,'source'))
            for route in ('sampled','refine03125','combined'):
                cand=next(r for r in sequences['runs'] if (r['scene'],r['movement'],r['route'])==(scene,movement,route))
                frames=[];previous=None
                for i,(ra,rb) in enumerate(zip(ref['frames'],cand['frames'])):
                    a=depth_only(Path(ra['quality_file']));b=depth_only(Path(rb['quality_file']))
                    am,bm=a>=0,b>=0
                    valid=(np.array(Image.open(ra['validity_file']))>0)&(np.array(Image.open(rb['validity_file']))>0)
                    edge_valid=~ndimage.binary_dilation(~valid,iterations=2)
                    ys,xs=np.nonzero(am|bm)
                    y0,y1=max(int(ys.min())-4,0),min(int(ys.max())+5,am.shape[0])
                    x0,x1=max(int(xs.min())-4,0),min(int(xs.max())+5,am.shape[1])
                    ac,bc=am[y0:y1,x0:x1],bm[y0:y1,x0:x1]
                    ev=edge_valid[y0:y1,x0:x1]
                    ae=ac&~ndimage.binary_erosion(ac);be=bc&~ndimage.binary_erosion(bc)
                    dist=np.r_[ndimage.distance_transform_edt(~be)[ae&ev],ndimage.distance_transform_edt(~ae)[be&ev]]
                    ia,ib=rgb(ra['frame_png']),rgb(rb['frame_png']);diff=np.max(abs(ia-ib),axis=-1)
                    record=dict(frame=i,unscored_pixels=int((~valid).sum()),
                        false_foreground=int((bm&~am&valid).sum()),false_background=int((am&~bm&valid).sum()),
                        silhouette_boundary_pixels=quantiles(dist),image_over_8=int((diff>8).sum()),
                        foreground_mean_abs_channel=float(abs(ia-ib)[am|bm].mean()))
                    if previous is not None:
                        pa,pb,pv=previous
                        record['unreprojected_mask_toggle_difference']=int((np.logical_xor(am^pa,bm^pb)&valid&pv).sum())
                    previous=(am,bm,valid);frames.append(record)
                comparisons.append(dict(scene=scene,movement=movement,route=route,frames=frames))
    result=dict(geometry_source='Per-frame native-validated depth/status snapshots; not RGB foreground inference',
        excluded_pixels='Every native validation/image mismatch remains unscored; two-pixel exclusion around silhouette measurements',
        scope='Static poses, paired reference sequence, no motion reprojection or automated human visual acceptance',
        comparisons=comparisons)
    (output/'native-temporal.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Native temporal geometry scored for',len(comparisons),'paired sequences',flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--native',action='store_true')
    args=p.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    if args.native:native_temporal(args.root,args.output)
    else:review(args.root,args.output)
    supersampled_fixtures(args.root,args.output)


if __name__=='__main__':main()
