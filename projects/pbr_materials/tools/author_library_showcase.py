"""Rebuild the two SDF presentation scenes and matched mesh/SDF launch presets."""
from pathlib import Path
import argparse
import json
import copy

ROOT = Path(__file__).resolve().parents[3]
catalog = json.loads((ROOT / 'assets/materials/catalog.json').read_text())['entries']
materials = [{'name': e['id'], 'recipe': json.loads((ROOT / 'assets/materials' / e['recipe']).read_text())}
             for e in catalog]
def local_recipe(material, position):
    result = copy.deepcopy(material)
    # SDF evaluates a shared world field; mesh specimens supply local vertex
    # positions. This instance translation makes their recipe coordinates agree.
    domain = result['recipe'].get('material_from_object', [1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1])
    domain = domain.copy()
    for row in range(4):
        domain[12 + row] -= sum(domain[col * 4 + row] * position[col] for col in range(3))
    result['recipe']['material_from_object'] = domain
    return result

spheres = [{'name': e['id'] + '-sphere', 'type': 'sphere', 'radius': .075,
            'position': [((i % 4) - 1.5) * .252, (1 - i // 4) * .195, 0], 'material': e['id']}
           for i, e in enumerate(catalog)]
library = {'format': 'cubey.sdf.scene', 'version': 3, 'name': 'Twelve material spheres',
           'environment_only_gallery': True,
           'materials': [local_recipe(m, s['position']) for m, s in zip(materials, spheres)],
           'root': {'name': 'material-library', 'type': 'union', 'children': spheres}}
roster = ['wood', 'marble', 'rusted-steel', 'glass']
blocks, bores = [], []
for i, name in enumerate(roster):
    x, y = ((i % 2) - .5) * .546, (.5 - i // 2) * .377
    blocks.append({'name': name + '-block', 'type': 'box', 'half_extent': [.15275, .13, .104],
                   'position': [x, y, 0], 'material': name})
    bores.append({'name': name + '-bore', 'type': 'cylinder', 'radius': .05525,
                  'half_length': .26, 'position': [x, y, 0],
                  'rotation_xyzw': [.70710678, 0, 0, .70710678]})
cuts = {'format': 'cubey.sdf.scene', 'version': 3, 'name': 'Four material fields, one cut sequence',
        'environment_only_gallery': True,
        'materials': [local_recipe(next(m for m in materials if m['name'] == b['material']), b['position']) for b in blocks],
        'root': {'name': 'cut-roster', 'type': 'union', 'children': [
            {'name': b['material'] + '-cut', 'type': 'subtract', 'children': [b, bore]}
            for b, bore in zip(blocks, bores)]},
        'history': [{'nodes': [b['name'] for b in blocks], 'label': 'Wood, marble, steel and glass', 'seconds': 1.5},
                    {'nodes': [b['name'] for b in bores], 'label': 'Cuts retain each body material', 'seconds': 2}]}
files = {'projects/sdf/scenes/material-presentation/library-spheres.json': library,
         'projects/sdf/scenes/material-presentation/cut-roster.json': cuts}
for phase in ['dusk', 'dawn']:
    files[f'projects/pbr_materials/presets/presentation/library-{phase}.json'] = {
        'presentation': 'library', 'material': 'gallery', 'environment': 'clouds',
        'lighting_cycle': phase, 'distance': .855, 'yaw': 0, 'pitch': 0}
    files[f'projects/sdf/presets/presentation/library-{phase}.json'] = {
        'authored-scene': 'projects/sdf/scenes/material-presentation/library-spheres.json',
        'motion': 'static', 'duration': 8, 'lighting': 'material-atmosphere',
        'lighting-cycle': phase, 'clouds': True, 'cloud-time': 300, 'cloud-coverage': .28,
        'distance': .855, 'yaw': 0, 'pitch': 0, 'target-y': 0, 'shadows': 0,
        'glass-crossings': 16, 'environment-extent': 128}
files['projects/sdf/presets/presentation/cut-roster.json'] = {
    'authored-scene': 'projects/sdf/scenes/material-presentation/cut-roster.json',
    'motion': 'material-story', 'duration': 8, 'orbit-turns': 1 / 18,
    'lighting': 'material-atmosphere', 'sun-elevation': 8, 'sun-azimuth': -155,
    'exposure': .6, 'clouds': True, 'cloud-time': 300, 'cloud-coverage': .28,
    'distance': 1.04, 'yaw': .2, 'pitch': .25, 'shadows': 1, 'glass-crossings': 16}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for name, value in files.items():
        path = ROOT / name
        expected = json.dumps(value, indent=2) + '\n'
        if args.check:
            if not path.exists() or path.read_text() != expected:
                raise SystemExit('Stale generated presentation input: ' + name)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(expected)
    print(('Checked' if args.check else 'Wrote'), len(files), 'presentation inputs.')
