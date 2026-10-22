"""Small build-time helpers for explicit, named Cubey SDF scenes."""
import json
import math
from pathlib import Path


def rotation(axis, degrees):
    length = math.sqrt(sum(x*x for x in axis))
    angle = math.radians(degrees) / 2
    return [*(x / length * math.sin(angle) for x in axis), math.cos(angle)]


def material(name, color, roughness=.5, emission=0):
    rgb = [int(color[i:i+2], 16) / 255 for i in (0, 2, 4)]
    linear = [v / 12.92 if v <= .04045 else ((v + .055) / 1.055)**2.4 for v in rgb]
    return dict(name=name, base_color=linear, roughness=roughness,
                emission=[v * emission for v in linear])


def shape(kind, name, position=(0, 0, 0), material=None, rotation_xyzw=None, **parameters):
    result = dict(type=kind, name=name, position=list(position), **parameters)
    if material is not None:
        result['material'] = material
    if rotation_xyzw is not None:
        result['rotation_xyzw'] = rotation_xyzw
    return result


def box(name, position, extent, rounding=.01, material=None, **options):
    return shape('box', name, position, material, half_extent=list(extent), rounding=rounding, **options)


def sphere(name, position, radius, material=None):
    return shape('sphere', name, position, material, radius=radius)


def cylinder(name, position, radius, half_length, material=None, **options):
    return shape('cylinder', name, position, material, radius=radius, half_length=half_length, **options)


def torus(name, position, major_radius, minor_radius, material=None, **options):
    return shape('torus', name, position, material, major_radius=major_radius, minor_radius=minor_radius, **options)


def capped_cone(name, position, bottom_radius, top_radius, half_length, material=None, **options):
    return shape('capped-cone', name, position, material, bottom_radius=bottom_radius,
                 top_radius=top_radius, half_length=half_length, **options)


def capsule(name, start, end, radius, material=None):
    delta = [b-a for a, b in zip(start, end)]
    length = math.sqrt(sum(v*v for v in delta))
    direction = [v / length for v in delta]
    q = [direction[2], 0, -direction[0], 1 + direction[1]]
    norm = math.sqrt(sum(v*v for v in q))
    q = [v / norm for v in q] if norm > 1e-8 else [1, 0, 0, 0]
    return shape('capsule', name, [(a+b)/2 for a, b in zip(start, end)], material,
                 rotation_xyzw=q, radius=radius, half_length=length/2)


def round_cone(name, start, end, bottom_radius, top_radius, material=None):
    """Convex hull of endpoint spheres; equal radii give an exact capsule."""
    node = capsule(name, start, end, max(bottom_radius, top_radius), material)
    node.update(type='round-cone', bottom_radius=bottom_radius, top_radius=top_radius)
    del node['radius']
    return node


def union(name, *children, **options):
    return shape('union', name, children=list(children), **options)


def subtract(name, body, cutter, cut_material=None, **options):
    result = shape('subtract', name, children=[body, cutter], **options)
    if cut_material is not None:
        result['cut_material'] = cut_material
    return result


def smooth_union(name, a, b, radius, blend_materials=False, **options):
    return shape('smooth-union', name, children=[a, b], radius=radius,
                 blend_materials=blend_materials, **options)


def smooth_subtract(name, body, cutter, radius, cut_material=None, **options):
    result = subtract(name, body, cutter, cut_material, **options)
    result.update(type='smooth-subtract', radius=radius)
    return result


def write_scene(path, name, materials, root, history=None):
    document = dict(format='cubey.sdf.scene', version=2, name=name,
                    materials=materials, root=root,
                    authoring=dict(generator='cubey-python-author-v1', units='workspace'))
    if history is not None:
        document['history'] = history
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2) + '\n')
    return document
