#!/usr/bin/env python3
"""Author three original robot silhouettes as legacy box/sphere recipes."""
import argparse
import json
from pathlib import Path
import struct


def robot(variant):
    edits = []

    def box(center, extent, rounding=.01, remove=False):
        edits.append(dict(operation='remove' if remove else 'fill', center=center,
                          rotation_xyzw=[0, 0, 0, 1], half_extent=extent, rounding=rounding))

    def sphere(center, radius, remove=False):
        box(center, [0, 0, 0], radius, remove)

    width, height, head_y, body_h = {
        'survey': (.155, .095, .225, .12),
        'tall': (.125, .075, .285, .15),
        'broad': (.19, .085, .205, .10),
    }[variant]
    box([0, head_y, 0], [width, height, .075], .035)
    sphere([0, head_y, .11], .073, True)
    sphere([0, head_y, .105], .054)
    for side in (-1, 1):
        box([side * (width + .04), head_y, 0], [.012, .027, .035], .008)
    box([.1, head_y + height + .075, -.02], [.005, .042, .005], .004)
    sphere([.1, head_y + height + .122, -.02], .016)
    sphere([0, .09 if variant != 'tall' else .15, 0], .04)
    box([0, -.035 if variant != 'tall' else -.005, 0], [.105, body_h, .07], .025)
    box([0, -.035, .09], [.06, .042, .007], .01)
    box([0, -.025, -.115], [.07, .08, .027], .018)
    for side in (-1, 1):
        shoulder = side * .16
        sphere([shoulder, .005, 0], .04)
        box([side * .18, -.065, 0], [.019, .045, .023], .012)
        sphere([side * .185, -.125, 0], .029)
        box([side * .195, -.172, .015], [.022, .027, .026], .011)
        sphere([side * .20, -.217, .025], .032)
        box([side * .075, -.247, 0], [.024, .046, .023], .011)
        sphere([side * .075, -.307, .015], .026)
        box([side * .085, -.351, .04], [.043, .025, .065], .016)
    return edits


def export_recipe(edits, variant):
    # Identity-only transforms keep this small legacy-format exporter exact.
    value = 14695981039346656037
    for edit in edits:
        lanes = [*edit['center'], 1. if edit['operation'] == 'fill' else 2.,
                 -0., -0., -0., 1., *edit['half_extent'], edit['rounding']]
        for byte in struct.pack('<12f', *lanes):
            value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return dict(format='cubey.sdf.boxes', version=1,
                generator=dict(name=f'robot-blockout-{variant}-v1', seed=0),
                hash_algorithm='fnv1a64-gpu-lanes-le-v1', hash=f'{value:016x}', edits=edits)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for variant in ('survey', 'tall', 'broad'):
        path = args.output / f'{variant}.json'
        path.write_text(json.dumps(export_recipe(robot(variant), variant), indent=2) + '\n')
        print(path)
