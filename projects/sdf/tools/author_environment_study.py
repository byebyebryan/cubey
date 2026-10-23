#!/usr/bin/env python3
"""Emit presentation-only presets referencing the unchanged accepted materials."""
import argparse
import json
from pathlib import Path


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',choices=['initial','final'],default='final');a=p.parse_args()
    root=Path(__file__).resolve().parents[3];out=root/'projects/sdf/presets/environment-study';out.mkdir(exist_ok=True)
    for subject in ['sphere','guardian']:
        for material in ['bronze','glass','marble','clay','plain']:
            for environment in ['reference','sunset','clouds']:
                cfg=json.loads((root/f'projects/sdf/presets/material-study/{subject}-{material}.json').read_text())
                cfg['glass-crossings']=8
                if environment!='reference':
                    cfg.update({'lighting':'material-atmosphere','environment-extent':128,
                        'sun-elevation':4 if a.stage=='initial' else 8,'sun-azimuth':70 if a.stage=='initial' else -155,'exposure':-.4 if a.stage=='initial' else .6,
                        'clouds':environment=='clouds','cloud-time':300,
                        'cloud-coverage':.28})
                if environment=='clouds':
                    cfg.update({'cloud-distance-mode':'auto','cloud-quality':'half',
                        'cloud-view-samples':2,'cloud-resolve-mode':'terrain-post',
                        'cloud-sky-only':True})
                (out/f'{subject}-{material}-{environment}.json').write_text(json.dumps(cfg,indent=2)+'\n')
    print('Authored 30 presentation presets; accepted scene/material files unchanged.')


if __name__=='__main__':main()
