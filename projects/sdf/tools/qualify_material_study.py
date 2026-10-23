#!/usr/bin/env python3
"""Qualify the opt-in material route without changing settled guardian inputs."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess
from sdf_author import material, sphere, smooth_union, subtract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    binary = root / 'build/release/projects/sdf/sdf'
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    read = lambda p: json.loads(p.read_text())
    receipt = dict(binary_sha256=sha(binary), source_commit=subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        shader_sha256={p.name:sha(p) for p in sorted((binary.parent/'shaders').glob('*.spv'))}, runs={})

    def run(name, config, options=(), fixture=None, validate=True):
        if fixture is not None:
            scene = out / f'{name}-scene.json'
            scene.write_text(json.dumps(fixture, indent=2)+'\n')
            config = copy.deepcopy(config)
            config['authored-scene'] = str(scene)
        cfg = out / f'{name}-preset.json'
        cfg.write_text(json.dumps(config, indent=2)+'\n')
        image = out / f'{name}.png'
        cmd = [str(binary), '--headless', '--require-validation', '--config', str(cfg),
               '--width', '480', '--height', '480', '--supersample', '1',
               '--output', str(image), *map(str, options)]
        if validate: cmd.append('--validate-field')
        r = subprocess.run(cmd, cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / f'{name}.log').write_text(r.stdout)
        assert r.returncode == 0 and 'validation error' not in r.stdout.lower(), r.stdout
        if validate: assert 'optics-only cache check passed builds=1' in r.stdout
        receipt['runs'][name] = dict(command=cmd, capture=image.name, sha256=sha(image),
            stats=[s for s in r.stdout.splitlines() if s.startswith('sdf:')])
        (out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')
        print(name, 'passed', flush=True)
        return image

    config = read(root/'projects/sdf/presets/material-study/sphere-bronze.json')
    for kind in ['plain','limestone','marble','clay','ceramic','bronze']:
        cfg = read(root/f'projects/sdf/presets/material-study/sphere-{kind}.json')
        direct = run(kind, cfg)
        full = run(kind+'-full', cfg, ['--no-material-fast-path'], validate=False)
        assert direct.read_bytes() == full.read_bytes(), kind
    scene = dict(format='cubey.sdf.scene',version=2,name='Mixed optics fixture',
        materials=[material('a','D0CCC3',.7),material('b','D7A768',.32)],
        root=smooth_union('blend',sphere('left',[-.10,0,0],.18,'a'),
                          sphere('right',[.10,0,0],.18,'b'),.08))
    scene['materials'][1]['optics'] = dict(metallic=1)
    run('mixed-ownership',config,fixture=scene)
    scene['root'] = subtract('cut',sphere('base',[0,0,0],.25,'a'),
                            sphere('tool',[.1,.08,.15],.16,'b'),cut_material='b')
    run('explicit-cut-metal',config,fixture=scene)
    furnace_cfg = dict(config, lighting='material-furnace', exposure=0, shadows=0, ao=False)
    for metal in [0,1]:
        for rough in [.05,.32,.9]:
            fixture = dict(format='cubey.sdf.scene',version=2,name='White furnace',
                materials=[dict(material('body','FFFFFF',rough), optics=dict(metallic=metal))],
                root=sphere('furnace',[0,0,0],.25,'body'))
            image = run(f'furnace-{metal}-{rough}', furnace_cfg, fixture=fixture, validate=False)
            rgb = subprocess.check_output(['ffmpeg','-v','error','-i',str(image),'-f','rawvideo','-pix_fmt','rgb24','-'])
            excess = sum(r>180 and g<90 and b>180 for r,g,b in zip(rgb[::3],rgb[1::3],rgb[2::3]))
            assert excess == 0, ('white furnace energy exceeded 1.02', metal, rough, excess)
    for subject in ['sphere', 'guardian']:
        cfg = read(root/f'projects/sdf/presets/material-study/{subject}-glass.json')
        run(subject+'-glass', cfg)
        run(subject+'-glass-diagnostics', cfg, ['--debug',9], validate=False)
    cfg = read(root/'projects/sdf/presets/material-study/sphere-glass.json')
    exported = out/'glass-export.json'
    run('glass-empty-history', cfg, ['--validate-history'], validate=False)
    original = run('glass-export', cfg, ['--export-authored',str(exported)], validate=False)
    restored = run('glass-reload', dict(cfg, **{'authored-scene':str(exported)}), validate=False)
    assert original.read_bytes()==restored.read_bytes()
    receipt['passed'] = True
    receipt['checks'] = dict(opaque_shortcut_exact=True,mixed_optics=True,explicit_cut=True,
                             white_furnace_max_energy=1.02,appearance_cache=True,
                             optical_interface_and_absorption_gpu=True,glass_roundtrip_exact=True)
    (out/'manifest.json').write_text(json.dumps(receipt,indent=2)+'\n')


if __name__ == '__main__':
    main()
