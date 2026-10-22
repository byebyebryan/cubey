#!/usr/bin/env python3
"""Deterministic surface-only auditions on the retained mirrored guardian."""
import copy
import hashlib
import json
from pathlib import Path
from sdf_author import material


def variants(source):
    limestone=material('body','B7ADA0',.82)
    limestone['procedural']=dict(type='limestone',seed=173,scale=9,
        secondary_color=material('secondary','827562')['base_color'],color_strength=.65,
        roughness_variation=.12,bump_strength=.00035,grain_scale=240,
        offset=[.13,-.07,.09])
    marble=material('body','D8D7CE',.42)
    # Finer turbulent beds and denser, narrower veins; leave grain amplitude
    # restrained so the honed finish still differs from the limestone.
    marble['procedural']=dict(type='marble',seed=47,scale=12,
        secondary_color=material('secondary','65695F')['base_color'],color_strength=.88,
        roughness_variation=.055,bump_strength=.00008,grain_scale=360,
        vein_direction=[.67,.63,.19],vein_frequency=12,vein_width=.05,
        warp_strength=8.5,offset=[.17,-.08,.13])
    for name,appearance in (('limestone',limestone),('marble',marble)):
        scene=copy.deepcopy(source)
        scene['name']='Guardian / '+('carved limestone' if name=='limestone' else 'honed marble')
        scene['materials']=[appearance]
        scene['authoring']['surface_treatment']=name
        assert scene['root']==source['root'] and scene['history']==source['history']
        yield name,scene


def main():
    root=Path(__file__).resolve().parents[3]
    original=root/'projects/sdf/scenes/sculpture-guardian-mirrored.json'
    source=json.loads(original.read_text())
    for name,document in variants(source):
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
