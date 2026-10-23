#!/usr/bin/env python3
"""Emit one shared material catalog on a sphere and the settled guardian."""
import copy
import hashlib
import json
from pathlib import Path
from sdf_author import material, sphere


def catalog(root):
    def body(suffix):
        return json.loads((root / f'projects/sdf/scenes/sculpture-guardian-{suffix}.json').read_text())['materials'][0]
    rows = dict(plain=body('mirrored'), limestone=body('limestone'), marble=body('marble'))
    rows['clay'] = material('body', 'B87453', .90)
    rows['ceramic'] = material('body', '387D77', .22)
    rows['bronze'] = material('body', 'D7A768', .32)
    rows['bronze']['optics'] = dict(metallic=1)
    rows['glass'] = material('body', 'FFFFFF', .05)
    rows['glass']['optics'] = dict(transmission=1, ior=1.5,
                                  attenuation_color=[.65, .9, .76], attenuation_distance=.5)
    return rows


def main():
    root = Path(__file__).resolve().parents[3]
    source = root / 'projects/sdf/scenes/sculpture-guardian-mirrored.json'
    guardian = json.loads(source.read_text())
    sphere_scene = dict(format='cubey.sdf.scene', version=2, name='Material sphere',
        materials=[], root=sphere('material-sphere', [0, 0, 0], .25, 'body'),
        history=[dict(node='material-sphere', label='Establish the material sphere', seconds=1)])
    plain_config = json.loads((root / 'projects/sdf/presets/sculpture-guardian-mirrored.json').read_text())
    rows = catalog(root)
    for subject, template in [('sphere', sphere_scene), ('guardian', guardian)]:
        for name, appearance in rows.items():
            scene = copy.deepcopy(template)
            scene['name'] = f'Material study / {subject} / {name}'
            scene['materials'] = [appearance]
            scene['authoring'] = dict(generator='cubey-material-study-v1', subject=subject,
                material=name, shared_material_sha256=hashlib.sha256(json.dumps(appearance,sort_keys=True).encode()).hexdigest())
            if subject == 'guardian':
                assert scene['root'] == guardian['root'] and scene['history'] == guardian['history']
            path = Path(f'projects/sdf/scenes/material-study/{subject}-{name}.json')
            config = copy.deepcopy(plain_config)
            config.update({'authored-scene':str(path), 'lighting':'material-study',
                           'exposure':.6, 'motion':'static', 'ao':False, 'ground':False})
            if subject == 'sphere': config.update({'distance':1.05,'target-y':0,'pitch':.15})
            for target, document in [(root/path, scene),
                    (root/f'projects/sdf/presets/material-study/{subject}-{name}.json', config)]:
                target.parent.mkdir(parents=True,exist_ok=True)
                target.write_text(json.dumps(document,indent=2)+'\n')
    print('Authored seven shared material records on both fixed subjects.')


if __name__ == '__main__':
    main()
