#!/usr/bin/env python3
"""Deterministic surface-only auditions on the retained mirrored guardian."""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
from sdf_author import material


MARBLE_DETAILS={
    'original':dict(scale=4.5,vein_frequency=5.5,vein_width=.08,warp_strength=5,grain_scale=220),
    'balanced':dict(scale=6,vein_frequency=7.25,vein_width=.07,warp_strength=5.5,grain_scale=280),
    'denser':dict(scale=9,vein_frequency=11,vein_width=.055,warp_strength=7,grain_scale=360),
    'fine':dict(scale=12,vein_frequency=12,vein_width=.05,warp_strength=8.5,grain_scale=360),
}


def variants(source,marble_detail='balanced',color_variation=.08):
    if not math.isfinite(color_variation) or not 0<=color_variation<=.25:
        raise ValueError('Marble color variation must be finite and between 0 and 0.25')
    limestone=material('body','B7ADA0',.82)
    limestone['procedural']=dict(type='limestone',seed=173,scale=9,
        secondary_color=material('secondary','827562')['base_color'],color_strength=.65,
        roughness_variation=.12,bump_strength=.00035,grain_scale=240,
        offset=[.13,-.07,.09])
    marble=material('body','D8D7CE',.42)
    # Add modest detail to the original broad marble without crowding the face.
    # Keep grain amplitude restrained for the honed finish.
    marble['procedural']=dict(type='marble',seed=47,
        secondary_color=material('secondary','65695F')['base_color'],color_strength=.88,
        roughness_variation=.055,bump_strength=.00008,
        vein_direction=[.67,.63,.19],offset=[.17,-.08,.13],
        color_variation=color_variation,**MARBLE_DETAILS[marble_detail])
    for name,appearance in (('limestone',limestone),('marble',marble)):
        scene=copy.deepcopy(source)
        scene['name']='Guardian / '+('carved limestone' if name=='limestone' else 'honed marble')
        scene['materials']=[appearance]
        scene['authoring']['surface_treatment']=name
        assert scene['root']==source['root'] and scene['history']==source['history']
        yield name,scene


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--marble-detail',choices=MARBLE_DETAILS,default='balanced')
    parser.add_argument('--marble-color-variation',type=float,default=.08,
                        help='Warm/cool body variation, 0 to 0.25; zero retains the pale material')
    args=parser.parse_args()
    if not math.isfinite(args.marble_color_variation) or not 0<=args.marble_color_variation<=.25:
        parser.error('--marble-color-variation must be finite and between 0 and 0.25')
    root=Path(__file__).resolve().parents[3]
    original=root/'projects/sdf/scenes/sculpture-guardian-mirrored.json'
    source=json.loads(original.read_text())
    for name,document in variants(source,args.marble_detail,args.marble_color_variation):
        document['authoring']['surface_source_sha256']=hashlib.sha256(original.read_bytes()).hexdigest()
        scene=Path(f'projects/sdf/scenes/sculpture-guardian-{name}.json')
        (root/scene).write_text(json.dumps(document,indent=2)+'\n')
        for build in (False,True):
            suffix='-build' if build else ''
            config=json.loads((root/f'projects/sdf/presets/sculpture-guardian-mirrored{suffix}.json').read_text())
            config['authored-scene']=str(scene)
            (root/f'projects/sdf/presets/sculpture-guardian-{name}{suffix}.json').write_text(json.dumps(config,indent=2)+'\n')
        print(name,len(document['history']),'operations; geometry unchanged')


if __name__=='__main__':main()
